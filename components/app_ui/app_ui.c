/* app_ui — LVGL 界面层（架构 §10）
 * S1 横屏验证页：
 *   - 中央显示触摸坐标（校准触摸映射的关键：按屏幕四角与中央，核对坐标）
 *   - 移动方块动画（验证 DIRECT 局部刷新流畅度）
 *   - 「旋转」按钮切换 90/270（验证双向旋转 + 触摸映射随动）
 *   - 「事件」按钮验证事件总线链路（串口 log 可见收发）
 * 后续阶段本组件替换为正式页面栈/导航/主题。 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

#include "bsp_lcd.h"
#include "app_event.h"
#include "app_power.h"
#include "app_ui.h"

/* ============ 开机图（S2 素材接入点,当前 0=跳过）============
 * 图片规格：横屏视觉 640×172,RGB565,小端(little-endian) C 数组,
 * 约 214KB。转换工具任选（LVGL 官方 image converter 选 "C array"/RGB565,
 * 或 Image2Lcd 选 16 位真彩 RGB565/小端/C 语言数组）。
 * 接入步骤：素材转成 boot_logo.c（含 lv_image_dsc_t,cf=RGB565,w=640,h=172）,
 * BOOT_LOGO_ENABLE 置 1、BOOT_LOGO_IMG 指向该 dsc,开机显示 3s 后自动进入主界面。 */
#define BOOT_LOGO_ENABLE    0       /* 素材到位后置 1 */
#define BOOT_LOGO_IMG       NULL    /* const lv_image_dsc_t * */
#define BOOT_LOGO_SHOW_MS   3000

/* ============ S1.6 临时电量屏显（电压标定用,标定完成后置 0 即整体移除）============ */
#define S16_BATTERY_LABEL   1

static const char *TAG = "app_ui";

static lv_obj_t *s_coord_label;

/* 屏幕触摸事件：按下瞬间取一次坐标（逻辑+原始报文,S1.7 校准映射用）
 * 注意：不要在 PRESSING 持续更新——每次文本变化触发全帧刷新(顺序流写契约,
 * ≈65ms/帧),拖动会明显卡顿;点击式取数对校准完全够用且流畅 */
static void screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        uint16_t tx, ty;
        bsp_lcd_touch_get_raw(&tx, &ty);
        lv_label_set_text_fmt(s_coord_label, "L(%d,%d) R(%d,%d)", (int)p.x, (int)p.y, (int)tx, (int)ty);
    }
}

/* 旋转切换按钮 */
static void rot_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_display_rotation_t cur = lv_display_get_rotation(lv_display_get_default());
    lv_display_rotation_t next = (cur == LV_DISPLAY_ROTATION_90) ? LV_DISPLAY_ROTATION_270
                                                                 : LV_DISPLAY_ROTATION_90;
    bsp_lcd_set_rotation(next);
    /* 同步广播方向事件（S1.5 起 app_motion 是该事件的生产者,此处模拟） */
    app_event_post(LL_EV_ORIENTATION, &next, sizeof(next), 0);
}

/* 事件订阅示例（验证总线：收到后在串口打印） */
static void on_orientation(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id);
    ESP_LOGI(TAG, "event bus OK: ORIENTATION -> %d", *(int *)data);
}

#if S16_BATTERY_LABEL
/* S1.6 临时:订阅电量事件,屏显电压/百分比/充电态（标定用,见顶部开关） */
static lv_obj_t *s_batt_label;

static void on_battery(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id);
    const ll_battery_state_t *st = (const ll_battery_state_t *)data;
    if (st == NULL) return;
    /* 事件在 app_power 任务上下文回调,操作 LVGL 须持锁 */
    if (bsp_lcd_lock(200)) {
        lv_label_set_text_fmt(s_batt_label, "%s%umV %u%%%s",
                              (st->vbat_mv <= 3300) ? "LOW! " : "",
                              (unsigned)st->vbat_mv, (unsigned)st->percent,
                              st->charging ? " CHG" : "");
        bsp_lcd_unlock();
    }
}
#endif

/* 开机图显示（须持 LVGL 锁调用）：立绘一帧 → 让出锁延时（不饿死 LVGL
 * 刷新任务）→ 重新持锁返回。调用方语义不变（进入/离开均持锁） */
