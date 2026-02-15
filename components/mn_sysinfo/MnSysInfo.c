// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#include "MnSysInfo.h"

#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#if __has_include("esp_intr_alloc.h")
#include "esp_intr_alloc.h"
#endif

bool g_mn_sysinfo_runtime_enable = true;

bool mn_sysinfo_is_enabled(void) {
#ifdef CONFIG_MINOS_SYSINFO_ENABLE
    return g_mn_sysinfo_runtime_enable;
#else
    return false;
#endif
}

void mn_sysinfo_set_enabled(bool enable) {
    g_mn_sysinfo_runtime_enable = enable;
}

/* -------------------------- small growable string -------------------------- */

typedef struct {
    char*  buf;
    size_t len;
    size_t cap;
} mn_strbuf_t;

static bool sb_reserve(mn_strbuf_t* sb, size_t extra) {
    if (!sb) return false;
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return true;

    size_t ncap = (sb->cap == 0) ? 512 : sb->cap;
    while (ncap < need) ncap *= 2;

    char* nbuf = (char*)realloc(sb->buf, ncap);
    if (!nbuf) return false;
    sb->buf = nbuf;
    sb->cap = ncap;
    return true;
}

static bool sb_append(mn_strbuf_t* sb, const char* s) {
    if (!s) return true;
    size_t sl = strlen(s);
    if (!sb_reserve(sb, sl)) return false;
    memcpy(sb->buf + sb->len, s, sl);
    sb->len += sl;
    sb->buf[sb->len] = 0;
    return true;
}

static bool sb_appendf(mn_strbuf_t* sb, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n < 0) { va_end(ap2); return false; }
    if (!sb_reserve(sb, (size_t)n)) { va_end(ap2); return false; }
    vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap2);
    va_end(ap2);

    sb->len += (size_t)n;
    return true;
}

/* ----------------------------- helpers / stats ---------------------------- */

static void chip_info_to_str(char* out, size_t out_len) {
    if (!out || out_len == 0) return;

    esp_chip_info_t info;
    esp_chip_info(&info);

    const char* model = "Unknown";
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    switch (info.model) {
        case CHIP_ESP32:      model = "ESP32"; break;
#if defined(CHIP_ESP32S2)
        case CHIP_ESP32S2:    model = "ESP32-S2"; break;
#endif
#if defined(CHIP_ESP32S3)
        case CHIP_ESP32S3:    model = "ESP32-S3"; break;
#endif
#if defined(CHIP_ESP32C2)
        case CHIP_ESP32C2:    model = "ESP32-C2"; break;
#endif
#if defined(CHIP_ESP32C3)
        case CHIP_ESP32C3:    model = "ESP32-C3"; break;
#endif
#if defined(CHIP_ESP32C6)
        case CHIP_ESP32C6:    model = "ESP32-C6"; break;
#endif
#if defined(CHIP_ESP32H2)
        case CHIP_ESP32H2:    model = "ESP32-H2"; break;
#endif
        default: break;
    }
#endif

    char feats[128] = {0};
    size_t p = 0;
    if (info.features & CHIP_FEATURE_WIFI_BGN) p += snprintf(feats + p, sizeof(feats) - p, "WiFi ");
    if (info.features & CHIP_FEATURE_BT)       p += snprintf(feats + p, sizeof(feats) - p, "BT ");
    if (info.features & CHIP_FEATURE_BLE)      p += snprintf(feats + p, sizeof(feats) - p, "BLE ");
    if (info.features & CHIP_FEATURE_EMB_FLASH)p += snprintf(feats + p, sizeof(feats) - p, "EmbFlash ");
    if (info.features & CHIP_FEATURE_EMB_PSRAM)p += snprintf(feats + p, sizeof(feats) - p, "EmbPSRAM ");

    snprintf(out, out_len,
             "Model: %s\nRevision: %d\nCores: %d\nFeatures: %s",
             model, info.revision, info.cores, feats[0] ? feats : "-");
}

/* --------------- Keep (not shown in HTML anymore): raw runtime stats -------- */
static __attribute__((unused)) char* make_runtime_stats(void) {
#if (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS)
    UBaseType_t n = uxTaskGetNumberOfTasks();
    size_t sz = (size_t)n * 128 + 256;
    char* buf = (char*)malloc(sz);
    if (!buf) return NULL;
    memset(buf, 0, sz);
    vTaskGetRunTimeStats(buf);
    return buf;
#else
    return NULL;
#endif
}

/* ------------------- CPU monitoring with EMA “load average” ---------------- */

#if (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)

