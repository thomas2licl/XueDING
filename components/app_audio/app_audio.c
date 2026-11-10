/* app_audio — 音频中枢：播放会话仲裁 + 白噪音库 + 音量（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_audio.h"

static const char *TAG = "app_audio";

esp_err_t app_audio_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
