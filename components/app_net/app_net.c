/* app_net — WiFi STA 管理 + 断线重连 + SNTP 对时（S1 空壳，后续阶段实现） */

#include "esp_log.h"

#include "app_net.h"

static const char *TAG = "app_net";

esp_err_t app_net_init(void)
{
    ESP_LOGI(TAG, "init (S1 stub)");
    return ESP_OK;
}
