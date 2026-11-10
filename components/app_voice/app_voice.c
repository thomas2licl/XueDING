/* app_voice — ESP-SR（AFE→WakeNet→MultiNet）+ esp-tts 封装（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_voice.h"

static const char *TAG = "app_voice";

esp_err_t app_voice_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