typedef struct {
    TaskHandle_t handle;
    char         name[configMAX_TASK_NAME_LEN];
    uint32_t     last_rt;

    float inst;   // instantaneous % (last sample window)
    float avg5s;  // EMA 5s
    float avg1m;  // EMA 60s
    float avg5m;  // EMA 300s

    UBaseType_t prio;
    uint16_t    stack_hwm;

    UBaseType_t affinity_mask; // if available
} mn_task_cpu_t;

typedef struct {
    bool     ready;

    uint32_t last_total;
    uint64_t last_us;

    // global
    float global_inst;
    float global_avg5s;
    float global_avg1m;
    float global_avg5m;

    // per-core (approx using IDLE deltas)
    float core0_inst;
    float core1_inst;

    // tasks
    mn_task_cpu_t* tasks;
    size_t         tasks_len;
    size_t         tasks_cap;
} mn_cpu_mon_t;

static mn_cpu_mon_t       s_mon = {0};
static SemaphoreHandle_t  s_mon_mtx = NULL;
static TaskHandle_t       s_mon_task = NULL;

static inline uint32_t u32_delta(uint32_t now, uint32_t prev) {
    return (uint32_t)(now - prev); // modulo 2^32, wrap-safe
}

static inline float ema_update(float old, float x, float dt_s, float tau_s) {
    // EMA equivalent to Linux load average style smoothing
    // alpha = exp(-dt/tau), new = old*alpha + x*(1-alpha)
    double a = exp(-(double)dt_s / (double)tau_s);
    return (float)(old * a + x * (1.0 - a));
}

