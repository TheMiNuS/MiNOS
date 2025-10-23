// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//

#include "MnConfig.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <cstdio>

static const char* NVS_NS = "sys";
static const char* NVS_KEY = "cfg";

esp_err_t MnConfig::load_or_init() {
    nvs_handle_t h; size_t sz = sizeof(SystemConfig);
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_blob(h, NVS_KEY, &cfg, &sz);
    if (err == ESP_OK && cfg.FlashStatus == 0x5555) { nvs_close(h); return ESP_OK; }

    uint8_t mac[6];
    esp_err_t mac_ok = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (mac_ok != ESP_OK) {
    // fallback: lit le MAC par défaut depuis les eFuses
    esp_efuse_mac_get_default(mac);
    }

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    reset_defaults(mac_str);
    nvs_set_blob(h, NVS_KEY, &cfg, sizeof(cfg));
    nvs_commit(h); nvs_close(h);
    esp_restart();
    return ESP_OK;
}

void MnConfig::reset_defaults(const char* mac_str) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.FlashStatus = 0x5555;
    cfg.WifiConfig  = 0x5555;              // empty SSID => AP fallback
    strncpy(cfg.hostname, mac_str, sizeof(cfg.hostname)-1);
    strncpy(cfg.http_login, "admin", sizeof(cfg.http_login)-1);
    strncpy(cfg.http_password, "admin", sizeof(cfg.http_password)-1);
    strncpy(cfg.mqtt_host, "127.0.0.1", sizeof(cfg.mqtt_host)-1);
    cfg.mqtt_port = 1883;
    cfg.Sensitivity = 0xFF;
    // keep same OTA password hash if desired (placeholder here)
    strncpy(cfg.ota_password, "7effe6c005a70b573c5373d327335d19", sizeof(cfg.ota_password)-1);
}

esp_err_t MnConfig::save() {
    nvs_handle_t h; ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_blob(h, NVS_KEY, &cfg, sizeof(cfg)));
    ESP_ERROR_CHECK(nvs_commit(h)); nvs_close(h); return ESP_OK;
}
