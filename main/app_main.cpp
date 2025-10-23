// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "MnConfig.hpp"
#include "MnWiFi.hpp"
#include "MnTime.hpp"
#include "MnWeb.hpp"
#include "MnOta.hpp"
#include "examples_demo.hpp"

static const char* TAG = "APP";

extern "C" void app_main(void) {
    // Init NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    MnConfig cfg;
    cfg.load_or_init();      // initialize defaults if needed

    MnWiFi wifi(cfg);
    wifi.begin();            // AP fallback + STA test + staged commit

    MnTime time;             // SNTP + TZ
    time.begin();

    examples_init(); 

    MnOta  ota;              // OTA handler
    MnWeb  web(cfg, wifi, time, ota);
    web.begin();

    ESP_LOGI(TAG, "System ready.");
    while (true) {
        wifi.maintain();
        time.maintain();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
