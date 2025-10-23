// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#include "MnWiFi.hpp"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "MnWiFi";

static EventGroupHandle_t wifi_evt;
static const int GOT_IP_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_evt, GOT_IP_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_evt, GOT_IP_BIT);
    }
}

esp_err_t MnWiFi::start_ap() {
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));
    wifi_config_t ap; memset(&ap, 0, sizeof(ap));
    strncpy((char*)ap.ap.ssid, cfg.cfg.hostname, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = strlen((char*)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGW(TAG, "AP mode started (ssid=%s)", ap.ap.ssid);
    return ESP_OK;
}

esp_err_t MnWiFi::start_sta(const char* ssid, const char* pass, int timeout_ms, bool* ok) {
    *ok = false;
    if (!wifi_evt) wifi_evt = xEventGroupCreate();
    xEventGroupClearBits(wifi_evt, GOT_IP_BIT);

    // Stop/deinit in case an AP was already running (e.g., after a web reconfiguration)
    esp_wifi_stop();
    esp_wifi_deinit();

    // (Re)create the STA interface if it does not exist yet
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    // Set DHCP hostname from configuration
    if (sta_netif && cfg.cfg.hostname[0] != '\0') {
        ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, cfg.cfg.hostname));
    }

    wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));

    // Register Wi-Fi/IP event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t sta; memset(&sta, 0, sizeof(sta));
    strncpy((char*)sta.sta.ssid, ssid, sizeof(sta.sta.ssid) - 1);
    strncpy((char*)sta.sta.password, pass, sizeof(sta.sta.password) - 1);

    // Auth mode: OPEN if empty password, otherwise WPA2-PSK
    sta.sta.threshold.authmode = (sta.sta.password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "STA trying SSID '%s' (pwd_len=%d)", (char*)sta.sta.ssid, (int)strlen((char*)sta.sta.password));

    EventBits_t bits = xEventGroupWaitBits(
        wifi_evt, GOT_IP_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & GOT_IP_BIT) {
        *ok = true; sta_connected_ = true;
        ESP_LOGI(TAG, "STA connected and got IP");
    } else {
        *ok = false; sta_connected_ = false;
        ESP_LOGW(TAG, "STA timeout waiting for IP (%d ms)", timeout_ms);
    }
    return ESP_OK;
}

esp_err_t MnWiFi::begin() {
    esp_netif_init();
    esp_event_loop_create_default();

    if (cfg.cfg.wifi_ssid[0] == '\0') {
        ESP_LOGW(TAG, "No SSID → AP recovery");
        return start_ap();
    }

    bool ok = false;
    start_sta(cfg.cfg.wifi_ssid, cfg.cfg.wifi_password, 30000, &ok);
    if (!ok && cfg.cfg.WifiConfig == 0xAAAA) {
        ESP_LOGE(TAG, "New Wi-Fi config failed → rollback & reboot");
        cfg.cfg.WifiConfig = 0xAAAA;
        strncpy(cfg.cfg.wifi_ssid,     cfg.cfg.old_wifi_ssid,     sizeof(cfg.cfg.wifi_ssid));
        strncpy(cfg.cfg.wifi_password, cfg.cfg.old_wifi_password, sizeof(cfg.cfg.wifi_password));
        cfg.save();
        reboot();
    } else if (ok && cfg.cfg.WifiConfig == 0xAAAA) {
        ESP_LOGI(TAG, "New Wi-Fi config works → commit & reboot");
        cfg.cfg.WifiConfig = 0x5555;
        cfg.save();
        reboot();
    } else if (!ok) {
        ESP_LOGW(TAG, "STA failed → AP fallback");
        return start_ap();
    }
    return ESP_OK;
}

void MnWiFi::reboot() {
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

void MnWiFi::maintain() {
    // Optional: add periodic Wi‑Fi health checks here
}

esp_err_t MnWiFi::apply_new_cfg_and_test() {
    bool ok = false;
    start_sta(cfg.cfg.wifi_ssid, cfg.cfg.wifi_password, 30000, &ok);
    if (ok) { cfg.cfg.WifiConfig = 0x5555; cfg.save(); reboot(); }
    else    { cfg.cfg.WifiConfig = 0xAAAA; cfg.save(); reboot(); }
    return ESP_OK;
}
