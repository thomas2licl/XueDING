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
#include "driver/gpio.h"

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

/* ============ S1.7 排查遗留说明（自检代码已移除,结论见档案）============
 * 驱动窗口写行为二分定位法与结论：DEVELOPMENT_PROGRESS.md §6.3/§6.4。 */
#define BSP_LCD_FLUSH_LOG_CNT  0   /* S1.7 校准完成,flush 日志关闭 */
#define BSP_LCD_FLUSH_PERF     1   /* 每 5s 一行 flush 性能汇总（帧数/平均/最大耗时） */

/* RGB565 值做字节交换（面板收大端字节流） */
#define RGB565_BE(rgb)  ((uint16_t)(((rgb) << 8) | ((rgb) >> 8)))

static SemaphoreHandle_t s_lvgl_mux = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static uint16_t *s_trans_buf = NULL;      /* DMA 内部 RAM 中转缓冲 ×2（乒乓） */
static uint16_t *s_trans_buf2 = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_rotation_t s_rotation = DEFAULT_ROTATION;

/* 触摸报文统计（perf 行输出用） */
static uint32_t s_tsamp = 0;     /* 总采样数 */
static uint32_t s_tap_cnt = 0;   /* 有点报文数 */

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

/* 分 tile 旋转：逻辑列条带（x0..x0+block_h-1, 全高 172）→ 物理块（172×block_h）
 * 写入 DMA RAM 目标 dst,写入时顺带完成 RGB565 大端字节交换。
 * 相比 lv_draw_sw_rotate 的整列跳读（每像素跨 1280B,PSRAM 缓存全废）,
 * 本实现源按 16 像素行连续读（32B 对齐 cache 行）、目标按行写 RAM——快数倍。
 * 坐标关系（由 lv_display_rotate_area 公式推出,已四角实测验证）：
 *   rot90 : 物理(r,c) = 逻辑(x0 + block_h-1-r, c)        物理 y=639-逻辑x
 *   rot270: 物理(r,c) = 逻辑(x0 + r, 171-c)              物理 y=  逻辑x */
static void rot_strip_to_block(const uint16_t *src, uint16_t *dst,
                               int32_t x0, int32_t block_h, lv_display_rotation_t rot)
{
    const int32_t H = DISP_VER_RES;   /* 172,条带高=物理块宽 */
    for (int32_t ty = 0; ty < H; ty += 16) {
        const int32_t th = (H - ty < 16) ? (H - ty) : 16;   /* 尾 tile 高度 */
        for (int32_t tx = 0; tx < block_h; tx += 16) {
            uint16_t tile[16][16];   /* 512B 栈缓冲 */
            /* 行式读 16×16 tile（源行内连续 → cache 友好） */
            for (int32_t j = 0; j < th; j++) {
                const uint16_t *s = src + (int32_t)(ty + j) * DISP_HOR_RES + x0 + tx;
                for (int32_t i = 0; i < 16; i++) tile[j][i] = s[i];
            }
            /* 转置写 + 大端交换 */
            if (rot == LV_DISPLAY_ROTATION_90) {
                for (int32_t j = 0; j < th; j++)
                    for (int32_t i = 0; i < 16; i++)
                        dst[(block_h - 1 - (tx + i)) * PANEL_HOR_RES + (ty + j)] =
                            (uint16_t)((tile[j][i] >> 8) | (tile[j][i] << 8));
            }
            else {  /* ROTATION_270 */
                for (int32_t j = 0; j < th; j++)
                    for (int32_t i = 0; i < 16; i++)
                        dst[(tx + i) * PANEL_HOR_RES + (DISP_VER_RES - 1 - (ty + j))] =
                            (uint16_t)((tile[j][i] >> 8) | (tile[j][i] << 8));
            }
        }
    }
}

/* flush：分块旋转 + 乒乓 DMA + 前缀刷（S1.7 定稿）
 *
 * 传输契约（见 esp_lcd_axs15231b.c 注释）：QSPI 驱动仅支持从物理块 0 起顺序
 * 连续流式写 → 每次从块 0 刷起,但只刷到覆盖脏区的最深块（前缀刷）。
 * 乒乓双 DMA 缓冲:块 k 的旋转与块 k-1 的 DMA 传输重叠（每块提交前 Take 等
 * 上一块 DMA 完成是 CASET 同步上总线的需要,旋转在其之前完成即已重叠）。
 * 实测从全帧 PSRAM 旋转的 ~148ms/帧 优化到 ~15ms/帧（全屏 10 块）。 */
