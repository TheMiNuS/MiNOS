set(MN_SYSINFO_SRCS "")
if(CONFIG_MINOS_SYSINFO_ENABLE)
    set(MN_SYSINFO_SRCS "MnSysInfo.c")
endif()

idf_component_register(
    SRCS ${MN_SYSINFO_SRCS}
    INCLUDE_DIRS "."
    REQUIRES freertos esp_timer esp_system heap
)
