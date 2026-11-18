#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* bsp_rtc — 板载 PCF85063 RTC（I2C 总线0, 地址 0x51）
 * 寄存器级纯 C 驱动（参照例程 02 SensorLib 逻辑与 PCF85063 数据手册）。
 * S2.1 范围：读时/设时/有效性检测;闹钟寄存器随 S2.3 app_alarm 启用。
 * 依赖：I2C 总线0 已初始化（touch_i2c_master_Init）。 */

typedef struct {
    uint16_t year;      /* 完整年份 2000~2099 */
    uint8_t month;      /* 1~12 */
    uint8_t day;        /* 1~31 */
    uint8_t weekday;    /* 0=周日 ~ 6=周六 */
    uint8_t hour;       /* 0~23（24 小时制） */
    uint8_t minute;     /* 0~59 */
    uint8_t second;     /* 0~59 */
} bsp_rtc_datetime_t;

/* 初始化：挂总线0 设备 + 探测（无 ACK 返回失败,调用方降级不阻塞整机） */
esp_err_t bsp_rtc_init(void);

/* 读时间（寄存器 0x04~0x0A,BCD 解码;跨秒撕裂由芯片内部读取锁存保证） */
esp_err_t bsp_rtc_get_time(bsp_rtc_datetime_t *dt);

/* 写时间（含星期自动计算 Zeller 公式） */
esp_err_t bsp_rtc_set_time(const bsp_rtc_datetime_t *dt);

/* RTC 时间有效？（振荡器停止标志 OS=0 且年份在合理范围。
 * 断电后 OS 置位 → 无效,需重新校时——风险⑮ 实测断电走时即验证此场景 */
bool bsp_rtc_is_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
