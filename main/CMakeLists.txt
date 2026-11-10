idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        bsp_common bsp_exio bsp_lcd i2c_bsp
        app_event app_storage app_net app_power
        app_clock app_alarm app_reminder app_audio app_voice
        app_weather app_album app_scene app_motion app_timer app_ui)