static mn_task_cpu_t* mon_find_or_add(TaskHandle_t h, const char* name) {
    for (size_t i = 0; i < s_mon.tasks_len; ++i) {
        if (s_mon.tasks[i].handle == h) return &s_mon.tasks[i];
    }

    if (s_mon.tasks_len >= s_mon.tasks_cap) {
        size_t ncap = (s_mon.tasks_cap == 0) ? 16 : (s_mon.tasks_cap * 2);
        mn_task_cpu_t* nt = (mn_task_cpu_t*)realloc(s_mon.tasks, ncap * sizeof(mn_task_cpu_t));
        if (!nt) return NULL;
        s_mon.tasks = nt;
        s_mon.tasks_cap = ncap;
    }

    mn_task_cpu_t* e = &s_mon.tasks[s_mon.tasks_len++];
    memset(e, 0, sizeof(*e));
    e->handle = h;
    strncpy(e->name, name ? name : "?", sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = 0;
    return e;
}

static void mn_cpu_monitor_task(void* arg) {
    (void)arg;

    const TickType_t period = pdMS_TO_TICKS(1000);
    TickType_t last_wake = xTaskGetTickCount();

    static TaskStatus_t* st = NULL;
    static UBaseType_t   st_cap = 0;

    for (;;) {
        vTaskDelayUntil(&last_wake, period);

        // runtime disable: keep task alive but skip heavy work
        if (!mn_sysinfo_is_enabled()) {
            continue;
        }

        UBaseType_t n = uxTaskGetNumberOfTasks();

        // Ensure capacity with some headroom to avoid frequent realloc
        if (n > st_cap) {
            UBaseType_t new_cap = (st_cap == 0) ? (n + 8) : st_cap;
            while (new_cap < n) new_cap = (UBaseType_t)(new_cap * 2);

            TaskStatus_t* ns = (TaskStatus_t*)realloc(st, sizeof(TaskStatus_t) * new_cap);
            if (!ns) {
                // if we can’t allocate, skip this round (don’t destroy old buffer)
                continue;
            }
            st = ns;
            st_cap = new_cap;
        }

        uint32_t total_now = 0;
        UBaseType_t got = uxTaskGetSystemState(st, n, &total_now);
        uint64_t now_us = (uint64_t)esp_timer_get_time();
        if (got == 0) {
            continue;
        }

        if (!s_mon_mtx) {
            s_mon_mtx = xSemaphoreCreateMutex();
            if (!s_mon_mtx) {
                continue;
            }
        }

        xSemaphoreTake(s_mon_mtx, portMAX_DELAY);

        float dt_s = 1.0f;
        if (s_mon.last_us != 0 && now_us > s_mon.last_us) {
            dt_s = (float)((double)(now_us - s_mon.last_us) / 1e6);
            if (dt_s < 0.2f) dt_s = 0.2f;
            if (dt_s > 5.0f) dt_s = 5.0f;
        }

        if (s_mon.last_total == 0) {
            s_mon.last_total = total_now;
            s_mon.last_us = now_us;

            for (UBaseType_t i = 0; i < got; ++i) {
                mn_task_cpu_t* e = mon_find_or_add(st[i].xHandle, st[i].pcTaskName);
                if (e) {
                    e->last_rt = st[i].ulRunTimeCounter;
                }
            }

            s_mon.ready = true;
            xSemaphoreGive(s_mon_mtx);
            continue;
        }

        uint32_t d_total = u32_delta(total_now, s_mon.last_total);
        if (d_total == 0) d_total = 1;

        double denom_global = (double)d_total;
#if (portNUM_PROCESSORS > 1)
        denom_global *= (double)portNUM_PROCESSORS;
#endif
        if (denom_global <= 0.0) denom_global = 1.0;

        uint32_t d_idle0 = 0, d_idle1 = 0;

        for (UBaseType_t i = 0; i < got; ++i) {
            mn_task_cpu_t* e = mon_find_or_add(st[i].xHandle, st[i].pcTaskName);
            if (!e) continue;

            uint32_t d_rt = u32_delta(st[i].ulRunTimeCounter, e->last_rt);
            e->last_rt = st[i].ulRunTimeCounter;

            float inst = (float)(100.0 * (double)d_rt / denom_global);
            if (inst < 0.f) inst = 0.f;
            if (inst > 100.f) inst = 100.f;

            e->inst  = inst;
            e->avg5s = ema_update(e->avg5s, inst, dt_s, 5.0f);
            e->avg1m = ema_update(e->avg1m, inst, dt_s, 60.0f);
            e->avg5m = ema_update(e->avg5m, inst, dt_s, 300.0f);

            e->prio = st[i].uxCurrentPriority;
            e->stack_hwm = st[i].usStackHighWaterMark;

#if (portNUM_PROCESSORS > 1) && defined(configUSE_CORE_AFFINITY) && (configUSE_CORE_AFFINITY == 1)
            e->affinity_mask = vTaskCoreAffinityGet(st[i].xHandle);
#else
            e->affinity_mask = 0;
#endif

            if (strcmp(e->name, "IDLE0") == 0) d_idle0 = d_rt;
            if (strcmp(e->name, "IDLE1") == 0) d_idle1 = d_rt;
        }

        double idle_pct_global = 0.0;
#if (portNUM_PROCESSORS > 1)
        idle_pct_global = 100.0 * ((double)d_idle0 + (double)d_idle1) / denom_global;
#else
        idle_pct_global = 100.0 * (double)d_idle0 / (double)d_total;
#endif
        double load_pct = 100.0 - idle_pct_global;
        if (load_pct < 0.0) load_pct = 0.0;
        if (load_pct > 100.0) load_pct = 100.0;

        s_mon.global_inst  = (float)load_pct;
        s_mon.global_avg5s = ema_update(s_mon.global_avg5s, s_mon.global_inst, dt_s, 5.0f);
        s_mon.global_avg1m = ema_update(s_mon.global_avg1m, s_mon.global_inst, dt_s, 60.0f);
        s_mon.global_avg5m = ema_update(s_mon.global_avg5m, s_mon.global_inst, dt_s, 300.0f);

#if (portNUM_PROCESSORS > 1)
        double c0 = 100.0 - (100.0 * (double)d_idle0 / (double)d_total);
        double c1 = 100.0 - (100.0 * (double)d_idle1 / (double)d_total);
        if (c0 < 0.0) c0 = 0.0;
        if (c0 > 100.0) c0 = 100.0;
        if (c1 < 0.0) c1 = 0.0;
        if (c1 > 100.0) c1 = 100.0;
        s_mon.core0_inst = (float)c0;
        s_mon.core1_inst = (float)c1;
#endif

        s_mon.last_total = total_now;
        s_mon.last_us = now_us;

        xSemaphoreGive(s_mon_mtx);
    }
}


static void mn_cpu_monitor_ensure_started(void) {
    if (s_mon_task) return;

    if (!s_mon_mtx) {
        s_mon_mtx = xSemaphoreCreateMutex();
        if (!s_mon_mtx) return;
    }

    // Keep stack reasonable; no big buffers on stack; allocations happen on heap inside.
    (void)xTaskCreate(mn_cpu_monitor_task, "mn_sysmon", 4096, NULL, 5, &s_mon_task);
}

static bool append_cpu_load(mn_strbuf_t* sb) {
    mn_cpu_monitor_ensure_started();

    if (!s_mon_mtx) return true;

    xSemaphoreTake(s_mon_mtx, portMAX_DELAY);

    if (!s_mon.ready) {
        xSemaphoreGive(s_mon_mtx);
        sb_append(sb, "<fieldset><legend>CPU usage</legend><div class='form-group'>");
        sb_append(sb, "<p>Warming up… refresh in a few seconds.</p>");
        sb_append(sb, "</div></fieldset>");
        return true;
    }

    sb_append(sb, "<fieldset><legend>CPU usage</legend><div class='form-group'><pre>");
    sb_appendf(sb, "Instant : %.1f %%\n", s_mon.global_inst);
    sb_appendf(sb, "Avg 5s  : %.1f %%\n", s_mon.global_avg5s);
    sb_appendf(sb, "Avg 1m  : %.1f %%\n", s_mon.global_avg1m);
    sb_appendf(sb, "Avg 5m  : %.1f %%\n", s_mon.global_avg5m);

#if (portNUM_PROCESSORS > 1)
    sb_appendf(sb, "\nCore 0 (inst): %.1f %%\n", s_mon.core0_inst);
    sb_appendf(sb, "Core 1 (inst): %.1f %%\n", s_mon.core1_inst);
#endif
    sb_append(sb, "</pre></div></fieldset>");

    sb_append(sb, "<fieldset><legend>CPU per task</legend><div class='form-group'>");
    sb_append(sb, "<p>Columns: Name | State (R=Running, Y=Ready, B=Blocked, S=Suspended, D=Deleted) | Prio | Stack(HWM) | Core(allowed) | CPU% (inst/5s/1m/5m)</p>");
    sb_append(sb, "<pre>");
    sb_append(sb, "Task                          State Prio Stack  Core     Inst   5sAvg  1mAvg  5mAvg\n");
    sb_append(sb, "--------------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < s_mon.tasks_len; ++i) {
        mn_task_cpu_t* e = &s_mon.tasks[i];

        // Keep the list readable: tasks that never updated will be 0, but keep them.
        char core_allowed[12] = "Any";
#if (portNUM_PROCESSORS > 1) && defined(configUSE_CORE_AFFINITY) && (configUSE_CORE_AFFINITY == 1)
        UBaseType_t m = e->affinity_mask;
        if (m == 1) strncpy(core_allowed, "0", sizeof(core_allowed));
        else if (m == 2) strncpy(core_allowed, "1", sizeof(core_allowed));
        else if (m == 3) strncpy(core_allowed, "0|1", sizeof(core_allowed));
        else snprintf(core_allowed, sizeof(core_allowed), "0x%X", (unsigned)m);
#endif
        core_allowed[sizeof(core_allowed) - 1] = 0;

        // state is not stored in monitor (would require keeping it); show '-' if unknown.
        // We can’t reliably know the core it is running on without extra internals; show allowed cores.
        sb_appendf(sb, "%-28s  -     %2u  %5u  %-7s %6.1f %6.1f %6.1f %6.1f\n",
                   e->name,
                   (unsigned)e->prio,
                   (unsigned)e->stack_hwm,
                   core_allowed,
                   e->inst, e->avg5s, e->avg1m, e->avg5m);
    }

    sb_append(sb, "</pre></div></fieldset>");

    xSemaphoreGive(s_mon_mtx);
    return true;
}

#else

static bool append_cpu_load(mn_strbuf_t* sb) {
    (void)sb;
    return true;
}

#endif

/* ----------------------------- interrupts dump ---------------------------- */

static bool append_interrupts(mn_strbuf_t* sb) {
    sb_append(sb, "<fieldset><legend>Interrupts</legend><div class='form-group'>");

#if __has_include("esp_intr_alloc.h")
    // Avoid big heap spikes when memory is already fragmented / low
    size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (big < 16 * 1024) {
        sb_appendf(sb,
                   "<p>Skipped (largest free block too small: %u bytes). "
                   "This dump can allocate a lot; try again when RAM is freer.</p>",
                   (unsigned)big);
        sb_append(sb, "</div></fieldset>");
        return true;
    }

    char* mem = NULL;
    size_t mem_sz = 0;
    FILE* f = open_memstream(&mem, &mem_sz);
    if (!f) {
        sb_append(sb, "<p>open_memstream() not available, cannot capture interrupt dump.</p>");
        sb_append(sb, "</div></fieldset>");
        return true;
    }

    esp_err_t err = esp_intr_dump(f);
    fclose(f);

    if (err != ESP_OK) {
        sb_appendf(sb, "<p>esp_intr_dump() failed: %d</p>", (int)err);
    }

    if (mem && mem_sz) {
        // Optional: cap output to keep HTML size reasonable
        const size_t CAP = 8 * 1024;
        if (mem_sz > CAP) {
            mem_sz = CAP;
            sb_append(sb, "<p>(truncated to 8KB)</p>");
        }

        sb_append(sb, "<pre>");
        for (size_t i = 0; i < mem_sz; ++i) {
            char c = mem[i];
            if (c == '<') sb_append(sb, "&lt;");
            else if (c == '>') sb_append(sb, "&gt;");
            else if (c == '&') sb_append(sb, "&amp;");
            else {
                if (!sb_reserve(sb, 1)) break;
                sb->buf[sb->len++] = c;
                sb->buf[sb->len] = 0;
            }
        }
        sb_append(sb, "</pre>");
    } else {
        sb_append(sb, "<p>No interrupt information returned.</p>");
    }

    free(mem);
#else
    sb_append(sb, "<p>Interrupt dump not available (esp_intr_alloc.h not present).</p>");
#endif

    sb_append(sb, "</div></fieldset>");
    return true;
}

/* ----------------------------- vtasklist dump ---------------------------- */

static bool append_vtasklist(mn_strbuf_t* sb) {
#if (CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS)
    static char*  buf = NULL;
    static size_t cap = 0;


    UBaseType_t n = uxTaskGetNumberOfTasks();
    size_t need = (size_t)n * 96 + 256;
    if (need < 1024) need = 1024;


    if (need > cap) {
        size_t ncap = (cap == 0) ? need : cap;
        while (ncap < need) ncap *= 2;


        char* nb = (char*)realloc(buf, ncap);
        if (!nb) return false;
        buf = nb;
        cap = ncap;
    }


    memset(buf, 0, cap);
    vTaskList(buf);


    sb_append(sb, "<fieldset><legend>vTaskList()</legend><div class='form-group'>");
    sb_append(sb, "<p>Columns: Name | State (R=Ready, B=Blocked, S=Suspended, D=Deleted, X/R=Running) | Prio | Stack(HWM) | Num</p>");
    sb_append(sb, "<pre>");


    for (char* p = buf; *p; ++p) {
        if (*p == '<') sb_append(sb, "&lt;");
        else if (*p == '>') sb_append(sb, "&gt;");
        else if (*p == '&') sb_append(sb, "&amp;");
        else {
            if (!sb_reserve(sb, 1)) break;
            sb->buf[sb->len++] = *p;
            sb->buf[sb->len] = 0;
        }
    }


    sb_append(sb, "</pre></div></fieldset>");
    return true;
#else
    (void)sb;
    return true;
#endif
}

/* ------------------------------- main builder ------------------------------ */

char* mn_sysinfo_build_body_html(void) {
    if (!mn_sysinfo_is_enabled()) {
        char* out = (char*)malloc(64);
        if (!out) return NULL;
        strcpy(out, "<p>System infos disabled.</p>");
        return out;
    }

    //mn_strbuf_t sb = {0}; //Can eat a lot of memory.
    mn_strbuf_t sb;
    sb_reserve(&sb, 4096);

    // Chip info
    char chip[256];
    chip_info_to_str(chip, sizeof(chip));

    sb_append(&sb, "<fieldset><legend>Chip</legend><div class='form-group'><pre>");
    sb_append(&sb, chip);
    sb_append(&sb, "</pre></div></fieldset>");

    // Core / uptime / heap + unique device ID
    int core = xPortGetCoreID();
    int64_t us = esp_timer_get_time();

    uint64_t total_sec = (uint64_t)(us / 1000000ULL);
    uint32_t days      = (uint32_t)(total_sec / 86400ULL);
    uint32_t hours     = (uint32_t)((total_sec % 86400ULL) / 3600ULL);
    uint32_t minutes   = (uint32_t)((total_sec % 3600ULL) / 60ULL);
    uint32_t seconds   = (uint32_t)(total_sec % 60ULL);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    size_t heap_free = esp_get_free_heap_size();
    size_t heap_min  = esp_get_minimum_free_heap_size();
    size_t heap_big  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    sb_append(&sb, "<fieldset><legend>System</legend><div class='form-group'><pre>");
    sb_appendf(&sb, "Device ID (MAC): %02X:%02X:%02X:%02X:%02X:%02X\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    sb_appendf(&sb, "Current core: %d\n", core);
    sb_appendf(&sb, "Uptime: %u day%s %02u:%02u:%02u (%" PRId64 " us)\n",
               days, (days > 1) ? "s" : "", hours, minutes, seconds, us);
    sb_appendf(&sb, "Heap free: %u bytes\n", (unsigned)heap_free);
    sb_appendf(&sb, "Heap min free: %u bytes\n", (unsigned)heap_min);
    sb_appendf(&sb, "Largest free block: %u bytes\n", (unsigned)heap_big);
    sb_append(&sb, "</pre></div></fieldset>");

    // FreeRTOS tasks count
    UBaseType_t nt = uxTaskGetNumberOfTasks();
    sb_append(&sb, "<fieldset><legend>FreeRTOS</legend><div class='form-group'>");
    sb_appendf(&sb, "<p>Number of tasks: %u</p>", (unsigned)nt);
    sb_append(&sb, "</div></fieldset>");

    // vTaskList
#if (CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS)
    append_vtasklist(&sb);
#else
    sb_append(&sb, "<fieldset><legend>vTaskList()</legend><div class='form-group'>"
                   "<p>Disabled. Enable CONFIG_FREERTOS_USE_TRACE_FACILITY and "
                   "CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS.</p>"
                   "</div></fieldset>");
#endif

    // NOTE: We intentionally do NOT show raw vTaskGetRunTimeStats() here because it becomes wrong after long uptime.
    // CPU usage + per task (monitor-based with EMA averages)
    append_cpu_load(&sb);

    // Interrupts
    append_interrupts(&sb);

    if (!sb.buf) return NULL;
    return sb.buf;
}

/* ------------------------------ Chunked/streaming HTML builder (no big malloc for the whole page) ------------------------------ */

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
    void* ctx;
    mn_sysinfo_write_cb_t cb;
    esp_err_t last_err;
} mn_streambuf_t;

static bool sbw_flush(mn_streambuf_t* w) {
    if (!w || !w->cb) return false;
    if (w->last_err != ESP_OK) return false;
    if (w->len == 0) return true;

    w->last_err = w->cb(w->ctx, w->buf, w->len);
    w->len = 0;
    return (w->last_err == ESP_OK);
}

static bool sbw_append_n(mn_streambuf_t* w, const char* s, size_t n) {
    if (!w || !s || n == 0) return true;
    if (w->last_err != ESP_OK) return false;

    // If chunk bigger than our buffer, flush current and send directly
    if (n >= w->cap) {
        if (!sbw_flush(w)) return false;
        w->last_err = w->cb(w->ctx, s, n);
        return (w->last_err == ESP_OK);
    }

    if (w->len + n > w->cap) {
        if (!sbw_flush(w)) return false;
    }

    memcpy(w->buf + w->len, s, n);
    w->len += n;
    return true;
}

static bool sbw_append(mn_streambuf_t* w, const char* s) {
    if (!s) return true;
    return sbw_append_n(w, s, strlen(s));
}

static bool sbw_appendf(mn_streambuf_t* w, const char* fmt, ...) {
    if (!w || !fmt) return false;
    if (w->last_err != ESP_OK) return false;

    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0) return false;

    // If truncated, fall back to two-pass into direct send (rare here)
    if ((size_t)n >= sizeof(tmp)) {
        // Two-pass: allocate only for this big format (should be extremely rare)
        char* big = (char*)malloc((size_t)n + 1);
        if (!big) return false;
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        bool ok = sbw_append_n(w, big, (size_t)n);
        free(big);
        return ok;
    }

    return sbw_append_n(w, tmp, (size_t)n);
}

