/* app_alarm — 闹钟引擎（S2.3）
 * 触发判定：1s 周期读 app_clock 缓存,时/分匹配且与上次触发分钟不同（去重）
 * → LL_EV_ALARM_FIRED（载荷 id）→ 响铃状态（30s 无操作自动停）。
 * 存储：NVS namespace "ll" 键 "alarms",整个数组一个 blob。 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

#include "app_alarm.h"
#include "app_event.h"
#include "app_clock.h"

static const char *TAG = "app_alarm";

#define NVS_NS          "ll"
#define NVS_KEY_ALARMS  "alarms"

/* 编译期测试闹钟：置 N 时 init 后自动添加一个 N 分钟后触发的单次闹钟
 * （S2.3 链路验证用,已于 S2.4 验收后归零;UI 编辑闹钟待 S2.5+/需求细化） */
#define ALARM_TEST_MIN_AFTER    0

typedef struct {
    ll_alarm_t list[APP_ALARM_MAX];
    /* 去重:每条闹钟上次已触发的"天+分钟"标签（weekday*1440+当天分钟,跨日自然重置） */
    uint16_t last_fired_tag[APP_ALARM_MAX];
    bool ringing;
    int ringing_id;
    uint8_t ring_countdown_s;
} alarm_state_t;

static alarm_state_t s_st;

static void save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY_ALARMS, s_st.list, sizeof(s_st.list));
    nvs_commit(h);
    nvs_close(h);
}

static void load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        memset(s_st.list, 0, sizeof(s_st.list));
        return;
    }
    size_t len = sizeof(s_st.list);
    if (nvs_get_blob(h, NVS_KEY_ALARMS, s_st.list, &len) != ESP_OK || len != sizeof(s_st.list)) {
        memset(s_st.list, 0, sizeof(s_st.list));
    }
    nvs_close(h);
}

static void fire(int id)
{
    s_st.ringing = true;
    s_st.ringing_id = id;
    s_st.ring_countdown_s = APP_ALARM_RING_S;

    int32_t payload = id;
    app_event_post(LL_EV_ALARM_FIRED, &payload, sizeof(payload), 0);
    ESP_LOGW(TAG, "ALARM %d fired: %02u:%02u (%s), ringing %us",
             id, s_st.list[id].hour, s_st.list[id].minute,
             s_st.list[id].repeat_mode == LL_ALARM_DAILY ? "daily" : "once",
             (unsigned)APP_ALARM_RING_S);

    if (s_st.list[id].repeat_mode == LL_ALARM_ONCE) {
        /* 单次闹钟响完即删（手机习惯语义,同时避免残留占满槽位） */
        const uint8_t h = s_st.list[id].hour, m = s_st.list[id].minute;
        memset(&s_st.list[id], 0, sizeof(ll_alarm_t));
        s_st.last_fired_tag[id] = 0;
        save_nvs();
        ESP_LOGI(TAG, "once alarm %02u:%02u removed after fire", h, m);
    }
}

static void alarm_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* 响铃倒计时（无操作自动停） */
        if (s_st.ringing && --s_st.ring_countdown_s == 0) {
            app_alarm_stop();
        }

        const bsp_rtc_datetime_t *now = app_clock_get_time();
        if (now == NULL) continue;

        const uint16_t tag = (uint16_t)(now->weekday * 1440u + now->hour * 60u + now->minute);

        for (int i = 0; i < APP_ALARM_MAX; i++) {
            if (!s_st.list[i].enabled) continue;
            if (s_st.list[i].hour != now->hour || s_st.list[i].minute != now->minute) continue;
            /* 重复档位过滤（weekday:0=周日,1~6=周一~六） */
            if (s_st.list[i].repeat_mode == LL_ALARM_WORKWEEK &&
                (now->weekday == 0 || now->weekday == 6)) continue;
            if (s_st.list[i].repeat_mode == LL_ALARM_MON_SAT && now->weekday == 0) continue;
            if (s_st.last_fired_tag[i] == tag) continue;   /* 同一天同一分钟只触发一次 */

            s_st.last_fired_tag[i] = tag;
            fire(i);
        }
    }
}

esp_err_t app_alarm_init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    load_nvs();

#if ALARM_TEST_MIN_AFTER > 0
    {
        /* 注意:app_clock 首次缓存要 1s 后才有,init 里等一拍 */
        vTaskDelay(pdMS_TO_TICKS(1500));
        const bsp_rtc_datetime_t *now = app_clock_get_time();
        if (now) {
            const uint16_t m = (uint16_t)(now->hour * 60u + now->minute + ALARM_TEST_MIN_AFTER);
            ll_alarm_t t = {
                .enabled = true,
                .hour = (uint8_t)(m / 60 % 24),
                .minute = (uint8_t)(m % 60),
                .repeat_mode = LL_ALARM_ONCE,
            };
            int id = app_alarm_add(&t);
            ESP_LOGW(TAG, "TEST alarm id=%d at %02u:%02u (%d min later)",
                     id, t.hour, t.minute, ALARM_TEST_MIN_AFTER);
        }
    }
#endif

    int n = 0;
    for (int i = 0; i < APP_ALARM_MAX; i++) {
        if (s_st.list[i].enabled) n++;
    }

    if (xTaskCreate(alarm_task, "alarm", 3 * 1024, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create task failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "alarm engine running (%d enabled)", n);
    return ESP_OK;
}

int app_alarm_add(const ll_alarm_t *alarm)
{
    for (int i = 0; i < APP_ALARM_MAX; i++) {
        /* 空槽判定:未使能且时间全 0 */
        if (!s_st.list[i].enabled && s_st.list[i].hour == 0 && s_st.list[i].minute == 0) {
            s_st.list[i] = *alarm;
            s_st.last_fired_tag[i] = 0;
            save_nvs();
            return i;
        }
    }
    return -1;
}

esp_err_t app_alarm_remove(int id)
{
    if (id < 0 || id >= APP_ALARM_MAX) return ESP_ERR_INVALID_ARG;
    memset(&s_st.list[id], 0, sizeof(ll_alarm_t));
    s_st.last_fired_tag[id] = 0;
    save_nvs();
    return ESP_OK;
}

esp_err_t app_alarm_update(int id, const ll_alarm_t *alarm)
{
    if (id < 0 || id >= APP_ALARM_MAX || alarm == NULL) return ESP_ERR_INVALID_ARG;
    s_st.list[id] = *alarm;
    s_st.last_fired_tag[id] = 0;
    save_nvs();
    return ESP_OK;
}

int app_alarm_get_all(ll_alarm_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < APP_ALARM_MAX && n < max; i++) {
        if (s_st.list[i].enabled || s_st.list[i].hour || s_st.list[i].minute) {
            out[n++] = s_st.list[i];
        }
    }
    return n;
}

int app_alarm_get_with_ids(ll_alarm_t *out, int *ids, int max)
{
    int n = 0;
    for (int i = 0; i < APP_ALARM_MAX && n < max; i++) {
        if (s_st.list[i].enabled || s_st.list[i].hour || s_st.list[i].minute) {
            out[n] = s_st.list[i];
            ids[n] = i;
            n++;
        }
    }
    return n;
}

bool app_alarm_is_ringing(void)
{
    return s_st.ringing;
}

esp_err_t app_alarm_stop(void)
{
    if (s_st.ringing) {
        ESP_LOGI(TAG, "alarm %d stopped", s_st.ringing_id);
    }
    s_st.ringing = false;
    return ESP_OK;
}
