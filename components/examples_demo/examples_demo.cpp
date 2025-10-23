// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#include "examples_demo.hpp"
#include <cstring>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc_handle = nullptr;
static bool s_inited = false;

// ------------------------------------------------------------
// Init demo peripherals (digital input and ADC oneshot)
// ------------------------------------------------------------
void examples_init() {
    if (s_inited) return;

    // Digital input with internal pull-up
    gpio_config_t io{};
    io.pin_bit_mask   = (1ULL << static_cast<uint32_t>(EX_GPIO_D));
    io.mode           = GPIO_MODE_INPUT;
    io.pull_up_en     = GPIO_PULLUP_ENABLE;
    io.pull_down_en   = GPIO_PULLDOWN_DISABLE;
    io.intr_type      = GPIO_INTR_DISABLE;
    gpio_config(&io);

    // ADC oneshot configuration
    adc_oneshot_unit_init_cfg_t init_cfg{};
    init_cfg.unit_id = EX_ADC_UNIT;
    if (adc_oneshot_new_unit(&init_cfg, &s_adc_handle) == ESP_OK) {
        adc_oneshot_chan_cfg_t chan_cfg{};
        chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        chan_cfg.atten    = ADC_ATTEN_DB_12;  // ~0..3.3V effective range
        adc_oneshot_config_channel(s_adc_handle, EX_ADC_CH, &chan_cfg);
    }

    s_inited = true;
}

// ------------------------------------------------------------
// Read digital GPIO state (returns 0 or 1)
// ------------------------------------------------------------
int examples_read_gpio_d() {
    if (!s_inited) examples_init();
    return gpio_get_level(static_cast<gpio_num_t>(EX_GPIO_D)) ? 1 : 0;
}

// ------------------------------------------------------------
// Read analog voltage in millivolts (rough estimation)
// For accurate readings, add eFuse-based calibration.
// ------------------------------------------------------------
int examples_read_adc_mv() {
    if (!s_inited) examples_init();
    if (!s_adc_handle) return 0;

    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, EX_ADC_CH, &raw) != ESP_OK) return 0;

    // 12-bit -> 0..4095, approx 0..3300 mV with 12 dB attenuation
    return (raw * 3300) / 4095;
}

// ------------------------------------------------------------
// Read STA MAC address as string "AA:BB:CC:DD:EE:FF"
// ------------------------------------------------------------
std::string examples_mac_str() {
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

// ------------------------------------------------------------
// Helper: convert IPv4 to string
// ------------------------------------------------------------
static void ip4_to_str(const esp_ip4_addr_t* a, char* out, size_t out_len) {
    if (!a) {
        std::strncpy(out, "-", out_len);
        out[out_len - 1] = '\0';
        return;
    }
    esp_ip4addr_ntoa(a, out, out_len);
}

// ------------------------------------------------------------
// Helpers: find netifs by ifkey or by ifkey-prefix (thread-safe)
// ------------------------------------------------------------

// Exact ifkey lookup (e.g., "WIFI_STA_DEF", "WIFI_AP_DEF")
static esp_netif_t* find_netif_by_ifkey(const char* key) {
    if (!key) return nullptr;
    return esp_netif_get_handle_from_ifkey(key);
}

// Thread-safe iteration context
struct find_ifkey_ctx {
    const char*   prefix;
    size_t        plen;
    esp_netif_t*  result;
};

// Callback must return int to match esp_netif_callback_fn
static int find_ifkey_cb(void* arg) {
    auto* ctx = reinterpret_cast<find_ifkey_ctx*>(arg);
    esp_netif_t* it = nullptr;
    while ((it = esp_netif_next_unsafe(it)) != nullptr) {
        const char* key = esp_netif_get_ifkey(it);
        if (key && std::strncmp(key, ctx->prefix, ctx->plen) == 0) {
            ctx->result = it;
            break;
        }
    }
    return 0; // success
}

// First netif whose ifkey starts with given prefix (e.g. "WIFI_STA", "WIFI_AP")
static esp_netif_t* find_first_netif_by_key_prefix(const char* prefix) {
    if (!prefix) return nullptr;
    find_ifkey_ctx ctx{ prefix, std::strlen(prefix), nullptr };
    (void)esp_netif_tcpip_exec(find_ifkey_cb, &ctx);
    return ctx.result;
}

// ------------------------------------------------------------
// Get IP configuration: prioritizes STA, falls back to AP.
// ------------------------------------------------------------
bool examples_ip_info(std::string& ip, std::string& mask, std::string& gw, std::string& dns) {
    esp_netif_t* nif = nullptr;

    // 1) try usual STA keys first
    nif = find_netif_by_ifkey("WIFI_STA_DEF");
    if (!nif) nif = find_netif_by_ifkey("WIFI_STA");

    // 2) fallback: first netif whose key starts with "WIFI_STA"
    if (!nif) nif = find_first_netif_by_key_prefix("WIFI_STA");

    // 3) fallback: AP (useful when running captive portal)
    bool using_ap = false;
    if (!nif) {
        nif = find_netif_by_ifkey("WIFI_AP_DEF");
        if (!nif) nif = find_first_netif_by_key_prefix("WIFI_AP");
        using_ap = (nif != nullptr);
    }

    if (!nif) {
        ip = mask = gw = dns = "-";
        return false;
    }

    // IP info
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(nif, &info) != ESP_OK) {
        ip = mask = gw = dns = "-";
        return false;
    }

    char ipbuf[16], mskbuf[16], gwbuf[16], dnsbuf[16];
    ip4_to_str(&info.ip,      ipbuf,  sizeof ipbuf);
    ip4_to_str(&info.netmask, mskbuf, sizeof mskbuf);
    ip4_to_str(&info.gw,      gwbuf,  sizeof gwbuf);

    // Primary DNS
    esp_netif_dns_info_t dns_info{};
    if (esp_netif_get_dns_info(nif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
        dns_info.ip.type == ESP_IPADDR_TYPE_V4) {
        esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, dnsbuf, sizeof dnsbuf);
    } else {
        std::strncpy(dnsbuf, using_ap ? "192.168.4.1" : "0.0.0.0", sizeof dnsbuf);
        dnsbuf[sizeof(dnsbuf) - 1] = '\0';
    }

    ip   = ipbuf;
    mask = mskbuf;
    gw   = gwbuf;
    dns  = dnsbuf;
    return true;
}
