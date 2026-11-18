/* app_storage — NVS 封装：设置/闹钟/提醒/地点/天气缓存（S2.3 实体化：NVS 初始化） */

#include "esp_log.h"
#include "nvs_flash.h"

#include "app_storage.h"

static const char *TAG = "app_storage";

esp_err_t app_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs partition issue, erasing and retrying: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "nvs ready (namespace \"ll\")");
    return ESP_OK;
}
