/* app_reminder — 提醒事项：增删改查、周期性、到点通知（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_reminder.h"

static const char *TAG = "app_reminder";

esp_err_t app_reminder_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
