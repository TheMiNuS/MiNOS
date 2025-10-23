// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#pragma once
#include "MnConfig.hpp"
#include "esp_err.h"

class MnWiFi {
public:
    explicit MnWiFi(MnConfig& c) : cfg(c) {}
    esp_err_t begin();   // AP if empty SSID, otherwise STA test
    void      maintain();
    esp_err_t apply_new_cfg_and_test(); // used by /wifi endpoint
private:
    MnConfig& cfg;
    bool sta_connected_ = false;
    esp_err_t start_ap();
    esp_err_t start_sta(const char* ssid, const char* pass, int timeout_ms, bool* ok);
    void reboot();
};
