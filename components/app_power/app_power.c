/* app_power — 电源管理状态机：供电档位/电量/息屏/亮度/小夜灯（§11a/§11b）（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_power.h"

static const char *TAG = "app_power";

esp_err_t app_power_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
