#ifndef APP_UI_H
#define APP_UI_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_ui — LVGL 界面层（架构 §10）
 * S1 阶段为横屏验证页：触摸坐标/旋转切换/局部刷新动画/事件链路。
 * 调用方须已持 LVGL 锁（bsp_lcd_lock）。 */
esp_err_t app_ui_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_H */
