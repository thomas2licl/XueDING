idf_component_register(
    SRCS "bsp_power.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc bsp_exio bsp_common)
