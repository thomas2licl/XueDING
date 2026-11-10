/* app_event — LocalLife 事件总线（esp_event 封装,架构 §4） */

#include "esp_log.h"

#include "app_event.h"

static const char *TAG = "app_event";

/* 事件基（同一基下用 id 区分,便于统一订阅调试） */
ESP_EVENT_DECLARE_BASE(LL_EVENTS);
ESP_EVENT_DEFINE_BASE(LL_EVENTS);

static const char *s_event_names[LL_EV_MAX] = {
    [LL_EV_TIME_SYNCED]      = "TIME_SYNCED",
    [LL_EV_NET_STATE]        = "NET_STATE",
    [LL_EV_ALARM_FIRED]      = "ALARM_FIRED",
    [LL_EV_ALARM_SNOOZE]     = "ALARM_SNOOZE",
    [LL_EV_REMINDER_FIRED]   = "REMINDER_FIRED",
    [LL_EV_TIMER_FIRED]      = "TIMER_FIRED",
    [LL_EV_TIMER_STATE]      = "TIMER_STATE",
    [LL_EV_VOICE_CMD]        = "VOICE_CMD",
    [LL_EV_PLAY_REQUEST]     = "PLAY_REQUEST",
    [LL_EV_PLAY_STATE]       = "PLAY_STATE",
    [LL_EV_WEATHER_UPDATED]  = "WEATHER_UPDATED",
    [LL_EV_SCENE_ENTER]      = "SCENE_ENTER",
    [LL_EV_SCENE_EXIT]       = "SCENE_EXIT",
    [LL_EV_BRIGHTNESS]       = "BRIGHTNESS",
    [LL_EV_BATTERY_STATE]    = "BATTERY_STATE",
    [LL_EV_SCREEN_STATE]     = "SCREEN_STATE",
    [LL_EV_NIGHTLIGHT_STATE] = "NIGHTLIGHT_STATE",
    [LL_EV_ORIENTATION]      = "ORIENTATION",
    [LL_EV_GESTURE_STANDUP]  = "GESTURE_STANDUP",
    [LL_EV_GESTURE_SHAKE]    = "GESTURE_SHAKE",
};

esp_err_t app_event_init(void)
{
    /* 使用 esp_event 默认循环（系统各组件也挂在其上） */
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* 已创建视为成功 */
        ESP_LOGE(TAG, "create event loop failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "event bus ready (%d events)", (int)LL_EV_MAX);
    return ESP_OK;
}

esp_err_t app_event_post(ll_event_id_t id, const void *data, size_t len, uint32_t timeout_ms)
{
    if (id >= LL_EV_MAX) return ESP_ERR_INVALID_ARG;
    return esp_event_post(LL_EVENTS, (int32_t)id, data, len,
                          timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY);
}

esp_err_t app_event_subscribe(ll_event_id_t id, esp_event_handler_t handler, void *ctx)
{
    if (id >= LL_EV_MAX) return ESP_ERR_INVALID_ARG;
    return esp_event_handler_register(LL_EVENTS, (int32_t)id, handler, ctx);
}

esp_err_t app_event_unsubscribe(ll_event_id_t id, esp_event_handler_t handler)
{
    if (id >= LL_EV_MAX) return ESP_ERR_INVALID_ARG;
    return esp_event_handler_unregister(LL_EVENTS, (int32_t)id, handler);
}

const char *app_event_name(ll_event_id_t id)
{
    if (id >= LL_EV_MAX) return "INVALID";
    return s_event_names[id] ? s_event_names[id] : "UNNAMED";
}
