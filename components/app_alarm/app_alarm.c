/* app_alarm — 闹钟引擎：单次/每日/自定义星期、贪睡（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_alarm.h"

static const char *TAG = "app_alarm";

esp_err_t app_alarm_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
