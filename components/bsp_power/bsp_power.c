/* bsp_power — 电池/供电驱动（S1.6）
 * VBAT: 200K/100K 分压(1/3) → GPIO4(ADC1_CH3),衰减 6dB(量程 0~2.2V,
 *       VBAT/3 在 1.0~1.4V 全程线性;曾用 2.5dB 导致 >3.9V 段饱和钳制)
 * STAT: ETA6098 充电状态(开漏,充电中拉低),飞线 GPIO5,内部上拉
 * KEY1: GPIO16 电源键(按下拉低,硬件同时使能电池 MOS) */

#include "bsp_power.h"

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include "user_config.h"
#include "bsp_exio.h"

static const char *TAG = "bsp_power";

#define VBAT_DIV_RATIO       3      /* 分压 1/3,还原 ×3 */
#define VBAT_SAMPLE_CNT      32     /* 均值滤波采样次数 */
/* S1.6.2 标定系数（千分比）：可调电源九点实测（4.15~3.30V,2026-11-13）,
 * 换 6dB 衰减后需复核微调。残余误差 ±12mV 量级,远小于电量档宽 */
#define VBAT_CAL_FACTOR      1008

static adc_oneshot_unit_handle_t s_adc1 = NULL;
static adc_cali_handle_t s_adc_cali = NULL;
static adc_channel_t s_vbat_chan = 0;

esp_err_t bsp_power_init(void)
{
    if (s_adc1 != NULL) {
        return ESP_OK;
    }

    /* VBAT ADC：GPIO4 → ADC1_CH3,衰减 6dB（量程 0~2.2V,分压后 1.0~1.4V
     * 全程线性;eFuse curve-fitting 出厂校准将 raw 转 mV） */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc1));

    adc_unit_t vbat_unit = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_VBAT_ADC, &vbat_unit, &s_vbat_chan));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, s_vbat_chan, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali));

    /* STAT(开漏,内部上拉) 与 KEY1(按下拉低,内部上拉) */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PIN_CHG_STAT) | (1ULL << PIN_KEY_PWR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_cfg));

    ESP_LOGI(TAG, "power hw ready: vbat=GPIO%d(div 1/3), stat=GPIO%d, key=GPIO%d",
             PIN_VBAT_ADC, PIN_CHG_STAT, PIN_KEY_PWR);
    return ESP_OK;
}

esp_err_t bsp_power_read_vbat_mv(uint16_t *vbat_mv)
{
    if (vbat_mv == NULL) return ESP_ERR_INVALID_ARG;

    uint32_t sum_mv = 0;
    for (int i = 0; i < VBAT_SAMPLE_CNT; i++) {
        int raw = 0;
        int mv = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc1, s_vbat_chan, &raw));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_adc_cali, raw, &mv));
        sum_mv += (uint32_t)mv;
    }
    /* 均值 × 分压比 × 标定系数（整数运算,四舍五入） */
    const uint32_t raw_mv = (sum_mv / VBAT_SAMPLE_CNT) * VBAT_DIV_RATIO;
    *vbat_mv = (uint16_t)((raw_mv * VBAT_CAL_FACTOR + 500) / 1000);
    return ESP_OK;
}

bool bsp_power_is_charging(void)
{
    /* 开漏低 = 充电中（ETA6098 datasheet:STAT low when charging） */
    return gpio_get_level(PIN_CHG_STAT) == 0;
}

bool bsp_power_is_key_pressed(void)
{
    return gpio_get_level(PIN_KEY_PWR) == 0;
}

esp_err_t bsp_power_shutdown(void)
{
    ESP_LOGW(TAG, "shutdown: releasing SYS_EN");
    return bsp_exio_set_sys_en(0);
}
