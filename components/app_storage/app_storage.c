/* app_storage — NVS 封装：设置/闹钟/提醒/地点/天气缓存（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_storage.h"

static const char *TAG = "app_storage";

esp_err_t app_storage_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
