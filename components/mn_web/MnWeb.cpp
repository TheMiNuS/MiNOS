// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#include "MnWeb.hpp"
#include "esp_log.h"
#include "esp_http_server.h"
#if CONFIG_MINOS_WEB_USE_HTTPS
  #include "esp_https_server.h"
#endif
#include "mbedtls/base64.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <string>
#include <cstring>
#include <ctime>
#ifdef CONFIG_MINOS_SYSINFO_ENABLE
#include "MnSysInfo.h"
#endif

#include "htmlCode.h"  // HTML/CSS templates
#include "examples_demo.hpp"

// ---------------------------------------------------------------------------
// Variable substitution for HTML templates
// ---------------------------------------------------------------------------
static std::string subst(const MnConfig& cfg, const std::string& var) {
    if (var=="COPYRIGHT")      return "<footer><p>&copy; TheMiNuS</p></footer>";
    if (var=="wifi_ssid")      return cfg.cfg.wifi_ssid;
    if (var=="wifi_password")  return cfg.cfg.wifi_password;
    if (var=="http_login")     return cfg.cfg.http_login;
    if (var=="http_password")  return cfg.cfg.http_password;
    if (var=="hostname")       return cfg.cfg.hostname;
    if (var=="mqtt_login")     return cfg.cfg.mqtt_login;
    if (var=="mqtt_password")  return cfg.cfg.mqtt_password;
    if (var=="mqtt_host")      return cfg.cfg.mqtt_host;
    if (var=="mqtt_port")      { char b[8]; snprintf(b,8,"%u",cfg.cfg.mqtt_port); return b; }
    if (var=="CurrentTime") {
        time_t now = time(nullptr);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M:%S", &tm_local);
        return std::string("") + buf;
    }
    if (var=="CurrentDate") {
        time_t now = time(nullptr);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        char buf[16];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_local);  // format ISO AAAA-MM-JJ
        return std::string("") + buf;
    }
    // Below section related to example_demo
        if (var=="MAC") {
        return examples_mac_str();
    }
    if (var=="IP_ADDR" || var=="NETMASK" || var=="GATEWAY" || var=="DNS") {
        std::string ip, mask, gw, dns;
        if (examples_ip_info(ip, mask, gw, dns)) {
            if (var=="IP_ADDR")  return ip;
            if (var=="NETMASK")  return mask;
            if (var=="GATEWAY")  return gw;
            if (var=="DNS")      return dns;
        }
        return std::string("-");
    }
    if (var=="EX_GPIO_D") {
        return examples_gpio_d_label();
    }
    if (var=="EX_GPIO_A") {
        return examples_gpio_a_label();
    }
    if (var=="GPIO_D_IN") {
        return examples_read_gpio_d() ? "HIGH (1)" : "LOW (0)";
    }
    if (var=="GPIO_A_IN_mV") {
        char b[16]; snprintf(b, sizeof b, "%d", examples_read_adc_mv());
        return b;
    }
#ifdef CONFIG_MINOS_SYSINFO_ENABLE
    if (var=="SYSINFO_BTN") {
        if (mn_sysinfo_is_enabled()) {
            return "<a class='button' href='/sysinfo'>System infos</a>";
        }
        return std::string("");
    }
    if (var=="SYSINFO_BODY") {
        char* body = mn_sysinfo_build_body_html();
        std::string out = body ? body : "";
        free(body);
        return out;
    }
#endif


    return {};
}

