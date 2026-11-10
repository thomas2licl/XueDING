#ifndef BSP_LCD_H
#define BSP_LCD_H

#include <stdbool.h>
#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化显示子系统：背光 PWM → SPI 总线 → AXS15231B 面板 → LVGL（含 tick/touch/任务）
 * 依赖：i2c 总线（touch_i2c_master_Init）与 bsp_exio_init 须先完成。
 * 返回前背光已使能，可直接在 LVGL 锁内创建 UI。 */
esp_err_t bsp_lcd_init(void);

/* LVGL 互斥锁：任何非 LVGL 任务线程调用 LVGL API 前必须持有 */
bool bsp_lcd_lock(int timeout_ms);   /* -1 = 永久等待 */
void bsp_lcd_unlock(void);

/* 运行时切换界面旋转方向（LVGL 内部坐标系 90/270,竖屏原生 0）
 * S1.2 起供 app_motion 方向自适应调用 */
esp_err_t bsp_lcd_set_rotation(lv_display_rotation_t rot);

/* 当前旋转方向（app_motion 防抖判定需要） */
lv_display_rotation_t bsp_lcd_get_rotation(void);

/* 最近一次有效触摸的原始报文坐标（tx 沿 640 长边, ty 沿 172 短边）
 * S1.7 触摸映射校准诊断用,正式 UI 不依赖 */
void bsp_lcd_touch_get_raw(uint16_t *tx, uint16_t *ty);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LCD_H */
