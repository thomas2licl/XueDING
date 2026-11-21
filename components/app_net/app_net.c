/* app_net — WiFi STA + SNTP 对时（2026-11-21 临时版）
 * 硬编码热点（user_config.h WIFI_TMP_*）→ 拿到 IP 后启动 SNTP(ntp.aliyun.com)
 * → 同步回调（lwIP 上下文,不碰 I2C）只发 LL_EV_TIME_SYNCED 事件,
 *   由 app_clock 订阅消费（esp_event 上下文写 bus0 RTC,bus0 有驱动级互斥）。
 * 断线自动重连;状态变化发 LL_EV_NET_STATE(bool)。正式配网归 U8。 */

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_system.h"

#include "app_net.h"
#include "app_event.h"
#include "bsp_rtc.h"
#include "user_config.h"

static const char *TAG = "app_net";

static volatile bool s_connected = false;
static bool s_sntp_started = false;

/* SNTP 同步回调（lwIP 定时器上下文——只做轻量事件转发,勿做 I2C/长操作） */
static void sntp_sync_cb(struct timeval *tv)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year < 100) return;            /* 时间仍无效 */

    bsp_rtc_datetime_t dt = {
        .year   = (uint16_t)(tm_now.tm_year + 1900),
        .month  = (uint8_t)(tm_now.tm_mon + 1),
        .day    = (uint8_t)tm_now.tm_mday,
        .hour   = (uint8_t)tm_now.tm_hour,
        .minute = (uint8_t)tm_now.tm_min,
        .second = (uint8_t)tm_now.tm_sec,
        .weekday = (uint8_t)tm_now.tm_wday,
    };
    app_event_post(LL_EV_TIME_SYNCED, &dt, sizeof(dt), 0);
    ESP_LOGI(TAG, "SNTP synced: %04u-%02u-%02u %02u:%02u:%02u",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

static void start_sntp(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;

    setenv("TZ", "CST-8", 1);                    /* 东八区 */
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, WIFI_TMP_NTP_SRV);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (%s)", WIFI_TMP_NTP_SRV);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        const bool st = false;
        app_event_post(LL_EV_NET_STATE, &st, sizeof(st), 0);
        ESP_LOGW(TAG, "wifi disconnected, retrying");
        esp_wifi_connect();                      /* 自动重连 */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        s_connected = true;
        const bool st = true;
        app_event_post(LL_EV_NET_STATE, &st, sizeof(st), 0);
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&evt->ip_info.ip));
        start_sntp();
    }
}

/* WiFi 初始化独立任务（2026-11-21 内存挤压终版架构）:
 * esf 静态池需大块连续内部 RAM——最后启动时堆已碎片化拿不出整块,
 * 先启动又会挤死 motion/timer 任务栈。折衷:main 早期建本任务(小栈),
 * 延时 1s 再做 esp_wifi_init——此刻小任务已建、LVGL 大户(渲染缓冲/9 屏)
 * 尚未创建、堆仍完整,esf 与任务栈各得其所。 */
static void net_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(50));   /* 让 main 先建好 app_event/storage 依赖即动手 */

    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "before wifi init: internal=%u", (unsigned)esp_get_free_internal_heap_size());
    const esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init err=0x%x internal=%u — network degraded",
                 (int)err, (unsigned)esp_get_free_internal_heap_size());
        vTaskDelete(NULL);
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid, WIFI_TMP_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, WIFI_TMP_PASS, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi sta started (ssid=%s, temp config)", WIFI_TMP_SSID);
    /* 内存全景:WiFi 驱动占的 ~25KB 此刻已锁定(归驱动所有,随 net_init 栈释放无关) */
    ESP_LOGI(TAG, "after wifi start: internal=%u all=%u",
             (unsigned)esp_get_free_internal_heap_size(),
             (unsigned)esp_get_free_heap_size());
    vTaskDelete(NULL);   /* 事件驱动后续连接,任务功成身退 */
}

esp_err_t app_net_init(void)
{
    /* WiFi 事件挂 default loop——该 loop 由 app_event_init 创建（main 先于本 init）,
     * 此处不可重复创建(重复创建 abort,2026-11-21 真机踩坑) */
    /* 栈 4KB:esp_wifi_init 调用深,2KB 实测溢出(2026-11-21 真机);
     * 优先级 3>main:抢先在 main 建任务前把 esf 40KB 大块拿走(堆完整窗口) */
    if (xTaskCreate(net_task, "net_init", 4 * 1024, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create net task failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool app_net_is_connected(void)
{
    return s_connected;
}
