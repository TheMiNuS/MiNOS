# This file is part of MiNOS (MiNuS OS).
# Copyright (c) 2025 TheMiNuS
# Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
# See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/

idf_component_register(
    SRCS "MnWeb.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_server
        mn_cfg
        mn_wifi
        mn_time
        mn_ota
        mn_sysinfo
        esp_system
        esp_wifi
        examples_demo
)

# DEBUG 
# message(STATUS "mn_web: CONFIG_MINOS_WEB_USE_HTTPS='${CONFIG_MINOS_WEB_USE_HTTPS}' (file=${CMAKE_CURRENT_LIST_FILE})")


if(DEFINED CONFIG_MINOS_WEB_USE_HTTPS AND (CONFIG_MINOS_WEB_USE_HTTPS STREQUAL "y" OR CONFIG_MINOS_WEB_USE_HTTPS STREQUAL "1"))
    message(STATUS "mn_web: HTTPS enabled -> link esp_https_server + mbedtls + embed certs")

    idf_component_get_property(_https_lib esp_https_server COMPONENT_LIB)
    idf_component_get_property(_mbedtls_lib mbedtls COMPONENT_LIB)

    target_link_libraries(${COMPONENT_LIB} PRIVATE ${_https_lib} ${_mbedtls_lib})

    target_add_binary_data(${COMPONENT_LIB} "certs/server.crt" TEXT)
    target_add_binary_data(${COMPONENT_LIB} "certs/server.key" TEXT)
else()
    message(STATUS "mn_web: HTTPS disabled")
endif()
