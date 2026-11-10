#include "bsp_exio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "user_config.h"
#include "i2c_bsp.h"

static const char *TAG = "bsp_exio";

static esp_io_expander_handle_t s_io_expander = NULL;

esp_err_t bsp_exio_init(void)
{
    if (s_io_expander != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_master_get_bus_handle(0, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get i2c bus0 handle failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_io_expander));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(s_io_expander, EXAMPLE_EXIO_PIN_TOUCH_INT, IO_EXPANDER_INPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(s_io_expander,
                                            EXAMPLE_EXIO_PIN_BL_EN | EXAMPLE_EXIO_PIN_LCD_RST |
                                            EXAMPLE_EXIO_PIN_SYS_EN | EXAMPLE_EXIO_PIN_NS_MODE,
                                            IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, EXAMPLE_EXIO_PIN_BL_EN, 0));
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, EXAMPLE_EXIO_PIN_LCD_RST, 1));
    /* SYS_EN 拉高自锁保持供电（电池供电时防止上电即断电） */
    ESP_ERROR_CHECK(esp_io_expander_set_level(s_io_expander, EXAMPLE_EXIO_PIN_SYS_EN, 1));

    ESP_LOGI(TAG, "TCA9554 exio init done");
    return ESP_OK;
}

esp_io_expander_handle_t bsp_exio_get_handle(void)
{
    return s_io_expander;
}

esp_err_t bsp_exio_set_lcd_rst(int level)
{
    return esp_io_expander_set_level(s_io_expander, EXAMPLE_EXIO_PIN_LCD_RST, level);
}

esp_err_t bsp_exio_set_bl_en(int enable)
{
    return esp_io_expander_set_level(s_io_expander, EXAMPLE_EXIO_PIN_BL_EN, enable ? 1 : 0);
}

esp_err_t bsp_exio_set_sys_en(int level)
{
    return esp_io_expander_set_level(s_io_expander, EXAMPLE_EXIO_PIN_SYS_EN, level);
}

esp_err_t bsp_exio_set_ns_mode(int level)
{
    return esp_io_expander_set_level(s_io_expander, EXAMPLE_EXIO_PIN_NS_MODE, level);
}
