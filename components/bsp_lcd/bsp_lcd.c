/* bsp_lcd — 显示子系统（QSPI AXS15231B + 触摸 + LVGL 移植层）
 * 自厂商例程 main.c 无损迁移（S1.1）。
 * 渲染模式：双全帧 PSRAM 缓冲 + full_refresh（S1.3 将改造为横屏 direct/局部刷新）。 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "esp_lcd_axs15231b.h"

#include "user_config.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "bsp_exio.h"
#include "bsp_lcd.h"

static const char *TAG = "bsp_lcd";

#define LCD_BIT_PER_PIXEL 16
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUFF_SIZE (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * BYTES_PER_PIXEL)

#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 10
#define LVGL_TASK_STACK_SIZE   (8 * 1024)
#define LVGL_TASK_PRIORITY     2
#define LVGL_TASK_CORE         0

static SemaphoreHandle_t s_lvgl_mux = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static uint8_t *s_rot_dest = NULL;        /* 旋转中转缓冲（Rotated 启用时） */
static uint16_t *s_trans_buf = NULL;      /* DMA 内部 RAM 中转缓冲 */
static esp_lcd_panel_handle_t s_panel = NULL;

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] =
{
    {0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 100},
};

/* ------------------------------------------------------------------ */
/* 面板回调与复位                                                       */
/* ------------------------------------------------------------------ */

static bool IRAM_ATTR bsp_lcd_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                             esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done_sem, &high_task_awoken);
    return false;
}

static void bsp_lcd_reset(void)
{
    ESP_ERROR_CHECK(bsp_exio_set_lcd_rst(1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(bsp_exio_set_lcd_rst(0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(bsp_exio_set_lcd_rst(1));
    vTaskDelay(pdMS_TO_TICKS(30));
}

/* ------------------------------------------------------------------ */
/* LVGL 移植                                                            */
/* ------------------------------------------------------------------ */

static void bsp_lcd_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * color_p)
{
    lv_draw_sw_rgb565_swap(color_p, lv_area_get_width(area) * lv_area_get_height(area));
#if (Rotated == USER_DISP_ROT_90)
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    lv_area_t rotated_area;
    if (rotation != LV_DISPLAY_ROTATION_0)
    {
        lv_color_format_t cf = lv_display_get_color_format(disp);
        /*Calculate the position of the rotated area*/
        rotated_area = *area;
        lv_display_rotate_area(disp, &rotated_area);
        /*Calculate the source stride (bytes in a line) from the width of the area*/
        uint32_t src_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), cf);
        /*Calculate the stride of the destination (rotated) area too*/
        uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&rotated_area), cf);

        int32_t src_w = lv_area_get_width(area);
        int32_t src_h = lv_area_get_height(area);
        lv_draw_sw_rotate(color_p, s_rot_dest, src_w, src_h, src_stride, dest_stride, rotation, cf);
        /*Use the rotated area and rotated buffer from now on*/
        area = &rotated_area;
    }

    const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
    const int offgap = (EXAMPLE_LCD_V_RES / flush_coun);
    const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
    int offsetx1 = 0;
    int offsety1 = 0;
    int offsetx2 = EXAMPLE_LCD_H_RES;
    int offsety2 = offgap;

    uint16_t *map = (uint16_t *)s_rot_dest;
    xSemaphoreGive(s_flush_done_sem);
    for (int i = 0; i < flush_coun; i++)
    {
        xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
        memcpy(s_trans_buf, map, LVGL_DMA_BUFF_LEN);
        esp_lcd_panel_draw_bitmap(s_panel, offsetx1, offsety1, offsetx2, offsety2, s_trans_buf);
        offsety1 += offgap;
        offsety2 += offgap;
        map += dmalen;
    }
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
    lv_display_flush_ready(disp);
#else
    const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
    const int offgap = (EXAMPLE_LCD_V_RES / flush_coun);
    const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
    int offsetx1 = 0;
    int offsety1 = 0;
    int offsetx2 = EXAMPLE_LCD_H_RES;
    int offsety2 = offgap;

    uint16_t *map = (uint16_t *)color_p;
    xSemaphoreGive(s_flush_done_sem);
    for (int i = 0; i < flush_coun; i++)
    {
        xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
        memcpy(s_trans_buf, map, LVGL_DMA_BUFF_LEN);
        esp_lcd_panel_draw_bitmap(s_panel, offsetx1, offsety1, offsetx2, offsety2, s_trans_buf);
        offsety1 += offgap;
        offsety2 += offgap;
        map += dmalen;
    }
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
    lv_display_flush_ready(disp);