static esp_err_t render_with_vars_chunked(httpd_req_t* req, const char* tpl, const MnConfig& cfg) {
    // Send HTML template with %VAR% substitutions without building a full std::string in RAM.
    // Uses chunked transfer: httpd_resp_send_chunk().
    if (!tpl) {
        return httpd_resp_send(req, "", 0);
    }

    esp_err_t err = ESP_OK;

    auto send_chunk = [&](const char* p, size_t n) -> esp_err_t {
        if (n == 0) return ESP_OK;
        return httpd_resp_send_chunk(req, p, n);
    };

    const char* p = tpl;
    const char* seg = tpl;

    // Reuse a small key buffer to reduce heap churn
    std::string key;
    key.reserve(32);

    while (*p) {
        if (*p != '%') { ++p; continue; }

        // Send everything before '%'
        err = send_chunk(seg, (size_t)(p - seg));
        if (err != ESP_OK) return err;

        const char* q = strchr(p + 1, '%');
        if (!q) {
            // No closing '%': send the rest as-is and finish.
            err = send_chunk(p, strlen(p));
            if (err != ESP_OK) return err;
            seg = p + strlen(p);
            break;
        }

        // Extract key between %...%
        key.assign(p + 1, (size_t)(q - (p + 1)));
        std::string val = subst(cfg, key);

        // Send substitution value
        err = send_chunk(val.c_str(), val.size());
        if (err != ESP_OK) return err;

        // Continue after closing '%'
        p = q + 1;
        seg = p;
    }

    // Send remaining tail (if any)
    err = send_chunk(seg, strlen(seg));
    if (err != ESP_OK) return err;

    // Final empty chunk = end of response
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t send_text(httpd_req_t* req, const std::string& body, const char* mime) {
    httpd_resp_set_type(req, mime);
    return httpd_resp_send(req, body.c_str(), body.size());
}

// ---------------------------------------------------------------------------
// Strict Basic Authentication (Base64 user:pass check)
// ---------------------------------------------------------------------------
static bool check_basic_auth(httpd_req_t* req, const MnConfig& cfg) {
    char auth_hdr[256] = {0};

    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hdr_len == 0 || hdr_len >= sizeof(auth_hdr)) {
    unauthorized:
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"MiNOS\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return false;
    }
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) != ESP_OK) {
        goto unauthorized;
    }

    const char* prefix = "Basic ";
    if (strncmp(auth_hdr, prefix, 6) != 0) {
        goto unauthorized;
    }

    const char* b64 = auth_hdr + 6;  // skip "Basic "
    unsigned char decoded[128];
    size_t out_len = 0;
    int ret = mbedtls_base64_decode(
        decoded, sizeof(decoded) - 1, &out_len,
        (const unsigned char*)b64, strlen(b64)
    );
    if (ret != 0 || out_len == 0) {
        goto unauthorized;
    }
    decoded[out_len] = '\0';  // "user:pass"

    char expected[128];
    snprintf(expected, sizeof(expected), "%s:%s", cfg.cfg.http_login, cfg.cfg.http_password);

    if (strcmp((const char*)decoded, expected) == 0) {
        return true;  // ✅ Auth OK
    }

    goto unauthorized;
}

