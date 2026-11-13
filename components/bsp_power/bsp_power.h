#ifndef BSP_POWER_H
#define BSP_POWER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* bsp_power — 电池/供电硬件层（S1.6,原理图确认 2026-11-13）
 *
 * 电路模型：
 *   - USB 插入 → 强制上电,系统走 USB 并给电池充电（ETA6098）
 *   - 电池模式：按 KEY1(GPIO16,按下拉低)硬件使能电池 MOS 上电,
 *     软件须尽快拉高 SYS_EN(TCA9554 PP6,bsp_exio 已做)自锁保持供电;
 *     KEY1 松开且 SYS_EN 低 → 断电
 *   - VBAT 经 200K/100K 分压(1/3)接 GPIO4(ADC1_CH3)
 *   - ETA6098 STAT(9脚,开漏)飞线接 GPIO5+内部上拉：充电中=低,充满/未充=高 */

/* 初始化：VBAT ADC（含 eFuse 校准）+ STAT/KEY1 GPIO */
esp_err_t bsp_power_init(void);

/* 读电池电压（mV,32 次采样均值 ×3 分压还原）。标定前含分压/ADC 系统误差 */
esp_err_t bsp_power_read_vbat_mv(uint16_t *vbat_mv);

/* 充电中？（STAT 低 = 充电中;充电中必然 USB 在线） */
bool bsp_power_is_charging(void);

/* KEY1 电源键当前按下？ */
bool bsp_power_is_key_pressed(void);

/* 软件关机：拉低 SYS_EN（电池模式下 KEY1 松开即断电;USB 供电时无效——USB 强制上电） */
esp_err_t bsp_power_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_POWER_H */
