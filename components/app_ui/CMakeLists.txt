idf_component_register(
    SRCS "app_ui.c" "app_ui_nav.c" "boot_logo.c"
    INCLUDE_DIRS "."
    REQUIRES bsp_lcd app_event app_power app_clock app_alarm lvgl)
