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
#include "esp_heap_caps.h"

// OTA endpoint is registered by MnWeb with user_ctx = MnWeb*
#include "MnWeb.hpp"

#include <string>    // std::string
#include <vector>    // std::vector
#include <cstring>   // strlen, strstr, memcpy...

#if __has_include("esp_app_format.h")
  #include "esp_app_format.h"   // esp_image_header_t in recent IDF
#elif __has_include("esp_image_format.h")
  #include "esp_image_format.h"
#endif

static bool looks_like_esp_idf_image(const uint8_t* b, size_t n) {
    if (!b || n < 24) return false;

    // Quick reject multipart boundary
    if (n >= 2 && b[0] == '-' && b[1] == '-') return false;

#if defined(ESP_IMAGE_HEADER_MAGIC)
    // Some IDF expose constants; otherwise just check magic 0xE9.
#endif

    // esp_image_header_t layout starts with:
    // magic (0xE9), segment_count, spi_mode, spi_speed+size, entry_addr...
    const uint8_t magic = b[0];
    if (magic != 0xE9) return false;

    const uint8_t seg = b[1];
    if (seg == 0 || seg > 16) return false;          // typical range

    const uint8_t spi_mode = b[2];
    if (spi_mode > 5) return false;                  // conservative

    // b[3] packs flash freq/size in old formats; just ensure not 0xFF garbage
    if (b[3] == 0xFF) return false;

    // entry_addr is little-endian at offset 4
    const uint32_t entry =
        ((uint32_t)b[4]) |
        ((uint32_t)b[5] << 8) |
        ((uint32_t)b[6] << 16) |
        ((uint32_t)b[7] << 24);

    // Heuristic: entry is typically in IRAM/IROM map, avoid clearly invalid values
    if (entry == 0 || entry == 0xFFFFFFFF) return false;

    return true;
}