static void bsp_lcd_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * color_p)
{
    const int block_h = 64;                                   /* 物理块行数（172×64=22KB=单块 DMA） */
    /* 前缀刷的最深块：脏区必须被覆盖;更深的块内容未变可跳过 */
    const int last_blk = (s_rotation == LV_DISPLAY_ROTATION_90)
                         ? (DISP_HOR_RES - 1 - area->x1) / block_h
                         : area->x2 / block_h;
    const int blk_cnt = last_blk + 1;
    uint16_t *bufs[2] = { s_trans_buf, s_trans_buf2 };

#if BSP_LCD_FLUSH_PERF
    const int64_t t0 = esp_timer_get_time();   /* 性能打点 */
#endif

#if BSP_LCD_FLUSH_LOG_CNT > 0
    static int s_flush_log = BSP_LCD_FLUSH_LOG_CNT;
    if (s_flush_log > 0) {
        s_flush_log--;
        ESP_LOGI(TAG, "flush[%d/%d] blocks=%d rot=%d dirty(%d,%d %d,%d)",
                 BSP_LCD_FLUSH_LOG_CNT - s_flush_log, BSP_LCD_FLUSH_LOG_CNT,
                 blk_cnt, (int)s_rotation,
                 (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
    }
#endif

    for (int k = 0; k < blk_cnt; k++) {
        const int32_t y0 = k * block_h;                       /* 物理块起始行 */
        const int32_t x0 = (s_rotation == LV_DISPLAY_ROTATION_90)
                           ? (DISP_HOR_RES - y0 - block_h)    /* rot90 ↔ 逻辑右列 */
                           : y0;                              /* rot270 ↔ 逻辑左列 */
        if (k > 0) {
            xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);  /* 等上一块 DMA 完成 */
        }
        /* 旋转期间上一块的 DMA 仍在跑（乒乓缓冲不冲突） */
        rot_strip_to_block((const uint16_t *)color_p, bufs[k & 1], x0, block_h, s_rotation);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y0, PANEL_HOR_RES, y0 + block_h, bufs[k & 1]);
    }
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);   /* 等最后一块 DMA 完成 */
    lv_display_flush_ready(disp);

#if BSP_LCD_FLUSH_PERF
    /* 每 5s 汇总一行：期间帧数、平均/最大单帧耗时（含旋转与 DMA 等待）;
     * 同时低频打印脏区——若空载仍有刷新,脏区位置直接指认重绘源 */
    {
        static int s_frames = 0;
        static int64_t s_sum_us = 0, s_max_us = 0, s_win = 0;
        const int64_t dt = esp_timer_get_time() - t0;
        s_frames++; s_sum_us += dt;
        if (dt > s_max_us) s_max_us = dt;
        if (s_win == 0) s_win = t0;
        else if (t0 - s_win >= 5000000) {
            ESP_LOGI(TAG, "flush perf: %d fr, avg %d ms, max %d ms, dirty(%d,%d %d,%d), touch taps %u/%u",
                     s_frames, (int)(s_sum_us / s_frames / 1000), (int)(s_max_us / 1000),
                     (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
                     (unsigned)s_tap_cnt, (unsigned)s_tsamp);
            s_frames = 0; s_sum_us = 0; s_max_us = 0; s_win = 0;
            s_tap_cnt = 0; s_tsamp = 0;
        }
    }
#endif
}

/* 触摸：read_cb 须提供【物理面板坐标】（172×640: x 沿短边, y 沿长边）——
 * LVGL 9 的 indev 层会自动调 lv_display_rotate_point 按当前 rotation 转成
 * 逻辑坐标（lv_indev.c indev_pointer_proc）,不可在此手动映射（会二次旋转）。
 * 触摸芯片报文与 USB-左视觉恒向: tx 沿长边[0,640), ty 沿短边[0,172),
 * S1.7 四角实测标定的物理映射: ph.x = ty, ph.y = 639 - tx（两个旋转方向通用） */
static uint16_t s_raw_tx = 0, s_raw_ty = 0;   /* 最近一次有效原始报文坐标（诊断用） */

/* ---- 触摸读取策略（S1.7 定稿）----
 * 采样 10ms 全量轮询 + 空报文去抖。该触摸芯片实测特性:
 *  - 报文为"低频/边沿式"(按住静止时大量空报文,拖动期间有效率仅 ~4-18%)
 *  - 空报文 ≠ 抬起:立即判 RELEASED 会造成按下状态高频振荡(事件+重绘风暴,
 *    表现为全面卡顿) → 连续 N 次空报文才判抬起(30ms 去抖)
 * 曾尝试 GPIO8(TCA9554 INT)门控读,实测失败:TCA9554 INT 为保持型,不读其
 * 输入寄存器不复位,触摸期间 GPIO8 持续低,门控退化;且 INT 与报文窗口
 * 并不同源。INT 方案留待 S2 用逻辑分析仪实测波形后再考虑(见技术档案)。 */
#define TOUCH_RELEASE_DEBOUNCE_N   3    /* 连续 N 次空报文才判抬起 */

