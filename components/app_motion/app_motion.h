#ifndef APP_MOTION_H
#define APP_MOTION_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_motion — 姿态服务：IMU 中断驱动，方向判定+竖放/晃动手势（页面限定）（S1 空壳） */
esp_err_t app_motion_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MOTION_H */
