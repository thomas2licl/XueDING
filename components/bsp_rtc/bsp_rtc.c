/* bsp_rtc — PCF85063 RTC 驱动（纯 C,寄存器级,S2.1）
 * 寄存器地图与 BCD 编解码参照例程 02 SensorLib SensorPCF85063（MIT）:
 *   时间 0x04~0x0A（秒..年,BCD;秒 bit7=OS 振荡器停止标志,时 bit7=AM/PM）
 *   闹钟 0x0B~0x0F、定时器 0x10~0x11、CTRL1 0x00（bit5 STOP）、CTRL2 0x01 */

#include "bsp_rtc.h"

#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "bsp_rtc";

#define PCF85063_ADDR       0x51

#define PCF85063_REG_CTRL1  0x00    /* bit5: STOP 时钟停走 */
#define PCF85063_REG_SEC    0x04    /* bit7: OS 振荡器停止标志(断电置位) */
#define PCF85063_REG_MIN    0x05
#define PCF85063_REG_HOUR   0x06    /* bit7: AM/PM(12h),24h 模式值 0~23 */
#define PCF85063_REG_DAY    0x07
#define PCF85063_REG_WDAY   0x08    /* 0~6 */
#define PCF85063_REG_MON    0x09
#define PCF85063_REG_YEAR   0x0A    /* 0~99 → +2000 */

#define SEC_OS_MASK         0x80
#define HOUR_24H_MASK       0x3F

#define I2C_TIMEOUT_MS      100

static i2c_master_dev_handle_t s_rtc_dev = NULL;

static esp_err_t wr_regs(uint8_t reg, const uint8_t *buf, size_t len)
{
    uint8_t tmp[8] = { reg };
    for (size_t i = 0; i < len && i + 1 < sizeof(tmp); i++) tmp[i + 1] = buf[i];
    return i2c_master_transmit(s_rtc_dev, tmp, len + 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t rd_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_rtc_dev, &reg, 1, buf, len, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static inline uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* 星期计算（Zeller 公式,返回 0=周日~6=周六,与 PCF85063 weekday 定义一致） */
static uint8_t day_of_week(uint16_t y, uint8_t m, uint8_t d)
{
    if (m < 3) { m += 12; y -= 1; }
    const uint32_t k = y % 100, j = y / 100;
    const uint32_t h = (d + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;   /* 0=周六 */
    return (uint8_t)((h + 6) % 7);
}

esp_err_t bsp_rtc_init(void)
{
    if (s_rtc_dev != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_master_get_bus_handle(0, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get i2c bus0 handle failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_rtc_dev));

    /* 探测 + 确保时钟走行（清 CTRL1.STOP） */
    uint8_t ctrl1 = 0;
    ESP_ERROR_CHECK(rd_regs(PCF85063_REG_CTRL1, &ctrl1, 1));
    if (ctrl1 & (1 << 5)) {
        ESP_LOGW(TAG, "clock was stopped, starting");
        ESP_ERROR_CHECK(wr_regs(PCF85063_REG_CTRL1, (uint8_t[]){ ctrl1 & ~(1 << 5) }, 1));
    }

    ESP_LOGI(TAG, "PCF85063 init done (valid=%d)", bsp_rtc_is_valid());
    return ESP_OK;
}

esp_err_t bsp_rtc_get_time(bsp_rtc_datetime_t *dt)
{
    if (dt == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t buf[7];
    ESP_ERROR_CHECK(rd_regs(PCF85063_REG_SEC, buf, sizeof(buf)));
    dt->second  = bcd2dec(buf[0] & 0x7F);
    dt->minute  = bcd2dec(buf[1] & 0x7F);
    dt->hour    = bcd2dec(buf[2] & HOUR_24H_MASK);
    dt->day     = bcd2dec(buf[3] & 0x3F);
    dt->weekday = buf[4] & 0x07;
    dt->month   = bcd2dec(buf[5] & 0x1F);
    dt->year    = (uint16_t)(2000 + bcd2dec(buf[6]));
    return ESP_OK;
}

esp_err_t bsp_rtc_set_time(const bsp_rtc_datetime_t *dt)
{
    if (dt == NULL) return ESP_ERR_INVALID_ARG;

    const uint8_t buf[7] = {
        dec2bcd(dt->second) & 0x7F,          /* 写秒同时清 OS 标志 */
        dec2bcd(dt->minute),
        dec2bcd(dt->hour) & HOUR_24H_MASK,   /* 24h 模式 */
        dec2bcd(dt->day),
        day_of_week(dt->year, dt->month, dt->day),
        dec2bcd(dt->month),
        dec2bcd((uint8_t)(dt->year % 100)),
    };
    ESP_ERROR_CHECK(wr_regs(PCF85063_REG_SEC, buf, sizeof(buf)));
    return ESP_OK;
}

bool bsp_rtc_is_valid(void)
{
    uint8_t sec = 0;
    if (rd_regs(PCF85063_REG_SEC, &sec, 1) != ESP_OK) {
        return false;
    }
    if (sec & SEC_OS_MASK) {
        return false;   /* 振荡器曾停止（断电）→ 时间不可信 */
    }
    bsp_rtc_datetime_t dt;
    if (bsp_rtc_get_time(&dt) != ESP_OK) {
        return false;
    }
    /* 基本合理性:年份窗口 + 月日范围 */
    if (dt.year < 2024 || dt.year > 2099 ||
        dt.month < 1 || dt.month > 12 || dt.day < 1 || dt.day > 31) {
        return false;
    }
    return true;
}
