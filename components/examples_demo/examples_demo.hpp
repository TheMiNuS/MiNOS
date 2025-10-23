// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#pragma once
#include <string>
#include "driver/gpio.h"

// ==============================
// Default demo pins (ESP32-WROOM-32E)
// ==============================
// Digital demo input (pull-up)
#ifndef EX_GPIO_D
#define EX_GPIO_D GPIO_NUM_4
#endif

// ADC oneshot (ESP32 classique) : GPIO34 = ADC1_CHANNEL_6
#ifndef EX_ADC_UNIT
#define EX_ADC_UNIT ADC_UNIT_1
#endif
#ifndef EX_ADC_CH
#define EX_ADC_CH ADC_CHANNEL_6
#endif
#ifndef EX_GPIO_A_STR
#define EX_GPIO_A_STR "34"   // affichage friendly dans le HTML
#endif

// Init unique (GPIO + ADC)
void examples_init();

// Lectures
int   examples_read_gpio_d();  // 0/1
int   examples_read_adc_mv();  // mV (approx, sans calibration eFuse)

// Infos réseau
std::string examples_mac_str();
bool examples_ip_info(std::string& ip, std::string& mask, std::string& gw, std::string& dns);

// Libellés pour le template HTML
inline std::string examples_gpio_a_label() { return std::string(EX_GPIO_A_STR); }
inline std::string examples_gpio_d_label() { return std::to_string(static_cast<int>(EX_GPIO_D)); }
