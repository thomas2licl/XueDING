/* bsp_imu — QMI8658 加速度计驱动（纯 C,寄存器级）
 * 初始化序列参照微雪 SensorLib SensorQMI8658（MIT）:
 * 软复位(0x60=0xB0 → 等 0x4D==0x80) → CTRL1.ADDR_AI → WHO_AM_I → CTRL2 配置 → CTRL7 使能 */

#include "bsp_imu.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "bsp_imu";

/* QMI8658 寄存器（仅列本项目用到的） */
#define QMI8658_ADDR            0x6B        /* 7 位地址(SA0=0) */
#define QMI8658_REG_WHOAMI      0x00        /* 期望 0x05 */
#define QMI8658_REG_CTRL1       0x02        /* bit6: ADDR_AI 地址自增(多字节读必需) */
#define QMI8658_REG_CTRL2       0x03        /* [5:4]量程 [3:0]ODR */
#define QMI8658_REG_CTRL7       0x08        /* bit0: aEN 加速度计使能 */
#define QMI8658_REG_AX_L        0x35        /* AX/AY/AZ 各 2 字节,小端 */
#define QMI8658_REG_RST_RESULT  0x4D        /* 复位完成标志 0x80 */
#define QMI8658_REG_RESET       0x60        /* 写 0xB0 软复位 */

/* CTRL2 配置：±2G 量程 + 低功耗 11Hz ODR（方向判定轮询足够,功耗低;
 * 低功耗 ODR 仅陀螺仪禁用时可用,本驱动不使能陀螺仪） */
#define QMI8658_CTRL2_ACCEL_CFG ((0 << 4) | 0x0E)

/* ±2G 量程换算：LSB → g */
#define ACCEL_SCALE_G           (2.0f / 32768.0f)

#define I2C_TIMEOUT_MS          100

static i2c_master_dev_handle_t s_imu_dev = NULL;

static esp_err_t wr_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_imu_dev, buf, sizeof(buf), pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t rd_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_imu_dev, &reg, 1, buf, len, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

esp_err_t bsp_imu_init(void)
{
    if (s_imu_dev != NULL) {
        return ESP_OK;
    }

    /* 挂到 I2C 总线0（与 TCA9554/RTC 同总线,见 user_config.h;不经 i2c_bsp
     * 封装——其 wait 硬编码总线1,只适用触摸） */
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_master_get_bus_handle(0, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get i2c bus0 handle failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI8658_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_imu_dev));

    /* 软复位（最长约 500ms 完成） */
    ESP_ERROR_CHECK(wr_reg(QMI8658_REG_RESET, 0xB0));
    uint8_t rst = 0;
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (rd_regs(QMI8658_REG_RST_RESULT, &rst, 1) == ESP_OK && rst == 0x80) {
            break;
        }
    }
    if (rst != 0x80) {
        ESP_LOGE(TAG, "reset not complete (rst_result=0x%02X)", rst);
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(wr_reg(QMI8658_REG_CTRL1, 0x40));   /* 地址自增 */

    /* 器件识别 */
    uint8_t id = 0;
    ESP_ERROR_CHECK(rd_regs(QMI8658_REG_WHOAMI, &id, 1));
    if (id != 0x05) {
        ESP_LOGE(TAG, "who_am_i mismatch: 0x%02X (expect 0x05)", id);
        return ESP_FAIL;
    }

    /* 配置并使能加速度计 */
    ESP_ERROR_CHECK(wr_reg(QMI8658_REG_CTRL2, QMI8658_CTRL2_ACCEL_CFG));
    ESP_ERROR_CHECK(wr_reg(QMI8658_REG_CTRL7, 0x01));

    ESP_LOGI(TAG, "QMI8658 init done (accel ±2G, LP 11Hz)");
    return ESP_OK;
}

esp_err_t bsp_imu_read_accel(float *x_g, float *y_g, float *z_g)
{
    uint8_t buf[6];
    ESP_ERROR_CHECK(rd_regs(QMI8658_REG_AX_L, buf, sizeof(buf)));
    if (x_g) *x_g = (int16_t)((uint16_t)buf[1] << 8 | buf[0]) * ACCEL_SCALE_G;
    if (y_g) *y_g = (int16_t)((uint16_t)buf[3] << 8 | buf[2]) * ACCEL_SCALE_G;
    if (z_g) *z_g = (int16_t)((uint16_t)buf[5] << 8 | buf[4]) * ACCEL_SCALE_G;
    return ESP_OK;
}
