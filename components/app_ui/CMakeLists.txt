idf_component_register(
    SRCS "app_ui.c" "app_ui_nav.c" "ui_home_cards.c" "ui_page_alarms.c" "ui_page_hourglass.c" "boot_logo.c"
         "fonts/font_noto_sc_16.c" "fonts/font_noto_sc_24.c" "fonts/font_noto_num_64.c"
    INCLUDE_DIRS "."
    REQUIRES bsp_lcd app_event app_net app_power app_clock app_alarm app_timer lvgl)

# lv_font_conv 生成文件用 #include "lvgl.h"（组件式 include,非 lvgl/lvgl.h 路径）
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
