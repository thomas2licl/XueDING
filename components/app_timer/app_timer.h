#ifndef APP_TIMER_H
#define APP_TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_timer — 沙漏倒计时引擎（U5 实体化）
 * 时长 ≤30min;状态变化广播 LL_EV_TIMER_STATE（载荷 ll_timer_state_t）;
 * 倒计时归零广播 LL_EV_TIMER_FIRED 并进入 DONE 态。
 * 引擎独立于 UI：页面隐藏计时照跑,UI 只查询/订阅刷新。
 * 提示音（滴滴）S3 音频接入,当前仅事件+屏幕提示。 */

#define LL_TIMER_MAX_S   (30u * 60u)   /* 最长 30 分钟（需求 §8） */

typedef enum {
    LL_TIMER_IDLE = 0,     /* 待设置（沙漏页的"设置态"） */
    LL_TIMER_RUNNING,      /* 倒计时中（漏沙动画联动） */
    LL_TIMER_PAUSED,       /* 暂停（沙停,恢复继续） */
    LL_TIMER_DONE,         /* 归零（时间到,等取消/重来） */
} ll_timer_state_t;

esp_err_t app_timer_init(void);
void app_timer_tick(void);       /* 1s tick（slow_tick 调度） */

/* 启动倒计时（IDLE/DONE 态才可;时长钳到 [1, LL_TIMER_MAX_S]） */
esp_err_t app_timer_start(uint32_t duration_s);

/* 暂停/恢复（仅 RUNNING/PAUSED 态有效） */
esp_err_t app_timer_pause(void);
esp_err_t app_timer_resume(void);

/* 取消（任意态 → IDLE;DONE 态"重来"= cancel + start） */
esp_err_t app_timer_cancel(void);

ll_timer_state_t app_timer_get_state(void);

/* 剩余整秒（RUNNING/PAUSED）;IDLE/DONE 返回 0 */
uint32_t app_timer_get_remain_s(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TIMER_H */
