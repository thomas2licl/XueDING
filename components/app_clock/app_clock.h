#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include "esp_err.h"
#include "bsp_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_clock — 时间服务（S2.2 实体化）
 * 1s 周期读 PCF85063 → 内存缓存供 UI/其他服务取用。
 * 校时源：①开机 RTC 无效时以【编译时刻】自动校时（__DATE__/__TIME__,
 * 临时手段,S2.4 UI 设时页/S5 SNTP 接入后退役——重新编译烧录即校准）;
 * ②运行时外部校时调 app_clock_sync_time()（SNTP/UI 设时统一入口）。 */

/* 初始化：RTC 驱动 +（必要时）编译时刻校时 + 1s 走时任务。
 * RTC 硬件失败不阻塞整机（时间功能降级） */
esp_err_t app_clock_init(void);
void app_clock_tick(void);       /* 1s tick（slow_tick 调度,2026-11-21 任务合并） */

/* 取当前时间缓存（1s 刷新;直接读单写多读缓存,S2.4 UI 使用） */
const bsp_rtc_datetime_t *app_clock_get_time(void);

/* 运行时校时（SNTP/UI 设时的统一入口）：写 RTC 并刷新缓存 */
esp_err_t app_clock_sync_time(const bsp_rtc_datetime_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOCK_H */
