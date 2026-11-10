#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* LocalLife 公共板级配置（微雪 ESP32-S3-Touch-LCD-3.49-V2）
 * 自厂商例程 main/user_config.h 迁入 bsp_common，供各 bsp/组件共享 */

/* SPI host */
#define LCD_HOST SPI3_HOST

/* I2C 总线0：外设（TCA9554 / PCF85063 / QMI8658） */
#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

/* I2C 总线1：触摸 */
#define Touch_SCL_NUM (GPIO_NUM_18)
#define Touch_SDA_NUM (GPIO_NUM_17)

/* 显示：面板原生 172(H) x 640(V) 竖条 */
#define EXAMPLE_LCD_H_RES              172
#define EXAMPLE_LCD_V_RES              640
#define LVGL_DMA_BUFF_LEN    (EXAMPLE_LCD_H_RES * 64 * 2)
#define LVGL_SPIRAM_BUFF_LEN (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2)

/* 软件旋转开关（S1.2 起横屏为默认形态，此宏保留兼容厂商逻辑） */
#define USER_DISP_ROT_90    1
#define USER_DISP_ROT_NONO  0
#define Rotated USER_DISP_ROT_NONO

/* 背光循环测试（0=关闭） */
#define Backlight_Testing 0

/* 显示引脚 */
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_TE            (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_LCD_RST           (-1)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (GPIO_NUM_42)

#define EXAMPLE_PIN_NUM_EXIO_INT          (GPIO_NUM_8)

/* TCA9554 IO 扩展器引脚 */
#define EXAMPLE_EXIO_PIN_TOUCH_INT        (1ULL << 0)
#define EXAMPLE_EXIO_PIN_BL_EN            (1ULL << 1)
#define EXAMPLE_EXIO_PIN_IMU_INT1         (1ULL << 2)
#define EXAMPLE_EXIO_PIN_IMU_INT2         (1ULL << 3)
#define EXAMPLE_EXIO_PIN_RTC_INT          (1ULL << 4)
#define EXAMPLE_EXIO_PIN_LCD_RST          (1ULL << 5)
#define EXAMPLE_EXIO_PIN_SYS_EN           (1ULL << 6)
#define EXAMPLE_EXIO_PIN_NS_MODE          (1ULL << 7)

/* 触摸 */
#define DISP_TOUCH_ADDR                   0x3B
#define EXAMPLE_PIN_NUM_TOUCH_RST         (-1)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (-1)

#endif