bool MnWeb::check_auth(httpd_req_t* req) const {
    return check_basic_auth(req, m_cfg);
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------
static esp_err_t handle_root(httpd_req_t* req) {
    auto* self = (MnWeb*) req->user_ctx;
    if (!check_basic_auth(req, self->config())) return ESP_OK;

    httpd_resp_set_type(req, "text/html");
    return render_with_vars_chunked(req, HTML_ROOT, self->config());
}


static esp_err_t handle_css(httpd_req_t* req) {
    return send_text(req, HTML_CSS_STYLE, "text/css");
}

static esp_err_t handle_module_cfg(httpd_req_t* req) {
    auto* self = (MnWeb*) req->user_ctx;
    if (!check_basic_auth(req, self->config())) return ESP_OK;

    httpd_resp_set_type(req, "text/html");
    return render_with_vars_chunked(req, HTML_MODULE_CONFIGURATION, self->config());
}

// ---------------------------------------------------------------------------
// Wi-Fi configuration form handler (GET/POST)
// ---------------------------------------------------------------------------
static esp_err_t handle_query_wifi(httpd_req_t* req) {
    auto* self = (MnWeb*) req->user_ctx;
    if (!check_basic_auth(req, self->config())) return ESP_OK;

    // --- Log meta info
    char ctype[64] = {0};
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    ESP_LOGI("WEB", "/wifi method=%s content_len=%d content_type='%s'",
             (req->method==HTTP_POST?"POST":"GET"), req->content_len, ctype[0]?ctype:"(none)");

    // --- Retrieve GET or POST parameters (x-www-form-urlencoded)
    std::string kv; kv.reserve(512);
    if (req->method == HTTP_GET) {
        int len = httpd_req_get_url_query_len(req) + 1;
        if (len > 1) {
            kv.resize(len);
            if (httpd_req_get_url_query_str(req, kv.data(), len) != ESP_OK) kv.clear();
        }
    } else if (req->method == HTTP_POST) {
        int to_read = req->content_len;
        if (to_read < 0) to_read = 0;
        if (to_read > 4096) to_read = 4096;
        kv.resize(to_read);
        int received = 0;
        while (received < to_read) {
            int r = httpd_req_recv(req, kv.data() + received, to_read - received);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (r <= 0) break;
            received += r;
        }
        kv.resize(received);
        kv.push_back('\0');
    }
    for (char &ch : kv) if (ch == '+') ch = ' ';

    auto get_kv = [&](const char* key, char* dst, size_t dstsz) -> bool {
        if (kv.empty()) return false;
        return httpd_query_key_value(kv.c_str(), key, dst, dstsz) == ESP_OK;
    };

    // --- Backup old Wi-Fi configuration (for rollback if connection fails)
    strncpy(self->config().cfg.old_wifi_ssid,     self->config().cfg.wifi_ssid,     sizeof(self->config().cfg.old_wifi_ssid)-1);
    strncpy(self->config().cfg.old_wifi_password, self->config().cfg.wifi_password, sizeof(self->config().cfg.old_wifi_password)-1);

    char val[128];
    bool wifi_changed = false;

    // SSID and password (support CamelCase form fields: wifiSSID/wifiPassword)
    bool ssid_set = get_kv("wifiSSID",  val, sizeof(val));
    if (ssid_set) {
        strncpy(self->config().cfg.wifi_ssid, val, sizeof(self->config().cfg.wifi_ssid)-1);
        self->config().cfg.wifi_ssid[sizeof(self->config().cfg.wifi_ssid)-1] = 0;
        wifi_changed = true;
    }

    bool pass_set = get_kv("wifiPassword",  val, sizeof(val));
    if (pass_set) {
        strncpy(self->config().cfg.wifi_password, val, sizeof(self->config().cfg.wifi_password)-1);
        self->config().cfg.wifi_password[sizeof(self->config().cfg.wifi_password)-1] = 0;
        wifi_changed = true;
    }

    // --- Web Interface Configuration form fields ---
    if (get_kv("httpLogin", val, sizeof(val))) {
        strncpy(self->config().cfg.http_login, val, sizeof(self->config().cfg.http_login)-1);
        self->config().cfg.http_login[sizeof(self->config().cfg.http_login)-1] = 0;
    }
    if (get_kv("httpPassword", val, sizeof(val))) {
        strncpy(self->config().cfg.http_password, val, sizeof(self->config().cfg.http_password)-1);
        self->config().cfg.http_password[sizeof(self->config().cfg.http_password)-1] = 0;
    }
    if (get_kv("hostname", val, sizeof(val))) {
        strncpy(self->config().cfg.hostname, val, sizeof(self->config().cfg.hostname)-1);
        self->config().cfg.hostname[sizeof(self->config().cfg.hostname)-1] = 0;
    }
    if (get_kv("Sensitivity", val, sizeof(val))) {
        int s = atoi(val);
        if (s < 0) s = 0;
        if (s > 255) s = 255;
        self->config().cfg.Sensitivity = (uint8_t)s;
    }

    ESP_LOGI("WEB", "parsed ssid='%s' pwd_len=%d httpLogin='%s' host='%s' Sens=%u  (raw_first_200='%.*s')",
             self->config().cfg.wifi_ssid,
             (int)strlen(self->config().cfg.wifi_password),
             self->config().cfg.http_login,
             self->config().cfg.hostname,
             (unsigned)self->config().cfg.Sensitivity,
             (int)std::min<size_t>(200, kv.size()), kv.c_str());

    // --- Save configuration and apply ---
    if (wifi_changed) {
        // Staged config update and Wi-Fi reconnection test
        self->config().cfg.WifiConfig = 0xAAAA;
        self->config().save();

        httpd_resp_set_type(req, "text/html");
        render_with_vars_chunked(req, HTML_PUSH_CONFIGURATION_TO_MODULE, self->config());

        self->wifi().apply_new_cfg_and_test();  // reboot based on success/failure
    } else {
        // No Wi-Fi change: save only, no reboot
        self->config().save();

        httpd_resp_set_type(req, "text/html");
        render_with_vars_chunked(req, HTML_PUSH_CONFIGURATION_TO_MODULE, self->config());
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Factory reset handler
// ---------------------------------------------------------------------------
static esp_err_t handle_factory_reset(httpd_req_t* req) {
    auto* self = (MnWeb*) req->user_ctx;
    if (!check_basic_auth(req, self->config())) return ESP_OK;

    // Remet la configuration par défaut, sauvegarde et redémarre
    uint8_t mac[6];
    esp_err_t mac_ok = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (mac_ok != ESP_OK) {
    // fallback: lit le MAC par défaut depuis les eFuses
    esp_efuse_mac_get_default(mac);
    }
    char mac_str[18]; snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    self->config().reset_defaults(mac_str);
    self->config().save();

    send_text(req, "Factory reset OK. Rebooting...", "text/plain");
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Reboot handler
// ---------------------------------------------------------------------------
static esp_err_t handle_reboot(httpd_req_t* req) {
    send_text(req, "OK!", "text/html");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Handler de la page Examples
// ---------------------------------------------------------------------------
static esp_err_t handle_example(httpd_req_t* req) {
    auto* self = (MnWeb*) req->user_ctx;
    if (!check_basic_auth(req, self->config())) return ESP_OK;

    httpd_resp_set_type(req, "text/html");
    return render_with_vars_chunked(req, HTML_EXEMPLE, self->config());
}

// ---------------------------------------------------------------------------
// Handler de la page Sysinfo
// ---------------------------------------------------------------------------
#ifdef CONFIG_MINOS_SYSINFO_ENABLE

static esp_err_t sysinfo_httpd_writer(void* ctx, const char* data, size_t len) {
    httpd_req_t* req = (httpd_req_t*)ctx;
    return httpd_resp_send_chunk(req, data, len);
}

static esp_err_t handle_sysinfo(httpd_req_t* req) {
    auto* self = (MnWeb*) req->user_ctx;
    if (!check_basic_auth(req, self->config())) return ESP_OK;

    httpd_resp_set_type(req, "text/html");

    // Minimal page skeleton (no big template)
    static const char HEAD[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>System infos</title>"
        "<link rel='stylesheet' href='styles.css'>"
        "</head><body>"
        "<h1>System infos</h1>"
        "<div>";

    static const char TAIL[] =
        "</div>"
        "<p><a class='button' href='/'>Back</a></p>"
        "</body></html>";

    esp_err_t err = httpd_resp_send_chunk(req, HEAD, strlen(HEAD));
    if (err != ESP_OK) return err;

    // Stream the body chunks
    err = mn_sysinfo_stream_body_html(req, sysinfo_httpd_writer);
    if (err != ESP_OK) {
        // best effort: send an error note
        const char* msg = "<p>sysinfo streaming failed.</p>";
        httpd_resp_send_chunk(req, msg, strlen(msg));
    }

    err = httpd_resp_send_chunk(req, TAIL, strlen(TAIL));
    if (err != ESP_OK) return err;

    // End chunked response
    return httpd_resp_send_chunk(req, nullptr, 0);
}

#endif


MnWeb::MnWeb(MnConfig& c, MnWiFi& w, MnTime& t, MnOta& o) : m_cfg(c), m_wifi(w), m_time(t), m_ota(o) {}

// ---------------------------------------------------------------------------
// HTTPS server initialization
// ---------------------------------------------------------------------------
#if CONFIG_MINOS_WEB_USE_HTTPS
extern const unsigned char server_crt_start[] asm("_binary_server_crt_start");
extern const unsigned char server_crt_end[]   asm("_binary_server_crt_end");
extern const unsigned char server_key_start[] asm("_binary_server_key_start");
extern const unsigned char server_key_end[]   asm("_binary_server_key_end");
#endif

esp_err_t MnWeb::begin() {

    esp_err_t ret = ESP_FAIL;

#if CONFIG_MINOS_WEB_USE_HTTPS
    // HTTPS Server (port 443)
    httpd_ssl_config_t ssl = HTTPD_SSL_CONFIG_DEFAULT();
    ssl.httpd = HTTPD_DEFAULT_CONFIG();
    ssl.httpd.uri_match_fn      = httpd_uri_match_wildcard;
    ssl.httpd.server_port       = 443;
    ssl.httpd.max_open_sockets  = 4;
    ssl.httpd.lru_purge_enable  = true;
    ssl.httpd.max_uri_handlers  = 16;

    ssl.httpd.recv_wait_timeout = 20;
    ssl.httpd.send_wait_timeout = 20;

    ssl.servercert     = server_crt_start;
    ssl.servercert_len = server_crt_end - server_crt_start;
    ssl.prvtkey_pem    = server_key_start;
    ssl.prvtkey_len    = server_key_end - server_key_start;

    ret = httpd_ssl_start(&server_, &ssl);
    if (ret != ESP_OK) {
        ESP_LOGE("WEB", "HTTPS start failed (%d).", (int)ret);
        return ESP_OK; // tu gardes ton comportement actuel
    }
#else
    // HTTP Server (port 80)
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn      = httpd_uri_match_wildcard;
    cfg.server_port       = 80;
    cfg.max_open_sockets  = 4;
    cfg.lru_purge_enable  = true;
    cfg.max_uri_handlers  = 16;

    cfg.recv_wait_timeout = 20;
    cfg.send_wait_timeout = 20;

    ret = httpd_start(&server_, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE("WEB", "HTTP start failed (%d).", (int)ret);
        return ESP_OK; // tu gardes ton comportement actuel
    }
#endif

    // Server started successfully → register handlers (une seule fois)
    register_handlers_();

    return ESP_OK;
}

void MnWeb::register_handlers_() {
    httpd_uri_t root  = { .uri="/", .method=HTTP_GET,  .handler=handle_root,  .user_ctx=this };
    httpd_uri_t css   = { .uri="/styles.css", .method=HTTP_GET, .handler=handle_css, .user_ctx=this };
    httpd_uri_t mod   = { .uri="/module-configuration", .method=HTTP_GET, .handler=handle_module_cfg, .user_ctx=this };
    httpd_uri_t wifiu_get  = { .uri="/wifi", .method=HTTP_GET,  .handler=handle_query_wifi, .user_ctx=this };
    httpd_uri_t wifiu_post = { .uri="/wifi", .method=HTTP_POST, .handler=handle_query_wifi, .user_ctx=this };
    httpd_uri_t reb   = { .uri="/reboot", .method=HTTP_GET, .handler=handle_reboot, .user_ctx=this };
    httpd_uri_t up    = { .uri="/doUpdate", .method=HTTP_POST, .handler=MnOta::handle_upload, .user_ctx=this };
    httpd_uri_t frest = { .uri="/factory-reset", .method=HTTP_POST, .handler=handle_factory_reset, .user_ctx=this };
    httpd_uri_t ex    = { .uri="/example", .method=HTTP_GET, .handler=handle_example, .user_ctx=this };

#ifdef CONFIG_MINOS_SYSINFO_ENABLE
    httpd_uri_t sysinfo = { .uri="/sysinfo", .method=HTTP_GET, .handler=handle_sysinfo, .user_ctx=this };
#endif

    httpd_register_uri_handler(server_, &root);
    httpd_register_uri_handler(server_, &css);
    httpd_register_uri_handler(server_, &mod);
    httpd_register_uri_handler(server_, &wifiu_get);
    httpd_register_uri_handler(server_, &wifiu_post);
    httpd_register_uri_handler(server_, &reb);
    httpd_register_uri_handler(server_, &up);
    httpd_register_uri_handler(server_, &frest);
    httpd_register_uri_handler(server_, &ex);

#ifdef CONFIG_MINOS_SYSINFO_ENABLE
    httpd_register_uri_handler(server_, &sysinfo);
#endif
}
