/* app_timer — 沙漏倒计时：≤30min、点击/竖放启动、漏沙动画联动、滴滴（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_timer.h"

static const char *TAG = "app_timer";

esp_err_t app_timer_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
