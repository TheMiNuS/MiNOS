// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#include "MnOta.hpp"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// OTA endpoint is registered by MnWeb with user_ctx = MnWeb*
#include "MnWeb.hpp"

#include <string>    // std::string
#include <vector>    // std::vector
#include <cstring>   // strlen, strstr, memcpy...

esp_err_t MnOta::handle_upload(httpd_req_t* req) {
    // Protect OTA: reuse the same Basic Auth policy as the web UI
    if (req && req->user_ctx) {
        auto* web = reinterpret_cast<MnWeb*>(req->user_ctx);
        if (web && !web->check_auth(req)) {
            // check_auth already sent 401
            return ESP_OK;
        }
    }

    // Retrieve Content-Type to detect if it's multipart
    char ctype[128] = {0};
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));

    const esp_partition_t* update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return err;
    }

    // Stream reading + optional multipart extraction
    // Supports: 
    //   1) application/octet-stream (raw)
    //   2) multipart/form-data; boundary=...
    const bool is_multipart = (strstr(ctype, "multipart/form-data") != nullptr);
    std::string boundary;
    if (is_multipart) {
        const char* b = strstr(ctype, "boundary=");
        if (!b) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing boundary");
            esp_ota_end(handle);
            return ESP_FAIL;
        }
        boundary = "--";
        boundary += (b + strlen("boundary="));   // ex: "--------------------------------d74496d66958873e"
    }

    // Overlap window to detect boundary across chunk edges
    const size_t TAIL = (is_multipart ? boundary.size() + 8 : 0);
    std::string window; window.reserve(TAIL);

    bool started_payload = false;
    bool done = false;
    size_t total_written = 0;

    // Helper to find header end (CRLFCRLF)
    auto find_hdr_end = [](const std::string& s, size_t from) -> size_t {
        size_t p = s.find("\r\n\r\n", from);
        return p == std::string::npos ? std::string::npos : (p + 4);
    };

    // Receive loop
    std::vector<uint8_t> buf(4096);
    int received_total = 0;
    int spin = 0;

    while (!done && received_total < req->content_len) {
        int r = httpd_req_recv(req, (char*)buf.data(), buf.size());
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) break;
        received_total += r;

        // Combine overlap + current chunk
        std::string view;
        if (is_multipart) {
            view.reserve(window.size() + r);
            view.assign(window.begin(), window.end());
            view.append((const char*)buf.data(), r);
        } else {
            // Non-multipart: direct write
            if (esp_ota_write(handle, buf.data(), r) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                esp_ota_end(handle);
                return ESP_FAIL;
            }
            total_written += r;
            if (++spin % 16 == 0) vTaskDelay(1);
            continue;
        }

        if (is_multipart) {
            // Multipart parsing
            size_t write_from = 0;
            size_t write_upto = view.size();

            if (!started_payload) {
                // Look for first boundary, then header end (CRLFCRLF)
                size_t bp = view.find(boundary);     // "--boundary"
                if (bp == std::string::npos) {
                    // Keep tail for next iteration
                    if (view.size() > TAIL) {
                        window.assign(view.end() - TAIL, view.end());
                    } else {
                        window = view;
                    }
                    continue;
                }
                size_t hdr_end = find_hdr_end(view, bp);
                if (hdr_end == std::string::npos) {
                    // Header not fully received yet
                    if (view.size() > TAIL) {
                        window.assign(view.end() - TAIL, view.end());
                    } else {
                        window = view;
                    }
                    continue;
                }
                started_payload = true;
                write_from = hdr_end; // Start of firmware bytes
            }

            // Look for end of part: "\r\n--boundary" or "\r\n--boundary--"
            std::string end1 = "\r\n" + boundary;
            std::string end2 = end1 + "--";

            size_t end_pos = view.find(end1, write_from);
            if (end_pos == std::string::npos) {
                // End not found → write all except last TAIL bytes
                if (view.size() > write_from + TAIL) {
                    write_upto = view.size() - TAIL;
                } else {
                    write_upto = write_from; // nothing to write yet
                }
            } else {
                // Found boundary → write until before CRLF
                write_upto = end_pos;
                done = true;
            }

            // Write valid payload chunk
            if (write_upto > write_from) {
                const uint8_t* p = (const uint8_t*)view.data() + write_from;
                size_t n = write_upto - write_from;
                if (esp_ota_write(handle, p, n) != ESP_OK) {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                    esp_ota_end(handle);
                    return ESP_FAIL;
                }
                total_written += n;
            }

            // Prepare overlap window for next iteration if not done
            if (!done) {
                if (view.size() > TAIL) {
                    window.assign(view.end() - TAIL, view.end());
                } else {
                    window = view;
                }
            }
        }

        if (++spin % 16 == 0) vTaskDelay(1);
    }

    if (total_written == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty or invalid OTA payload");
        esp_ota_end(handle);
        return ESP_FAIL;
    }

    if (esp_ota_end(handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    esp_err_t setb = esp_ota_set_boot_partition(update_part);
    if (setb != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    // Send OK response then reboot
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK, rebooting");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

