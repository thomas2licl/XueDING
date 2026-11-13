idf_component_register(
    SRCS "app_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp_lcd app_event app_power lvgl)
