/* LocalLife — 应用装配入口
 * S1.1：初始化各板级层并启动 UI（显示逻辑已迁入 components/bsp_lcd 等） */

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"
#include "lv_demos.h"

#include "i2c_bsp.h"
#include "bsp_exio.h"
#include "bsp_lcd.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "LocalLife boot");

    /* 板级初始化（顺序：I2C 总线 → IO 扩展 → 显示） */
    touch_i2c_master_Init();   /* 总线0(外设) + 总线1(触摸) */
    ESP_ERROR_CHECK(bsp_exio_init());
    ESP_ERROR_CHECK(bsp_lcd_init());

    /* UI：S1.1 阶段沿用 widgets demo 验证拆分无损；S1.4 起替换为 app_ui */
    if (bsp_lcd_lock(-1)) {
        lv_demo_widgets();
        bsp_lcd_unlock();
    }

    ESP_LOGI(TAG, "LocalLife running");
}
