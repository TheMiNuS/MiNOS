# This file is part of MiNOS (MiNuS OS).
# Copyright (c) 2025 TheMiNuS
# Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
# See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
#
idf_component_register(SRCS "MnOta.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES freertos esp_system esp_event esp_netif nvs_flash esp_http_server app_update mbedtls)