static void show_boot_logo(void)
{
#if BOOT_LOGO_ENABLE
    lv_obj_t *im = lv_image_create(lv_screen_active());
    lv_image_set_src(im, BOOT_LOGO_IMG);
    lv_obj_center(im);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_refr_now(NULL);                       /* 立即渲染一帧显示开机图 */

    bsp_lcd_unlock();
    vTaskDelay(pdMS_TO_TICKS(BOOT_LOGO_SHOW_MS));
    bsp_lcd_lock(-1);

    lv_obj_delete(im);
    lv_obj_invalidate(lv_screen_active());   /* 清残影,进入主界面 */
#endif
}

esp_err_t app_ui_init(void)
{
    show_boot_logo();

    /* ============ S1 方向诊断页（临时）============
     * 逻辑屏四角色块:左上红 右上绿 左下蓝 右下黄 + 中央白圆 + TOP↑
     * 横拿板子拍照,即可一次确定正确的旋转方向与触摸映射。 */
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    /* 禁止屏幕滚动：诊断页无滚动需求;手指微动会被判定为拖动 → 滚动/回弹动画
     * 连续触发全帧刷新(顺序流写契约 ≈65ms/帧) → 交互卡顿 */
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 四角色块（各 48×36） */
    static const struct { lv_align_t align; uint32_t color; } corners[4] = {
        {LV_ALIGN_TOP_LEFT,     0xE53935},  /* 左上 红 */
        {LV_ALIGN_TOP_RIGHT,    0x43A047},  /* 右上 绿 */
        {LV_ALIGN_BOTTOM_LEFT,  0x1E88E5},  /* 左下 蓝 */
        {LV_ALIGN_BOTTOM_RIGHT, 0xFDD835},  /* 右下 黄 */
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = lv_obj_create(screen);
        lv_obj_set_size(c, 48, 36);
        lv_obj_set_align(c, corners[i].align);
        lv_obj_set_style_bg_color(c, lv_color_hex(corners[i].color), 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_radius(c, 0, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_EVENT_BUBBLE);   /* 事件冒泡:点击控件也更新坐标 label */
    }

    /* 中央白圆 */
    lv_obj_t *circle = lv_obj_create(screen);
    lv_obj_set_size(circle, 40, 40);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_white(), 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(circle, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* TOP↑ 大字（顶部居中,定义逻辑"上"） */
    lv_obj_t *top = lv_label_create(screen);
    lv_label_set_text(top, "TOP ^");
    lv_obj_set_style_text_font(top, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(top, lv_color_white(), 0);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 40);

    /* 触摸坐标（点击瞬间更新一次） */
    s_coord_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_coord_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_coord_label, lv_color_hex(0xB0BEC5), 0);
    lv_label_set_text(s_coord_label, "touch: --");
    lv_obj_align(s_coord_label, LV_ALIGN_BOTTOM_MID, 0, -56);

    /* 旋转切换按钮（一次烧录可测两个方向;放大便于点击） */
    lv_obj_t *btn_rot = lv_button_create(screen);
    lv_obj_set_size(btn_rot, 180, 48);
    lv_obj_align(btn_rot, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(btn_rot, rot_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab1 = lv_label_create(btn_rot);
    lv_label_set_text(lab1, "ROT 90/270");
    lv_obj_center(lab1);

    /* 屏幕（全局）触摸事件 */
    lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_ALL, NULL);
    /* 订阅事件（验证链路） */
    app_event_subscribe(LL_EV_ORIENTATION, on_orientation, NULL);

#if S16_BATTERY_LABEL
    /* S1.6 临时:左上角红块右侧显示电池电压/百分比/充电态（标定用） */
    s_batt_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_batt_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_batt_label, lv_color_hex(0xB0BEC5), 0);
    lv_label_set_text(s_batt_label, "----mV");
    lv_obj_align(s_batt_label, LV_ALIGN_TOP_LEFT, 56, 8);
    app_event_subscribe(LL_EV_BATTERY_STATE, on_battery, NULL);
#endif

    ESP_LOGI(TAG, "UI orientation diagnose page ready");
    return ESP_OK;
}
