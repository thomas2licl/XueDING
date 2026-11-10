/* bsp_lcd — 显示子系统（QSPI AXS15231B + 触摸 + LVGL 移植层）
 *
 * 横屏 + 局部刷新（S1.2/S1.3）：
 * - LVGL 逻辑分辨率 640×172（set_rotation 90/270 切换左右横），面板物理 172×640
 * - DIRECT 双全帧 PSRAM 渲染缓冲（逻辑布局 640×172）
 * - flush 仅对脏区：字节序交换 → lv_draw_sw_rotate 旋转脏区 → 按条带 DMA 发送
 * - 触摸原始坐标为横向（x∈[0,640] 沿长边, y∈[0,172]），按当前旋转方向映射到逻辑坐标
 *   （映射方向待真机校准，见 TOUCH_MAP_* 注释） */

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

/* 面板物理分辨率（172×640 竖条） */
#define PANEL_HOR_RES  EXAMPLE_LCD_H_RES   /* 172 */
#define PANEL_VER_RES  EXAMPLE_LCD_V_RES   /* 640 */
/* 横屏逻辑分辨率 */
#define DISP_HOR_RES   EXAMPLE_LCD_V_RES   /* 640 */
#define DISP_VER_RES   EXAMPLE_LCD_H_RES   /* 172 */

#define LCD_BIT_PER_PIXEL 16
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUFF_SIZE (DISP_HOR_RES * DISP_VER_RES * BYTES_PER_PIXEL)   /* = 172*640*2 */
#define DISP_STRIDE (DISP_HOR_RES * BYTES_PER_PIXEL)                /* 逻辑行距 1280B */

/* 默认横屏方向：90（左横）。app_motion 方向自适应会运行时切换 */
#define DEFAULT_ROTATION LV_DISPLAY_ROTATION_90

#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 10
#define LVGL_TASK_STACK_SIZE   (8 * 1024)
#define LVGL_TASK_PRIORITY     2
#define LVGL_TASK_CORE         0

static SemaphoreHandle_t s_lvgl_mux = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static uint8_t *s_rot_buf = NULL;         /* 脏区旋转中转（物理取向，最大全屏） */
static uint16_t *s_trans_buf = NULL;      /* DMA 内部 RAM 中转缓冲 */
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_rotation_t s_rotation = DEFAULT_ROTATION;

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

/* DIRECT 模式 flush：仅处理脏区（S1.3 局部刷新）
 * 步骤：脏区字节交换 → 旋转到物理取向 s_rot_buf → 按条带 DMA 只发脏区 */
static void bsp_lcd_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * color_p)
{
    const lv_color_format_t cf = lv_display_get_color_format(disp);
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    if (w <= 0 || h <= 0) {
        lv_display_flush_ready(disp);
        return;
    }

    /* direct 模式下 color_p 为全帧基址，按逻辑布局定位脏区首像素 */
    uint16_t *src = (uint16_t *)(color_p + (area->y1 * DISP_STRIDE) + (area->x1 * BYTES_PER_PIXEL));

    /* 1) RGB565 大端交换（仅脏区逐行） */
    for (int32_t r = 0; r < h; r++) {
        lv_draw_sw_rgb565_swap(src + r * DISP_HOR_RES, w);
    }

    /* 2) 逻辑脏区 → 物理脏区（旋转） */
    lv_area_t pa = *area;
    lv_display_rotate_area(disp, &pa);
    const int32_t pw = lv_area_get_width(&pa);    /* 物理脏区宽（≤640） */

    lv_draw_sw_rotate(src, s_rot_buf, w, h, DISP_STRIDE, pw * BYTES_PER_PIXEL, s_rotation, cf);

    /* 3) 分条带 DMA 发送物理脏区（条带行数受 DMA 缓冲限制） */
    int32_t x1 = pa.x1, x2 = pa.x2, y1 = pa.y1, y2 = pa.y2;
    int32_t max_rows = LVGL_DMA_BUFF_LEN / (pw * BYTES_PER_PIXEL);
    if (max_rows < 1) max_rows = 1;
    uint16_t *map = (uint16_t *)s_rot_buf;

    xSemaphoreGive(s_flush_done_sem);   /* 抵消首块 Take（沿用厂商信号量节奏） */
    while (y1 <= y2) {
        int32_t rows = y2 - y1 + 1;
        if (rows > max_rows) rows = max_rows;
        xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
        memcpy(s_trans_buf, map, rows * pw * BYTES_PER_PIXEL);
        esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y1 + rows - 1, s_trans_buf);
        y1 += rows;
        map += rows * pw;
    }
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);   /* 等最后一块 DMA 完成 */
    lv_display_flush_ready(disp);
}

/* 触摸：原始坐标为横向（tx 沿 640 长边, ty 沿 172 短边），按旋转方向映射到逻辑坐标
 * 映射与 lv_display_rotate_area 保持互逆（90: x=640-1-tx, y=ty; 270: x=tx, y=172-1-ty）
 * 注：若真机实测方向不符，仅需交换/取反此处的映射式 */
static void bsp_lcd_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * indev_data)
{
    uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buff[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_read_dev(disp_touch_dev_handle, read_touchpad_cmd, 11, buff, 32));
    uint16_t tx = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
    uint16_t ty = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];

    if (buff[1] > 0 && buff[1] < 5)
    {
        indev_data->state = LV_INDEV_STATE_PRESSED;
        if (tx > DISP_HOR_RES - 1) tx = DISP_HOR_RES - 1;
        if (ty > DISP_VER_RES - 1) ty = DISP_VER_RES - 1;
        if (s_rotation == LV_DISPLAY_ROTATION_90) {
            indev_data->point.x = (DISP_HOR_RES - 1) - tx;
            indev_data->point.y = ty;
        }
        else {  /* ROTATION_270（右横） */
            indev_data->point.x = tx;
            indev_data->point.y = (DISP_VER_RES - 1) - ty;
        }
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
    if (rot == s_rotation) return ESP_OK;
    if (rot != LV_DISPLAY_ROTATION_90 && rot != LV_DISPLAY_ROTATION_270) {
        return ESP_ERR_NOT_SUPPORTED;   /* 本产品只支持左右横两个方向 */
    }
    s_rotation = rot;
    lv_display_set_rotation(disp, rot);
    /* 旋转后布局改变，整屏重绘（调用方须持 LVGL 锁） */
    lv_obj_invalidate(lv_screen_active());
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

    /* LVGL 移植：物理面板 172×640 创建 + 旋转 90 → 逻辑横屏 640×172 */
    ESP_LOGI(TAG, "init LVGL (landscape %dx%d, rotation %d)", DISP_HOR_RES, DISP_VER_RES, (int)DEFAULT_ROTATION);
    lv_init();
    lv_display_t *disp = lv_display_create(PANEL_HOR_RES, PANEL_VER_RES);
    lv_display_set_flush_cb(disp, bsp_lcd_flush_cb);

    uint8_t *buffer_1 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(buffer_1);
    uint8_t *buffer_2 = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(buffer_2);
    s_rot_buf = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);   /* 脏区旋转中转，按最大全帧分配 */
    assert(s_rot_buf);
    s_trans_buf = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(s_trans_buf);

    /* DIRECT 双全帧缓冲：渲染直接画全帧，flush 只处理脏区 */
    lv_display_set_buffers(disp, buffer_1, buffer_2, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_user_data(disp, s_panel);
    lv_display_set_rotation(disp, s_rotation);

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
