// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

class MnOta {
public:
    static esp_err_t handle_upload(httpd_req_t* req);
};
