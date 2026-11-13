#ifndef APP_POWER_H
#define APP_POWER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_power — 电源管理服务（S1.6 实体化：电量采集与事件）
 * 5s 周期采集 VBAT/充电态 → 百分比查表 → LL_EV_BATTERY_STATE 上总线 +
 * 串口日志。低电告警（<3.4V 一次,回升 >3.5V 复位）。KEY1 长按 2s 软件关机。
 * 供电档位/息屏/小夜灯状态机为后续阶段（§11a/§11b）。 */

/* LL_EV_BATTERY_STATE 事件载荷 */
typedef struct {
    uint16_t vbat_mv;    /* 电池电压 mV */
    uint8_t percent;     /* 电量百分比 0-100（查表估算） */
    bool charging;       /* 充电中（STAT 低） */
} ll_battery_state_t;

esp_err_t app_power_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_POWER_H */
