/* app_clock — 时间源管理：SNTP↔RTC 同步、当前时间查询（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_clock.h"

static const char *TAG = "app_clock";

esp_err_t app_clock_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
