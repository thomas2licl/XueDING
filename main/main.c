/* LocalLife — 应用装配入口（架构 §3：main 只做初始化装配） */

#include <stdio.h>

#include "esp_log.h"

/* BSP 层 */
#include "i2c_bsp.h"
#include "bsp_exio.h"
#include "bsp_lcd.h"

/* 平台服务层 */
#include "app_event.h"
#include "app_storage.h"
#include "app_net.h"
#include "app_power.h"

/* 功能服务层 */
#include "app_clock.h"
#include "app_alarm.h"
#include "app_reminder.h"
#include "app_audio.h"
#include "app_voice.h"
#include "app_weather.h"
#include "app_album.h"
#include "app_scene.h"
#include "app_motion.h"
#include "app_timer.h"
#include "app_ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "LocalLife boot (S1 skeleton)");

    /* 板级层：I2C 总线 → IO 扩展 → 显示（横屏 640×172 + 局部刷新） */
    touch_i2c_master_Init();
    ESP_ERROR_CHECK(bsp_exio_init());
    ESP_ERROR_CHECK(bsp_lcd_init());

    /* 平台服务层 */
    ESP_ERROR_CHECK(app_event_init());
    ESP_ERROR_CHECK(app_storage_init());
    ESP_ERROR_CHECK(app_net_init());
    ESP_ERROR_CHECK(app_power_init());

    /* 功能服务层（S1 均为空壳） */
    ESP_ERROR_CHECK(app_clock_init());
    ESP_ERROR_CHECK(app_alarm_init());
    ESP_ERROR_CHECK(app_reminder_init());
    ESP_ERROR_CHECK(app_audio_init());
    ESP_ERROR_CHECK(app_voice_init());
    ESP_ERROR_CHECK(app_weather_init());
    ESP_ERROR_CHECK(app_album_init());
    ESP_ERROR_CHECK(app_scene_init());
    ESP_ERROR_CHECK(app_motion_init());
    ESP_ERROR_CHECK(app_timer_init());

    /* UI（持锁创建验证页） */
    if (bsp_lcd_lock(-1)) {
        app_ui_init();
        bsp_lcd_unlock();
    }

    ESP_LOGI(TAG, "LocalLife S1 skeleton running");
}
