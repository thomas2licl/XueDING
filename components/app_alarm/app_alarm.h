#ifndef APP_ALARM_H
#define APP_ALARM_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_alarm — 闹钟引擎（S2.3 实体化）
 * 1s 周期比对 app_clock 缓存时间,命中 → LL_EV_ALARM_FIRED（载荷 int32 闹钟 id）
 * → 进入响铃状态（S2.3 屏幕/日志提醒,音频响铃 S3 接入;贪睡 S2.4 UI 接入）。
 * 闹钟存 NVS,断电/重启不丢。 */

#define APP_ALARM_MAX      8       /* 最多闹钟条数 */
#define APP_ALARM_RING_S   30      /* 无操作自动停铃秒数（S2.4 UI 接入停止/贪睡） */

typedef enum {
    LL_ALARM_ONCE = 0,      /* 单次:触发后自动删除 */
    LL_ALARM_DAILY = 1,     /* 每日 */
    LL_ALARM_WORKWEEK = 2,  /* 周一~周五（2026-11-19 U4 需求细化） */
    LL_ALARM_MON_SAT = 3,   /* 周一~周六 */
    /* 预留:任意星期组合（weekdays_mask 位掩码,需求明确后扩展） */
} ll_alarm_repeat_t;

typedef struct {
    bool enabled;
    uint8_t hour;          /* 0~23 */
    uint8_t minute;        /* 0~59 */
    uint8_t repeat_mode;   /* ll_alarm_repeat_t */
} ll_alarm_t;

esp_err_t app_alarm_init(void);
void app_alarm_tick(void);        /* 1s tick（slow_tick 调度） */

/* 增删改查（id = 数组下标;-1 无空闲;改 enabled 也走 update 落 NVS） */
int      app_alarm_add(const ll_alarm_t *alarm);
esp_err_t app_alarm_remove(int id);
esp_err_t app_alarm_update(int id, const ll_alarm_t *alarm);
int      app_alarm_get_all(ll_alarm_t *out, int max);
/* 有效闹钟+槽位 id 一起取出（UI 列表用:get_all 压缩空槽,序号≠槽位,
 * update/remove 需要真实槽位 id,否则空洞时改错条目） */
int      app_alarm_get_with_ids(ll_alarm_t *out, int *ids, int max);

/* 响铃状态（UI 查询;触发后 APP_ALARM_RING_S 秒无操作自动停） */
bool app_alarm_is_ringing(void);
esp_err_t app_alarm_stop(void);       /* 停铃（UI 按钮/语音入口） */

#ifdef __cplusplus
}
#endif

#endif /* APP_ALARM_H */
