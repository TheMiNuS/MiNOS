// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#pragma once
#include <cstring>
#include "esp_err.h"

struct SystemConfig {
    uint16_t FlashStatus;      // 0x5555 = init OK
    uint16_t WifiConfig;       // 0x5555 normal, 0xAAAA = update in test
    char wifi_ssid[32];
    char wifi_password[64];
    char old_wifi_ssid[32];
    char old_wifi_password[64];
    char ota_password[64];
    char hostname[32];
    char http_login[32];
    char http_password[64];
    char mqtt_login[32];
    char mqtt_password[64];
    char mqtt_host[32];
    uint16_t mqtt_port;
    uint8_t Sensitivity;
};

class MnConfig {
public:
    SystemConfig cfg{};
    esp_err_t load_or_init();
    esp_err_t save();
    void reset_defaults(const char* mac_str);
};