static bool sbw_putc(mn_streambuf_t* w, char c) {
    if (!w) return false;
    if (w->last_err != ESP_OK) return false;
    if (w->len + 1 > w->cap) {
        if (!sbw_flush(w)) return false;
    }
    w->buf[w->len++] = c;
    return true;
}

static bool sbw_append_html_escape_pre(mn_streambuf_t* w, const char* s) {
    if (!w || !s) return true;
    for (const char* p = s; *p; ++p) {
        if (*p == '<') { if (!sbw_append(w, "&lt;")) return false; }
        else if (*p == '>') { if (!sbw_append(w, "&gt;")) return false; }
        else if (*p == '&') { if (!sbw_append(w, "&amp;")) return false; }
        else { if (!sbw_putc(w, *p)) return false; }
    }
    return true;
}

#if (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
static bool stream_cpu_load(mn_streambuf_t* w) {
    mn_cpu_monitor_ensure_started();
    if (!s_mon_mtx) return true;

    xSemaphoreTake(s_mon_mtx, portMAX_DELAY);

    if (!s_mon.ready) {
        xSemaphoreGive(s_mon_mtx);
        sbw_append(w, "<fieldset><legend>CPU usage</legend><div class='form-group'>");
        sbw_append(w, "<p>Warming up… refresh in a few seconds.</p>");
        sbw_append(w, "</div></fieldset>");
        return true;
    }

    sbw_append(w, "<fieldset><legend>CPU usage</legend><div class='form-group'><pre>");
    sbw_appendf(w, "Instant : %.1f %%\n", s_mon.global_inst);
    sbw_appendf(w, "Avg 5s  : %.1f %%\n", s_mon.global_avg5s);
    sbw_appendf(w, "Avg 1m  : %.1f %%\n", s_mon.global_avg1m);
    sbw_appendf(w, "Avg 5m  : %.1f %%\n", s_mon.global_avg5m);

#if (portNUM_PROCESSORS > 1)
    sbw_appendf(w, "\nCore 0 (inst): %.1f %%\n", s_mon.core0_inst);
    sbw_appendf(w, "Core 1 (inst): %.1f %%\n", s_mon.core1_inst);
#endif
    sbw_append(w, "</pre></div></fieldset>");

    sbw_append(w, "<fieldset><legend>CPU per task</legend><div class='form-group'>");
    sbw_append(w, "<p>Columns: Name | Prio | Stack(HWM) | Core(allowed) | CPU% (inst/5s/1m/5m)</p>");
    sbw_append(w, "<pre>");
    sbw_append(w, "Task                          Prio Stack  Core     Inst   5sAvg  1mAvg  5mAvg\n");
    sbw_append(w, "--------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < s_mon.tasks_len; ++i) {
        mn_task_cpu_t* e = &s_mon.tasks[i];

        char core_allowed[12] = "Any";
#if (portNUM_PROCESSORS > 1) && defined(configUSE_CORE_AFFINITY) && (configUSE_CORE_AFFINITY == 1)
        UBaseType_t m = e->affinity_mask;
        if (m == 1) strncpy(core_allowed, "0", sizeof(core_allowed));
        else if (m == 2) strncpy(core_allowed, "1", sizeof(core_allowed));
        else if (m == 3) strncpy(core_allowed, "0|1", sizeof(core_allowed));
        else snprintf(core_allowed, sizeof(core_allowed), "0x%X", (unsigned)m);
#endif
        core_allowed[sizeof(core_allowed) - 1] = 0;

        sbw_appendf(w, "%-28s  %2u  %5u  %-7s %6.1f %6.1f %6.1f %6.1f\n",
                    e->name,
                    (unsigned)e->prio,
                    (unsigned)e->stack_hwm,
                    core_allowed,
                    e->inst, e->avg5s, e->avg1m, e->avg5m);
    }

    sbw_append(w, "</pre></div></fieldset>");

    xSemaphoreGive(s_mon_mtx);
    return true;
}
#else
static bool stream_cpu_load(mn_streambuf_t* w) { (void)w; return true; }
#endif

