# MiNOS – `examples_demo` Component

This component demonstrates how **MiNOS (MiNuS OS)** exposes hardware and network state to the embedded web interface using **template substitution**.  
It’s a minimal and didactic example showing how to extend MiNOS with modular components.

---

## 1. Overview

The component `examples_demo` provides:
- MAC address display (STA interface)
- IPv4 configuration (IP / Netmask / Gateway / DNS)
- Digital GPIO input (logic level)
- Analog input (ADC oneshot, approximate mV)

Its purpose is to help developers understand how MiNOS isolates responsibilities:
- Hardware interactions live **outside** the web server (in dedicated components).
- The web layer only renders HTML templates and retrieves values via substitution tokens like `%MAC%` or `%GPIO_D_IN%`.

---

## 2. Implementation Logic

### 2.1 Component structure

```
components/
 └─ examples_demo/
    ├─ examples_demo.hpp      → Public API (init + getters)
    ├─ examples_demo.cpp      → Implementation (GPIO/ADC setup, readings, network info)
    └─ CMakeLists.txt         → Component definition and default pins
```

Default configuration for ESP32‑WROOM‑32E:
```cmake
EX_GPIO_D = GPIO_NUM_4      # Digital input
EX_ADC_UNIT = ADC_UNIT_1
EX_ADC_CH = ADC_CHANNEL_6   # GPIO34
EX_GPIO_A_STR = "34"        # Label used in web page
```

### 2.2 Hardware initialization and reading

All initialization happens once, at boot:
```cpp
#include "examples_demo.hpp"

extern "C" void app_main(void) {
    // Usual MiNOS init: NVS, esp_netif_init(), Wi‑Fi setup...
    examples_init();          // Initialize GPIO and ADC once
    // Then start the MiNOS web server
}
```

`examples_demo.cpp` provides:
- `examples_read_gpio_d()` → returns 0 or 1
- `examples_read_adc_mv()` → returns voltage in mV
- `examples_mac_str()` → returns MAC as string
- `examples_ip_info(...)` → fills IP, mask, gateway, DNS

### 2.3 HTML template and button

In `htmlCode.h`, add this template for `/example`:

```cpp
const char HTML_EXEMPLE[] PROGMEM = R"(
 <!DOCTYPE html>
 <html>
   <head>
     <meta charset='UTF-8'>
     <meta name='viewport' content='width=device-width, initial-scale=1'>
     <meta http-equiv='refresh' content='5;url=/example'>
     <title>Examples</title>
     <link rel="stylesheet" href="styles.css">
   </head>
   <body>
     <h1>Examples</h1>
     <fieldset>
       <legend>Network</legend>
       <p>MAC: %MAC%</p>
       <p>IP: %IP_ADDR%</p>
       <p>Netmask: %NETMASK%</p>
       <p>Gateway: %GATEWAY%</p>
       <p>DNS: %DNS%</p>
     </fieldset>

     <fieldset>
       <legend>GPIOs</legend>
       <p>Digital (GPIO %EX_GPIO_D%): %GPIO_D_IN%</p>
       <p>Analog (GPIO %EX_GPIO_A%): %GPIO_A_IN_mV% mV</p>
     </fieldset>

     <div class='config-button-container'>
       <a class='button' href='/'>Go Back</a>
     </div>
     %COPYRIGHT%
   </body>
 </html>
)";
```

And the main page button in `HTML_ROOT`:

```html
<a class='button' href='/example'>Examples</a>
```

### 2.4 Web server integration (`MnWeb.cpp`)

Add the handler:

```cpp
#include "examples_demo.hpp"

static esp_err_t handle_example(httpd_req_t* req) {
    auto* self = (MnWeb*) req->user_ctx;
    if (!check_basic_auth(req, self->config())) return ESP_OK;

    auto html = render_with_vars(HTML_EXEMPLE, self->config());
    return send_text(req, html, "text/html");
}
```

Register it (inside HTTPS server init sections):

```cpp
httpd_uri_t ex = { .uri="/example", .method=HTTP_GET, .handler=handle_example, .user_ctx=this };
httpd_register_uri_handler(server_, &ex);
```

Add new substitutions in the function handling `%...%` replacements:

```cpp
if (var=="MAC") return examples_mac_str();

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

if (var=="EX_GPIO_D") return examples_gpio_d_label();
if (var=="EX_GPIO_A") return examples_gpio_a_label();
if (var=="GPIO_D_IN") return examples_read_gpio_d() ? "HIGH (1)" : "LOW (0)";

if (var=="GPIO_A_IN_mV") {
    char b[16]; snprintf(b, sizeof b, "%d", examples_read_adc_mv());
    return b;
}
```

This ensures every placeholder in the HTML page is dynamically replaced with live values.

---

## 3. Build and flash

```bash
idf.py fullclean reconfigure build flash monitor
```

On boot, MiNOS will expose the new page `/example`, accessible from the main menu.  
Values update automatically every 5 seconds (meta‑refresh).

---

## 4. Key principles

- **Full ESP‑IDF** (no Arduino framework)
- **Hardware abstraction**: logic isolated in `examples_demo`
- **Web decoupling**: MnWeb only renders and routes
- **Dynamic HTML** through variable substitution
- **Extensible**: add any new page by repeating the same pattern

---

## 5. Troubleshooting

| Issue | Cause / Fix |
|-------|--------------|
| Empty network info | STA not connected → falls back to AP info or shows “-” |
| GPIO always LOW | Pin not wired or configured as input; check pull‑up |
| ADC values wrong | Different voltage divider or attenuation; adjust code or calibrate |
| IntelliSense errors | Run `idf.py reconfigure build` and link VS Code to `build/compile_commands.json` |

---

© 2025 TheMiNuS — MiNOS Project (CC BY‑NC 4.0)
