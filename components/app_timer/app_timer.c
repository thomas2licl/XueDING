/* app_timer — 沙漏倒计时引擎（U5 实体化）
 * 1s 周期任务递减剩余秒;状态迁移统一走 set_state() 广播 LL_EV_TIMER_STATE。
 * 沙量动画由 UI 按 remain/total 比例自绘,引擎不管显示。 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "app_timer.h"
#include "app_event.h"

static const char *TAG = "app_timer";

static volatile ll_timer_state_t s_state = LL_TIMER_IDLE;
static volatile uint32_t s_remain_s = 0;      /* 剩余秒（RUNNING/PAUSED 有效） */
static uint32_t s_total_s = 0;                /* 本次总时长（沙量比例分母） */

static void set_state(ll_timer_state_t st)
{
    if (s_state == st) return;
    s_state = st;
    const int32_t payload = (int32_t)st;
    app_event_post(LL_EV_TIMER_STATE, &payload, sizeof(payload), 0);
    ESP_LOGI(TAG, "state -> %d (remain %us/%us)", (int)st,
             (unsigned)s_remain_s, (unsigned)s_total_s);
}

static void timer_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s_state != LL_TIMER_RUNNING) continue;
        if (s_remain_s > 0) s_remain_s--;

        if (s_remain_s == 0) {
            set_state(LL_TIMER_DONE);
            const int32_t payload = (int32_t)s_total_s;
            app_event_post(LL_EV_TIMER_FIRED, &payload, sizeof(payload), 0);
            ESP_LOGW(TAG, "TIMEOUT! %us elapsed", (unsigned)s_total_s);
        }
    }
}

esp_err_t app_timer_init(void)
{
    s_state = LL_TIMER_IDLE;
    s_remain_s = 0;

    if (xTaskCreate(timer_task, "hourglass", 3 * 1024, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create task failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "hourglass engine running");
    return ESP_OK;
}

esp_err_t app_timer_start(uint32_t duration_s)
{
    if (s_state == LL_TIMER_RUNNING || s_state == LL_TIMER_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_s == 0) duration_s = 1;
    if (duration_s > LL_TIMER_MAX_S) duration_s = LL_TIMER_MAX_S;

    s_total_s = duration_s;
    s_remain_s = duration_s;
    set_state(LL_TIMER_RUNNING);
    return ESP_OK;
}

esp_err_t app_timer_pause(void)
{
    if (s_state != LL_TIMER_RUNNING) return ESP_ERR_INVALID_STATE;
    set_state(LL_TIMER_PAUSED);
    return ESP_OK;
}

esp_err_t app_timer_resume(void)
{
    if (s_state != LL_TIMER_PAUSED) return ESP_ERR_INVALID_STATE;
    set_state(LL_TIMER_RUNNING);
    return ESP_OK;
}

esp_err_t app_timer_cancel(void)
{
    if (s_state == LL_TIMER_IDLE) return ESP_OK;
    s_remain_s = 0;
    set_state(LL_TIMER_IDLE);
    return ESP_OK;
}

ll_timer_state_t app_timer_get_state(void)
{
    return s_state;
}

uint32_t app_timer_get_remain_s(void)
{
    return (s_state == LL_TIMER_RUNNING || s_state == LL_TIMER_PAUSED) ? s_remain_s : 0;
}
