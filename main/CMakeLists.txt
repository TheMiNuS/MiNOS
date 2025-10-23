# This file is part of MiNOS (MiNuS OS).
# Copyright (c) 2025 TheMiNuS
# Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
# See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
#
idf_component_register(
  SRCS "app_main.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    mn_cfg
    mn_wifi
    mn_time
    mn_web
    mn_ota
    nvs_flash
    esp_wifi
    esp_netif
    esp_event
    esp_http_server
    esp_https_server
    freertos
    examples_demo
)
