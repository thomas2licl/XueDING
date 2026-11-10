/* app_ui — LVGL 界面层（架构 §10）
 * S1 横屏验证页：
 *   - 中央显示触摸坐标（校准触摸映射的关键：按屏幕四角与中央，核对坐标）
 *   - 移动方块动画（验证 DIRECT 局部刷新流畅度）
 *   - 「旋转」按钮切换 90/270（验证双向旋转 + 触摸映射随动）
 *   - 「事件」按钮验证事件总线链路（串口 log 可见收发）
 * 后续阶段本组件替换为正式页面栈/导航/主题。 */

#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"

#include "bsp_lcd.h"
#include "app_event.h"
#include "app_ui.h"

static const char *TAG = "app_ui";

static lv_obj_t *s_coord_label;
static lv_obj_t *s_rot_label;
static lv_obj_t *s_mover;

/* 方块动画：每 30ms 移动一次,横向往返（局部刷新流畅度验证） */
static void mover_timer_cb(lv_timer_t *t)
{
    static int32_t x = 0;
    static int8_t dir = 1;
    x += dir * 8;
    if (x > 560) dir = -1;
    if (x < 0)   dir = 1;
    lv_obj_set_pos(s_mover, x, 120);
}

/* 屏幕触摸事件：实时显示逻辑坐标 */
static void screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *screen = lv_event_get_target(e);
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESSED) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        lv_label_set_text_fmt(s_coord_label, "触摸: (%d, %d)", (int)p.x, (int)p.y);
    }
    else if (code == LV_EVENT_RELEASED) {
        lv_label_set_text(s_coord_label, "触摸: 松开");
    }
    LV_UNUSED(screen);
}

/* 旋转切换按钮 */
static void rot_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_display_rotation_t cur = lv_display_get_rotation(lv_display_get_default());
    lv_display_rotation_t next = (cur == LV_DISPLAY_ROTATION_90) ? LV_DISPLAY_ROTATION_270
                                                                 : LV_DISPLAY_ROTATION_90;
    bsp_lcd_set_rotation(next);
    lv_label_set_text(s_rot_label, next == LV_DISPLAY_ROTATION_90 ? "方向: 90 (左横)" : "方向: 270 (右横)");
    /* 同步广播方向事件（S1.5 起 app_motion 是该事件的生产者,此处模拟） */
    app_event_post(LL_EV_ORIENTATION, &next, sizeof(next), 0);
}

/* 事件链路验证按钮 */
static void evt_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t magic = 0x5AA5A5A5;
    app_event_post(LL_EV_SCENE_ENTER, &magic, sizeof(magic), 0);
}

/* 事件订阅示例（验证总线：收到后在串口打印） */
static void on_orientation(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id);
    ESP_LOGI(TAG, "event bus OK: ORIENTATION -> %d", *(int *)data);
}

esp_err_t app_ui_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "LocalLife  S1 骨架验证");
    lv_obj_set_style_text_color(title, lv_color_hex(0x4FC3F7), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* 触摸坐标（校准用） */
    s_coord_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_coord_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_coord_label, lv_color_white(), 0);
    lv_label_set_text(s_coord_label, "触摸: --");
    lv_obj_align(s_coord_label, LV_ALIGN_TOP_MID, 0, 34);

    /* 旋转方向显示 */
    s_rot_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_rot_label, lv_color_hex(0xB0BEC5), 0);
    lv_label_set_text(s_rot_label, "方向: 90 (左横)");
    lv_obj_align(s_rot_label, LV_ALIGN_TOP_MID, 0, 58);

    /* 局部刷新动画方块 */
    s_mover = lv_obj_create(screen);
    lv_obj_set_size(s_mover, 40, 24);
    lv_obj_set_style_bg_color(s_mover, lv_color_hex(0xFF7043), 0);
    lv_obj_set_style_border_width(s_mover, 0, 0);
    lv_obj_set_style_radius(s_mover, 6, 0);
    lv_obj_clear_flag(s_mover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_mover, 0, 120);
    lv_timer_create(mover_timer_cb, 30, NULL);

    /* 屏幕（全局）触摸事件 */
    lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_ALL, NULL);

    /* 旋转切换按钮 */
    lv_obj_t *btn_rot = lv_button_create(screen);
    lv_obj_set_size(btn_rot, 150, 44);
    lv_obj_align(btn_rot, LV_ALIGN_BOTTOM_LEFT, 16, -12);
    lv_obj_add_event_cb(btn_rot, rot_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab1 = lv_label_create(btn_rot);
    lv_label_set_text(lab1, "旋转 90/270");
    lv_obj_center(lab1);

    /* 事件验证按钮 */
    lv_obj_t *btn_evt = lv_button_create(screen);
    lv_obj_set_size(btn_evt, 150, 44);
    lv_obj_align(btn_evt, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
    lv_obj_add_event_cb(btn_evt, evt_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab2 = lv_label_create(btn_evt);
    lv_label_set_text(lab2, "事件总线测试");
    lv_obj_center(lab2);

    /* 订阅事件（验证链路） */
    app_event_subscribe(LL_EV_ORIENTATION, on_orientation, NULL);

    ESP_LOGI(TAG, "UI verify page ready (640x172 landscape)");
    return ESP_OK;
}
