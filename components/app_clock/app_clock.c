/* app_clock — 时间服务（S2.2）
 * 1s 读 RTC → 缓存 + 低频走时日志。开机 RTC 无效（首次上电/断电后）时
 * 以编译时刻（__DATE__/__TIME__）自动校时——临时校时手段,精确到
 * "重新编译+烧录"的时间差（分钟级）;正式校时走 S5 SNTP / S2.4 UI 设时页。 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_clock.h"
#include "app_event.h"

static const char *TAG = "app_clock";

#define CLOCK_POLL_PERIOD_MS   1000    /* 读 RTC 周期 */
#define CLOCK_LOG_DIV          10      /* 每 N 秒一条走时日志 */

/* SNTP 对时事件（app_net 发,esp_event 上下文）——直接写 RTC:
 * bus0 驱动级互斥,与 IMU 轮询安全共享 */
static void on_time_synced(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    const bsp_rtc_datetime_t *dt = (const bsp_rtc_datetime_t *)data;
    if (dt == NULL) return;
    ESP_LOGI(TAG, "SNTP time received, syncing RTC...");
    app_clock_sync_time(dt);
}

/* 开机 RTC 无效时用编译时刻校时（置 0 关闭——仅用于观察无效状态） */
#define CLOCK_SET_COMPILE_TIME 1

static bsp_rtc_datetime_t s_now;            /* 当前时间缓存（本任务单写） */
static volatile bool s_time_ready = false;

/* 解析 __DATE__（"Nov 18 2026"/"Mmm  d yyyy"）与 __TIME__（"hh:mm:ss"） */
static bool parse_build_time(bsp_rtc_datetime_t *dt)
{
    static const char months[12][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    char mon[4] = { 0 };
    int day = 0, year = 0, h = 0, m = 0, s = 0;
    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3) return false;
    if (sscanf(__TIME__, "%d:%d:%d", &h, &m, &s) != 3) return false;

    int mi;
    for (mi = 0; mi < 12; mi++) {
        if (strcmp(mon, months[mi]) == 0) break;
    }
    if (mi == 12 || day < 1 || day > 31) return false;

    memset(dt, 0, sizeof(*dt));
    dt->year = (uint16_t)year;
    dt->month = (uint8_t)(mi + 1);
    dt->day = (uint8_t)day;
    dt->hour = (uint8_t)h;
    dt->minute = (uint8_t)m;
    dt->second = (uint8_t)s;
    return true;
}

/* 1s tick（由 main 的 slow_tick 调度,2026-11-21 任务合并省栈） */
void app_clock_tick(void)
{
    {
        static int n = 0;
        if (bsp_rtc_get_time(&s_now) == ESP_OK) {
            s_time_ready = true;
        }
        if ((++n % CLOCK_LOG_DIV) == 0) {
            ESP_LOGI(TAG, "%04u-%02u-%02u %02u:%02u:%02u w%u",
                     (unsigned)s_now.year, s_now.month, s_now.day,
                     s_now.hour, s_now.minute, s_now.second, s_now.weekday);
        }
    }
}

const bsp_rtc_datetime_t *app_clock_get_time(void)
{
    return s_time_ready ? &s_now : NULL;
}

esp_err_t app_clock_sync_time(const bsp_rtc_datetime_t *dt)
{
    esp_err_t err = bsp_rtc_set_time(dt);
    if (err == ESP_OK) {
        s_now = *dt;
        s_time_ready = true;
        ESP_LOGI(TAG, "time synced: %04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)dt->year, dt->month, dt->day,
                 dt->hour, dt->minute, dt->second);
    }
    return err;
}

esp_err_t app_clock_init(void)
{
    memset(&s_now, 0, sizeof(s_now));

    /* RTC 硬件失败不阻塞整机（时间功能降级） */
    esp_err_t err = bsp_rtc_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rtc init failed, clock service degraded: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    if (!bsp_rtc_is_valid()) {
        ESP_LOGW(TAG, "rtc time invalid (first boot or power lost)");
#if CLOCK_SET_COMPILE_TIME
        bsp_rtc_datetime_t bt;
        if (parse_build_time(&bt)) {
            /* 误差 = 编译到开机的耗时（烧录+启动,分钟级精度足够当前阶段） */
            ESP_LOGI(TAG, "set rtc from build time: %s %s", __DATE__, __TIME__);
            app_clock_sync_time(&bt);
        }
#endif
    }

    app_event_subscribe(LL_EV_TIME_SYNCED, on_time_synced, NULL);

    ESP_LOGI(TAG, "clock service ready (slow_tick)");
    return ESP_OK;
}
