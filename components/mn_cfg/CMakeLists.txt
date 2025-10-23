# This file is part of MiNOS (MiNuS OS).
# Copyright (c) 2025 TheMiNuS
# Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
# See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
#
idf_component_register(
  SRCS "MnConfig.cpp"
  INCLUDE_DIRS "."
  REQUIRES nvs_flash esp_system esp_wifi
)