static bool stream_interrupts(mn_streambuf_t* w) {
    sbw_append(w, "<fieldset><legend>Interrupts</legend><div class='form-group'>");

#if __has_include("esp_intr_alloc.h")
    // Guard: avoid heap spikes if RAM is already fragmented/low
    size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (big < 16 * 1024) {
        sbw_appendf(w,
                    "<p>Skipped (largest free block too small: %u bytes). "
                    "This dump can allocate a lot; try again when RAM is freer.</p>",
                    (unsigned)big);
        sbw_append(w, "</div></fieldset>");
        return true;
    }

    char* mem = NULL;
    size_t mem_sz = 0;
    FILE* f = open_memstream(&mem, &mem_sz);
    if (!f) {
        sbw_append(w, "<p>open_memstream() not available, cannot capture interrupt dump.</p>");
        sbw_append(w, "</div></fieldset>");
        return true;
    }

    esp_err_t err = esp_intr_dump(f);
    fclose(f);

    if (err != ESP_OK) {
        sbw_appendf(w, "<p>esp_intr_dump() failed: %d</p>", (int)err);
    }

    if (mem && mem_sz) {
        // optional cap to prevent huge HTML
        const size_t CAP = 8 * 1024;
        if (mem_sz > CAP) {
            mem_sz = CAP;
            sbw_append(w, "<p>(truncated to 8KB)</p>");
        }

        sbw_append(w, "<pre>");
        // escape as we stream
        for (size_t i = 0; i < mem_sz; ++i) {
            char c = mem[i];
            if (c == '<') sbw_append(w, "&lt;");
            else if (c == '>') sbw_append(w, "&gt;");
            else if (c == '&') sbw_append(w, "&amp;");
            else sbw_putc(w, c);
        }
        sbw_append(w, "</pre>");
    } else {
        sbw_append(w, "<p>No interrupt information returned.</p>");
    }

    free(mem);
#else
    sbw_append(w, "<p>Interrupt dump not available (esp_intr_alloc.h not present).</p>");
#endif

    sbw_append(w, "</div></fieldset>");
    return true;
}