esp_err_t MnOta::handle_upload(httpd_req_t* req) {
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

    // ---- Buffers on HEAP (NOT stack) to avoid httpd thread stack overflow ----
    constexpr size_t CHUNK = 1024;          // reduce peak RAM and stack pressure
    constexpr size_t TAIL_MAX = 256;
    uint8_t* rx   = (uint8_t*)heap_caps_malloc(CHUNK,          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t* win  = (uint8_t*)heap_caps_malloc(CHUNK+TAIL_MAX, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t* tail = (uint8_t*)heap_caps_malloc(TAIL_MAX,       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    auto cleanup = [&](esp_err_t ret) -> esp_err_t {
        if (rx)   heap_caps_free(rx);
        if (win)  heap_caps_free(win);
        if (tail) heap_caps_free(tail);
        return ret;
    };

    if (!rx || !win || !tail) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No RAM for OTA buffers");
        esp_ota_end(handle);
        return cleanup(ESP_FAIL);
    }

    auto ota_write = [&](const uint8_t* p, size_t n) -> bool {
        if (n == 0) return true;
        return (esp_ota_write(handle, p, n) == ESP_OK);
    };

    // Read first chunk to detect RAW vs multipart (avoid header API)
    int first_r = 0;
    while (first_r == 0) {
        first_r = httpd_req_recv(req, (char*)rx, CHUNK);
        if (first_r == HTTPD_SOCK_ERR_TIMEOUT) first_r = 0;
        else if (first_r < 0) break;
    }
    if (first_r <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No payload");
        esp_ota_end(handle);
        return cleanup(ESP_FAIL);
    }

    int received_total = first_r;
    size_t total_written = 0;

    const bool looks_raw = looks_like_esp_idf_image(rx, (size_t)first_r);


    // ----------------------------
    // RAW upload
    // ----------------------------
    if (looks_raw) {
        if (!ota_write(rx, (size_t)first_r)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            esp_ota_end(handle);
            return cleanup(ESP_FAIL);
        }
        total_written += (size_t)first_r;

        while (received_total < req->content_len) {
            int r = httpd_req_recv(req, (char*)rx, CHUNK);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (r <= 0) break;
            received_total += r;

            if (!ota_write(rx, (size_t)r)) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                esp_ota_end(handle);
                return cleanup(ESP_FAIL);
            }
            total_written += (size_t)r;
            if ((total_written & 0x3FFF) == 0) vTaskDelay(1);
        }

    } else {
        // ----------------------------
        // multipart/form-data
        // boundary is first line: "--<token>\r\n"
        // ----------------------------
        int crlf = -1;
        for (int i = 0; i + 1 < first_r; ++i) {
            if (rx[i] == '\r' && rx[i + 1] == '\n') { crlf = i; break; }
        }
        if (crlf < 2 || rx[0] != '-' || rx[1] != '-') {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown upload format");
            esp_ota_end(handle);
            return cleanup(ESP_FAIL);
        }

        char boundary[128];
        size_t boundary_len = (size_t)crlf;
        if (boundary_len >= sizeof(boundary)) boundary_len = sizeof(boundary) - 1;
        memcpy(boundary, rx, boundary_len);
        boundary[boundary_len] = 0;

        char end1[132];
        size_t end1_len = 0;
        end1[end1_len++] = '\r';
        end1[end1_len++] = '\n';
        memcpy(end1 + end1_len, boundary, boundary_len);
        end1_len += boundary_len;

        size_t TAIL = boundary_len + 8;
        if (TAIL < 32) TAIL = 32;
        if (TAIL > TAIL_MAX) TAIL = TAIL_MAX;

        size_t tail_len = 0;
        bool started_payload = false;
        bool done = false;
        int spin = 0;

        auto find_subseq = [](const uint8_t* hay, size_t hay_len,
                              const uint8_t* needle, size_t needle_len,
                              size_t from) -> size_t {
            if (!hay || !needle || needle_len == 0 || hay_len < needle_len || from > hay_len) return (size_t)-1;
            for (size_t i = from; i + needle_len <= hay_len; ++i) {
                if (hay[i] == needle[0] && memcmp(hay + i, needle, needle_len) == 0) return i;
            }
            return (size_t)-1;
        };

        const uint8_t hdr_end_pat[] = {'\r','\n','\r','\n'};

        bool have_prefetched = true;
        int prefetched_len = first_r;

        while (!done && received_total <= req->content_len) {
            int r = 0;

            if (have_prefetched) {
                r = prefetched_len;
                have_prefetched = false;
            } else {
                if (received_total >= req->content_len) break;
                r = httpd_req_recv(req, (char*)rx, CHUNK);
                if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
                if (r <= 0) break;
                received_total += r;
            }

            // win = tail + rx
            size_t win_len = 0;
            if (tail_len) {
                memcpy(win, tail, tail_len);
                win_len += tail_len;
            }
            memcpy(win + win_len, rx, (size_t)r);
            win_len += (size_t)r;

            size_t write_from = 0;
            size_t write_upto = win_len;

            if (!started_payload) {
                size_t bp = find_subseq(win, win_len, (const uint8_t*)boundary, boundary_len, 0);
                if (bp == (size_t)-1) {
                    tail_len = (win_len > TAIL) ? TAIL : win_len;
                    memcpy(tail, win + (win_len - tail_len), tail_len);
                    continue;
                }

                size_t hdr_end = find_subseq(win, win_len, hdr_end_pat, sizeof(hdr_end_pat), bp);
                if (hdr_end == (size_t)-1) {
                    tail_len = (win_len > TAIL) ? TAIL : win_len;
                    memcpy(tail, win + (win_len - tail_len), tail_len);
                    continue;
                }

                started_payload = true;
                write_from = hdr_end + sizeof(hdr_end_pat);
            }

            size_t end_pos = find_subseq(win, win_len, (const uint8_t*)end1, end1_len, write_from);
            if (end_pos == (size_t)-1) {
                if (win_len > write_from + TAIL) write_upto = win_len - TAIL;
                else write_upto = write_from;
            } else {
                write_upto = end_pos;
                done = true;
            }

            if (write_upto > write_from) {
                const uint8_t* p = win + write_from;
                size_t n = write_upto - write_from;

                if (!ota_write(p, n)) {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                    esp_ota_end(handle);
                    return cleanup(ESP_FAIL);
                }
                total_written += n;
            }

            if (!done) {
                tail_len = (win_len > TAIL) ? TAIL : win_len;
                memcpy(tail, win + (win_len - tail_len), tail_len);
            }

            if (++spin % 16 == 0) vTaskDelay(1);
        }
    }

    // ---- Finalization ----
    if (total_written == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty or invalid OTA payload");
        esp_ota_end(handle);
        return cleanup(ESP_FAIL);
    }

    if (esp_ota_end(handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return cleanup(ESP_FAIL);
    }

    if (esp_ota_set_boot_partition(update_part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return cleanup(ESP_FAIL);
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK, rebooting");
    cleanup(ESP_OK); // free buffers before reboot
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}
