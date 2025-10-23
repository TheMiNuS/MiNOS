// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#include "MnTime.hpp"
#include "esp_sntp.h"
#include <ctime>

void MnTime::begin() {
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, (char*)"europe.pool.ntp.org");
    esp_sntp_init();
}
void MnTime::maintain() {
    // Optional: check sntp_get_sync_status()
}
