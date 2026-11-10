#ifndef BSP_IMU_H
#define BSP_IMU_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* bsp_imu — 板载 QMI8658 六轴 IMU（I2C 总线0, 地址 0x6B）
 * 仅启用加速度计（方向判定/手势检测用,S1.5）;寄存器级纯 C 驱动,
 * 初始化序列与换算参照 QMI8658 数据手册及微雪 SensorLib 逻辑。
 * 依赖：I2C 总线0 已初始化（touch_i2c_master_Init）。 */

/* 初始化：软复位 → WHO_AM_I 校验(期望 0x05) → 加速度计 ±2G、低功耗 11Hz → 使能 */
esp_err_t bsp_imu_init(void);

/* 读加速度计（单位: g,量程 ±2G）。任一指针可为 NULL */
esp_err_t bsp_imu_read_accel(float *x_g, float *y_g, float *z_g);

#ifdef __cplusplus
}
#endif

#endif /* BSP_IMU_H */
