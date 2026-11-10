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

#ifdef __cplusplus
}
#endif

#endif /* BSP_LCD_H */
