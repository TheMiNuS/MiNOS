// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#pragma once
#include "esp_http_server.h"
#include "MnConfig.hpp"
#include "MnWiFi.hpp"
#include "MnTime.hpp"
#include "MnOta.hpp"

class MnWeb {
public:
    MnWeb(MnConfig& c, MnWiFi& w, MnTime& t, MnOta& o);
    esp_err_t begin();
    inline MnConfig& config(){return m_cfg;}
    inline MnWiFi&  wifi()  {return m_wifi;}
    inline MnTime&  time()  {return m_time;}
    inline MnOta&   ota()   {return m_ota;}
private:
    MnConfig& m_cfg; MnWiFi& m_wifi; MnTime& m_time; MnOta& m_ota;
    httpd_handle_t server_{};
};
