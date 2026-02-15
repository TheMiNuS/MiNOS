// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Runtime enable switch (in addition to Kconfig).
 * Default: enabled.
 */
extern bool g_mn_sysinfo_runtime_enable;

/** Returns true only if Kconfig enables the feature AND runtime flag is true. */
bool mn_sysinfo_is_enabled(void);

/** Enable/disable at runtime. */
void mn_sysinfo_set_enabled(bool enable);

/** Enable/disable task during boot */
void mn_sysinfo_init(void);

typedef esp_err_t (*mn_sysinfo_write_cb_t)(void* ctx, const char* data, size_t len);

/**
 * Stream the HTML body for /sysinfo without building the whole page in RAM.
 * The callback will be called multiple times with chunks to send.
 */
esp_err_t mn_sysinfo_stream_body_html(void* ctx, mn_sysinfo_write_cb_t write_cb);


/**
 * Build the HTML body for the /sysinfo page.
 * Returned buffer is heap-allocated and must be freed with free().
 *
 * The generated HTML uses only existing CSS classes already used by MiNOS pages:
 *  - fieldset / legend
 *  - form-group
 *  - pre
 */
char* mn_sysinfo_build_body_html(void);

#ifdef __cplusplus
}
#endif