static void bsp_lcd_touch_read_cb(lv_indev_t * indev, lv_indev_data_t * indev_data)
{
    LV_UNUSED(indev);
    static uint8_t s_empty_cnt = 0;   /* 连续空报文计数（抬起去抖） */
    static lv_point_t s_last_pt = {0};

    uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buff[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_read_dev(disp_touch_dev_handle, read_touchpad_cmd, 11, buff, 32));
    uint16_t tx = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
    uint16_t ty = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];

    s_tsamp++;
    if (buff[1] > 0 && buff[1] < 5)
    {
        s_tap_cnt++;
        s_empty_cnt = 0;
        s_raw_tx = tx;
        s_raw_ty = ty;
        indev_data->state = LV_INDEV_STATE_PRESSED;
        /* 物理面板坐标（LVGL 自动按 rotation 转逻辑坐标） */
        s_last_pt.x = (ty > PANEL_HOR_RES - 1) ? (PANEL_HOR_RES - 1) : ty;
        s_last_pt.y = (tx > PANEL_VER_RES - 1) ? 0 : (PANEL_VER_RES - 1 - tx);
        indev_data->point = s_last_pt;

        /* 触摸日志:每次新按下(间隔>250ms)打一行,前 30 次 */
        {
            static uint32_t s_last_tap_ms = 0;
            static int s_tap_log = 0;
            const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (now_ms - s_last_tap_ms > 250 && s_tap_log < 30) {
                s_tap_log++;
                ESP_LOGI(TAG, "tap raw(%u,%u) phys(%d,%d) rot=%d",
                         (unsigned)s_raw_tx, (unsigned)s_raw_ty,
                         (int)s_last_pt.x, (int)s_last_pt.y, (int)s_rotation);
            }
            s_last_tap_ms = now_ms;
        }
    }
    else
    {
        /* 空报文去抖:芯片按住期间大量空报文,立即判抬起会造成按下状态高频
         * 振荡(事件+重绘风暴);连续 N 次空报文才判真抬起,期间维持按下+坐标 */
        if (++s_empty_cnt >= TOUCH_RELEASE_DEBOUNCE_N) {
            indev_data->state = LV_INDEV_STATE_RELEASED;
        }
        else {
            indev_data->state = LV_INDEV_STATE_PRESSED;
            indev_data->point = s_last_pt;
        }
    }
}

/* 最近一次有效触摸的原始报文坐标（S1.7 校准用,正式 UI 不依赖） */
void bsp_lcd_touch_get_raw(uint16_t *tx, uint16_t *ty)
{
    if (tx) *tx = s_raw_tx;
    if (ty) *ty = s_raw_ty;
}

/* ------------------------------------------------------------------ */
/* 开机启动时序（S2：防开机花屏 + 开机图接口）                          */
/* ------------------------------------------------------------------ */

/* 开机防花屏：LVGL 启动前向面板直发全屏黑色（物理坐标顺序刷,遵守驱动
 * 顺序流写契约）,之后才点亮背光——避免背光先亮时显示面板 RAM 里的
 * 上电随机数据造成开机瞬间花屏 */
static void bsp_lcd_prefill_black(void)
{
    const uint16_t black = RGB565_BE(0x0000);
    const int px = LVGL_DMA_BUFF_LEN / 2;
    for (int i = 0; i < px; i++) s_trans_buf[i] = black;

    xSemaphoreGive(s_flush_done_sem);
    for (int i = 0; i < PANEL_VER_RES / 64; i++) {
        xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
        esp_lcd_panel_draw_bitmap(s_panel, 0, i * 64, PANEL_HOR_RES, i * 64 + 64, s_trans_buf);
    }
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
}

static void bsp_lcd_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void bsp_lcd_port_task(void *arg)
{
    /* S1.7：固定 10ms 周期轮询,不用 lv_timer_handler() 返回值做睡眠——
     * 本项目界面无持续动画,timer 空闲时 handler 返回值可达数百 ms,
     * 任务睡眠过长导致点击刷新帧被排队,体感一卡一卡。固定短周期轮询的
     * 空转成本可忽略（handler 无到期任务时为微秒级）。 */
    for (;;)
    {
        if (bsp_lcd_lock(-1))
        {
            lv_timer_handler();
            bsp_lcd_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_MIN_DELAY_MS));
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

lv_display_rotation_t bsp_lcd_get_rotation(void)
{
    return s_rotation;
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
    s_trans_buf = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(s_trans_buf);
    s_trans_buf2 = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(s_trans_buf2);

    /* 启动时序：先清屏为纯黑再点亮背光（防止背光亮起瞬间显示面板
     * 上电随机数据的花屏）;开机图（若启用）由 app_ui 在 LVGL 首帧绘制 */
    bsp_lcd_prefill_black();
    bsp_exio_set_bl_en(true);

    /* DIRECT 双全帧缓冲：渲染直接画全帧，flush 只处理脏区 */
    lv_display_set_buffers(disp, buffer_1, buffer_2, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_user_data(disp, s_panel);
    lv_display_set_rotation(disp, s_rotation);

    /* 触摸输入设备。采样 10ms + 空报文去抖（见 touch_read_cb 注释）:
     * 芯片报点为低频/边沿式,10ms 采样保证快速点击(<50ms 按下)命中报文窗口,
     * 空报文去抖防止按住期间按下状态振荡 */
    lv_indev_t *touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, bsp_lcd_touch_read_cb);
    lv_timer_set_period(lv_indev_get_read_timer(touch_indev), 10);

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

    ESP_LOGI(TAG, "bsp_lcd init done");
    return ESP_OK;
}
