idf_component_register(
    SRCS "bsp_lcd.c"
    INCLUDE_DIRS "."
    REQUIRES bsp_common bsp_exio i2c_bsp lcd_bl_pwm_bsp lvgl espressif__esp_lcd_axs15231b esp_timer)