esp_err_t mn_sysinfo_stream_body_html(void* ctx, mn_sysinfo_write_cb_t write_cb) {
    if (!write_cb) return ESP_ERR_INVALID_ARG;

    if (!mn_sysinfo_is_enabled()) {
        write_cb(ctx, "<p>System infos disabled.</p>", strlen("<p>System infos disabled.</p>"));
        return ESP_OK;
    }

    // Small heap buffer (per request), avoids building the full HTML
    const size_t CAP = 2048;
    mn_streambuf_t w = {
        .buf = (char*)heap_caps_malloc(CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .len = 0,
        .cap = CAP,
        .ctx = ctx,
        .cb  = write_cb,
        .last_err = ESP_OK
    };

    if (!w.buf) return ESP_ERR_NO_MEM;

    // Chip info
    char chip[256];
    chip_info_to_str(chip, sizeof(chip));
    sbw_append(&w, "<fieldset><legend>Chip</legend><div class='form-group'><pre>");
    sbw_append_html_escape_pre(&w, chip);
    sbw_append(&w, "</pre></div></fieldset>");

    // System info
    int core = xPortGetCoreID();
    int64_t us = esp_timer_get_time();

    uint64_t total_sec = (uint64_t)(us / 1000000ULL);
    uint32_t days      = (uint32_t)(total_sec / 86400ULL);
    uint32_t hours     = (uint32_t)((total_sec % 86400ULL) / 3600ULL);
    uint32_t minutes   = (uint32_t)((total_sec % 3600ULL) / 60ULL);
    uint32_t seconds   = (uint32_t)(total_sec % 60ULL);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    size_t heap_free = esp_get_free_heap_size();
    size_t heap_min  = esp_get_minimum_free_heap_size();
    size_t heap_big  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    sbw_append(&w, "<fieldset><legend>System</legend><div class='form-group'><pre>");
    sbw_appendf(&w, "Device ID (MAC): %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    sbw_appendf(&w, "Current core: %d\n", core);
    sbw_appendf(&w, "Uptime: %u day%s %02u:%02u:%02u (%" PRId64 " us)\n",
                days, (days > 1) ? "s" : "", hours, minutes, seconds, us);
    sbw_appendf(&w, "Heap free: %u bytes\n", (unsigned)heap_free);
    sbw_appendf(&w, "Heap min free: %u bytes\n", (unsigned)heap_min);
    sbw_appendf(&w, "Largest free block: %u bytes\n", (unsigned)heap_big);
    sbw_append(&w, "</pre></div></fieldset>");

    // FreeRTOS count
    UBaseType_t nt = uxTaskGetNumberOfTasks();
    sbw_append(&w, "<fieldset><legend>FreeRTOS</legend><div class='form-group'>");
    sbw_appendf(&w, "<p>Number of tasks: %u</p>", (unsigned)nt);
    sbw_append(&w, "</div></fieldset>");

    // vTaskList() (uses a persistent buffer to avoid malloc every request)
#if (CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS)
    static char* tbuf = NULL;
    static size_t tcap = 0;

    size_t need = (size_t)nt * 96 + 256;
    if (need < 1024) need = 1024;

    if (need > tcap) {
        size_t ncap = (tcap == 0) ? need : tcap;
        while (ncap < need) ncap *= 2;
        char* nb = (char*)realloc(tbuf, ncap);
        if (nb) { tbuf = nb; tcap = ncap; }
    }

    sbw_append(&w, "<fieldset><legend>vTaskList()</legend><div class='form-group'>");
    sbw_append(&w, "<p>Columns: Name | State (R=Ready, B=Blocked, S=Suspended, D=Deleted, X/R=Running) | Prio | Stack(HWM) | Num</p>");
    sbw_append(&w, "<pre>");

    if (tbuf && tcap) {
        memset(tbuf, 0, tcap);
        vTaskList(tbuf);
        sbw_append_html_escape_pre(&w, tbuf);
    } else {
        sbw_append(&w, "Allocation failed.");
    }
    sbw_append(&w, "</pre></div></fieldset>");
#else
    sbw_append(&w, "<fieldset><legend>vTaskList()</legend><div class='form-group'>"
                   "<p>Disabled. Enable CONFIG_FREERTOS_USE_TRACE_FACILITY and "
                   "CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS.</p>"
                   "</div></fieldset>");
#endif

    // CPU usage (EMA monitor)
    stream_cpu_load(&w);

    // Interrupts
    stream_interrupts(&w);

    // Flush and cleanup
    sbw_flush(&w);
    esp_err_t ret = w.last_err;
    heap_caps_free(w.buf);
    return ret;
}



/* ------------------------------ Sysinfo init function ------------------------------ */

void mn_sysinfo_init(void)
{
#ifdef CONFIG_MINOS_SYSINFO_ENABLE
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    // démarre dès le boot (même si /sysinfo n'est jamais consultée)
    if (!s_mon_task) {
        if (!s_mon_mtx) {
            s_mon_mtx = xSemaphoreCreateMutex();
            if (!s_mon_mtx) return;
        }
        (void)xTaskCreate(mn_cpu_monitor_task, "mn_sysmon", 4096, NULL, 5, &s_mon_task);
    }
#endif
#endif
}

