/* app_motion — 姿态服务：IMU 中断驱动，方向判定+竖放/晃动手势（页面限定）（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_motion.h"

static const char *TAG = "app_motion";

esp_err_t app_motion_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
