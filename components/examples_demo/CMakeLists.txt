# This file is part of MiNOS (MiNuS OS).
# Copyright (c) 2025 TheMiNuS
# Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
# See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
#
idf_component_register(
    SRCS "examples_demo.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif driver esp_adc esp_driver_gpio
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
     EX_GPIO_D=GPIO_NUM_4
     EX_ADC_UNIT=ADC_UNIT_1
     EX_ADC_CH=ADC_CHANNEL_6
     EX_GPIO_A_STR="34"
)
