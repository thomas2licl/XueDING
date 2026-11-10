#ifndef BSP_EXIO_H
#define BSP_EXIO_H

#include "esp_io_expander_tca9554.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 TCA9554（I2C 总线0），配置各引脚方向：
 * TOUCH_INT 输入；BL_EN / LCD_RST / SYS_EN / NS_MODE 输出。
 * 须在 i2c 总线初始化（touch_i2c_master_Init）之后调用。 */
esp_err_t bsp_exio_init(void);

/* 获取 TCA9554 句柄（供 bsp_power 等其他组件复用） */
esp_io_expander_handle_t bsp_exio_get_handle(void);

/* 常用引脚操作 */
esp_err_t bsp_exio_set_lcd_rst(int level);   /* LCD 复位 */
esp_err_t bsp_exio_set_bl_en(int enable);    /* 背光使能 */
esp_err_t bsp_exio_set_sys_en(int level);    /* 系统电源自锁保持（1=保持供电） */
esp_err_t bsp_exio_set_ns_mode(int level);   /* NS_MODE（音频相关，S3 确认用途） */

#ifdef __cplusplus
}
#endif

#endif /* BSP_EXIO_H */
