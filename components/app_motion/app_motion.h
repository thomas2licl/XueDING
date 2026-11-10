#ifndef APP_MOTION_H
#define APP_MOTION_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_motion — 姿态服务（S1.5 实体化）：
 * 轮询 QMI8658 加速度计,横放方向判定（左横/右横）,防抖后切换屏幕旋转
 * （bsp_lcd_set_rotation）并广播 LL_EV_ORIENTATION（载荷 lv_display_rotation_t）。
 * 竖放/晃动手势（页面限定）为后续阶段。
 * 注：IMU 硬件失败时本服务静默降级,不影响其他功能。 */
esp_err_t app_motion_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MOTION_H */