#endif
}

static void bsp_lcd_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * indev_data)
{
    uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buff[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_read_dev(disp_touch_dev_handle, read_touchpad_cmd, 11, buff, 32));
    uint16_t pointX;
    uint16_t pointY;
    pointX = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
    pointY = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];
    if (buff[1] > 0 && buff[1] < 5)
    {
        indev_data->state = LV_INDEV_STATE_PRESSED;
#if (Rotated == USER_DISP_ROT_90)
        if (pointX > EXAMPLE_LCD_H_RES) pointX = EXAMPLE_LCD_H_RES;
        if (pointY > EXAMPLE_LCD_V_RES) pointY = EXAMPLE_LCD_V_RES;
        indev_data->point.x = (EXAMPLE_LCD_H_RES - pointX);
        indev_data->point.y = (EXAMPLE_LCD_V_RES - pointY);
#else
        if (pointX > EXAMPLE_LCD_V_RES) pointX = EXAMPLE_LCD_V_RES;
        if (pointY > EXAMPLE_LCD_H_RES) pointY = EXAMPLE_LCD_H_RES;
        indev_data->point.x = pointY;
        indev_data->point.y = (EXAMPLE_LCD_V_RES - pointX);
#endif
    }
    else
    {
        indev_data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void bsp_lcd_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void bsp_lcd_port_task(void *arg)
{
    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    for (;;)
    {
        if (bsp_lcd_lock(-1))
        {
            task_delay_ms = lv_timer_handler();
            bsp_lcd_unlock();
        }
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/* 背光循环测试任务（厂商调试功能，Backlight_Testing=0 时空转保留） */
static void bsp_lcd_backlight_loop_task(void *arg)
{
    for (;;)
    {
#if (Backlight_Testing == true)
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_255);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_175);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_125);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_0);
#else
        vTaskDelay(pdMS_TO_TICKS(2000));
#endif
    }
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                             */
/* ------------------------------------------------------------------ */

bool bsp_lcd_lock(int timeout_ms)
{
    assert(s_lvgl_mux && "bsp_lcd_init must be called first");
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, timeout_ticks) == pdTRUE;
}

void bsp_lcd_unlock(void)
{
    assert(s_lvgl_mux && "bsp_lcd_init must be called first");
    xSemaphoreGive(s_lvgl_mux);
}

esp_err_t bsp_lcd_set_rotation(lv_display_rotation_t rot)
{
    lv_display_t *disp = lv_display_get_default();
    if (disp == NULL) return ESP_ERR_INVALID_STATE;
    lv_display_set_rotation(disp, rot);
    return ESP_OK;
}

esp_err_t bsp_lcd_init(void)
{
    ESP_LOGI(TAG, "init backlight pwm");
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

    s_flush_done_sem = xSemaphoreCreateBinary();
    assert(s_flush_done_sem);

    ESP_LOGI(TAG, "init SPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
    buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
    buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "install panel IO");
    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = bsp_lcd_flush_ready_cb;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));

    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config = &vendor_config;

    ESP_LOGI(TAG, "install panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &s_panel));

    bsp_lcd_reset();
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* LVGL 移植 */
    ESP_LOGI(TAG, "init LVGL");
    lv_init();
    lv_display_t *disp = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    lv_display_set_flush_cb(disp, bsp_lcd_flush_cb);

    uint8_t *buffer_1 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(buffer_1);
    uint8_t *buffer_2 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(buffer_2);
    s_trans_buf = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(s_trans_buf);
    lv_display_set_buffers(disp, buffer_1, buffer_2, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(disp, s_panel);
#if (Rotated == USER_DISP_ROT_90)
    s_rot_dest = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(s_rot_dest);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
#endif

    /* 触摸输入设备 */
    lv_indev_t *touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, bsp_lcd_touch_read_cb);

    /* LVGL tick */
    esp_timer_create_args_t tick_args = {};
    tick_args.callback = &bsp_lcd_tick_cb;
    tick_args.name = "lvgl_tick";
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    s_lvgl_mux = xSemaphoreCreateMutex();
    assert(s_lvgl_mux);

    xTaskCreatePinnedToCore(bsp_lcd_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, LVGL_TASK_CORE);
    xTaskCreatePinnedToCore(bsp_lcd_backlight_loop_task, "bl_loop", 4 * 1024, NULL, 2, NULL, LVGL_TASK_CORE);

    bsp_exio_set_bl_en(true);
    ESP_LOGI(TAG, "bsp_lcd init done");
    return ESP_OK;
}
