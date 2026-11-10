#ifndef APP_EVENT_H
#define APP_EVENT_H

#include <stddef.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LocalLife 事件总线（架构 §4）
 * 所有功能服务之间通过本事件解耦，禁止直接互调。
 * 载荷结构体由各事件的发布者定义在各自头文件中，此处只管路由。 */

typedef enum {
    /* 时间/网络 */
    LL_EV_TIME_SYNCED,        /* SNTP 对时完成 */
    LL_EV_NET_STATE,          /* 网络连接状态变化 */

    /* 闹钟/提醒/倒计时 */
    LL_EV_ALARM_FIRED,        /* 闹钟触发（载荷: 闹钟 id） */
    LL_EV_ALARM_SNOOZE,       /* 贪睡 */
    LL_EV_REMINDER_FIRED,     /* 提醒触发 */
    LL_EV_TIMER_FIRED,        /* 沙漏倒计时结束 */
    LL_EV_TIMER_STATE,        /* 倒计时状态变化（运行/取消） */

    /* 语音/音频 */
    LL_EV_VOICE_CMD,          /* 语音命令（载荷: 命令 id + 参数） */
    LL_EV_PLAY_REQUEST,       /* 播放请求（源类型 + 资源 id） */
    LL_EV_PLAY_STATE,         /* 播放状态变化 */

    /* 天气/场景 */
    LL_EV_WEATHER_UPDATED,    /* 天气数据更新 */
    LL_EV_SCENE_ENTER,        /* 进入场景 */
    LL_EV_SCENE_EXIT,         /* 退出场景 */

    /* 电源/显示/姿态 */
    LL_EV_BRIGHTNESS,         /* 亮度变化请求 */
    LL_EV_BATTERY_STATE,      /* 电量/充电/供电来源变化 */
    LL_EV_SCREEN_STATE,       /* 亮屏/息屏 */
    LL_EV_NIGHTLIGHT_STATE,   /* 小夜灯开/关 */
    LL_EV_ORIENTATION,        /* 左横/右横方向变化 */
    LL_EV_GESTURE_STANDUP,    /* 竖放手势（仅沙漏页待机时发布） */
    LL_EV_GESTURE_SHAKE,      /* 晃动手势（仅夜灯页且灯关时发布） */

    LL_EV_MAX
} ll_event_id_t;

/* 初始化事件总线（内部创建/获取事件循环） */
esp_err_t app_event_init(void);

/* 发布事件（在调用者上下文同步分发；timeout 仅用于队列满时） */
esp_err_t app_event_post(ll_event_id_t id, const void *data, size_t len, uint32_t timeout_ms);

/* 订阅事件 */
esp_err_t app_event_subscribe(ll_event_id_t id, esp_event_handler_t handler, void *ctx);

/* 取消订阅 */
esp_err_t app_event_unsubscribe(ll_event_id_t id, esp_event_handler_t handler);

/* 取事件 id 的可读名（日志用） */
const char *app_event_name(ll_event_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* APP_EVENT_H */
