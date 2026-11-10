idf_component_register(
    SRCS "bsp_exio.c"
    INCLUDE_DIRS "."
    REQUIRES bsp_common i2c_bsp espressif__esp_io_expander_tca9554)
