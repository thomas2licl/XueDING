/* app_ui_nav — 横滑页面导航框架实现（U1）。机制说明见 app_ui_nav.h 头注释。 */

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "app_ui_nav.h"
#include "bsp_lcd.h"

/* 中文字体（U2,fonts/ 目录）——占位页页面名显示用 */
LV_FONT_DECLARE(noto_sans_sc_24);

static const char *TAG = "app_ui_nav";

/* 视觉规范（XueDING 交互规范 V1.1 §三） */
#define COL_BG_PAGE     0x080A0E     /* 主背景 */
#define COL_BRAND       0x3988E8     /* 品牌蓝（选中） */
#define COL_DIM         0xA0A9B6     /* 次级文字 */
#define COL_DOT_IDLE    0x626D7B     /* 未选中页点 */

static const ui_page_def_t *s_pages = NULL;
static int s_count = 0;
static lv_obj_t *s_scr[UI_PAGE_COUNT];
static lv_obj_t *s_dots[UI_PAGE_COUNT];   /* 页点指示器（top layer） */
static int s_cur = -1;
static bool s_anim_busy = false;

/* ------------------------------------------------------------------ */
/* 内部：切页                                                           */
/* ------------------------------------------------------------------ */
static void anim_busy_clear_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    s_anim_busy = false;
}

static void dots_update(void)
{
    for (int i = 0; i < s_count; i++) {
        lv_obj_set_style_bg_color(s_dots[i],
                                  lv_color_hex(i == s_cur ? COL_BRAND : COL_DOT_IDLE), 0);
    }
}

static void nav_load(int idx, lv_screen_load_anim_t anim, uint32_t time)
{
    if (idx < 0 || idx >= s_count || idx == s_cur) return;   /* 线性到头即停,不回弹 */

    if (s_cur >= 0 && s_pages[s_cur].on_hide) s_pages[s_cur].on_hide();

    lv_screen_load_anim(s_scr[idx], anim, time, 0, false);   /* 页面常驻不销毁 */

    if (time > 0 && !s_anim_busy) {          /* 动画窗口内忽略后续手势（防重入） */
        s_anim_busy = true;
        lv_timer_t *t = lv_timer_create(anim_busy_clear_cb, UI_NAV_ANIM_BUSY_MS, NULL);
        lv_timer_set_repeat_count(t, 1);
    }

    s_cur = idx;
    if (s_pages[idx].on_show) s_pages[idx].on_show();
    dots_update();
    ESP_LOGI(TAG, "page %d/%d: %s", idx + 1, s_count, s_pages[idx].name);
}

/* ------------------------------------------------------------------ */
/* 手势回调：indev 级事件（LVGL 任务上下文=锁内）                        */
/* ------------------------------------------------------------------ */
static void gesture_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    const lv_indev_t *indev = bsp_lcd_get_touch_indev();
    if (indev == NULL) return;
    if (s_anim_busy) return;

    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_LEFT:                                   /* 左滑 → 下一页,新屏从右滑入 */
        nav_load(s_cur + 1, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, UI_NAV_ANIM_MS);
        break;
    case LV_DIR_RIGHT:                                  /* 右滑 → 上一页,新屏从左滑入 */
        nav_load(s_cur - 1, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, UI_NAV_ANIM_MS);
        break;
    default:                                            /* 上下滑暂不响应（相册页特例 U11 接） */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 页点指示器：top layer 底部居中（非 clickable → 不挡页面触摸）          */
/* ------------------------------------------------------------------ */
static void dots_create(void)
{
    lv_display_t *disp = lv_display_get_default();
    const int32_t w = lv_display_get_horizontal_resolution(disp);
    const int32_t h = lv_display_get_vertical_resolution(disp);
    const int dot = 4, gap = 8;
    const int total = s_count * dot + (s_count - 1) * gap;

    lv_obj_t *layer = lv_layer_top();
    for (int i = 0; i < s_count; i++) {
        lv_obj_t *d = lv_obj_create(layer);
        lv_obj_set_size(d, dot, dot);
        lv_obj_set_pos(d, (w - total) / 2 + i * (dot + gap), h - 12);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_dots[i] = d;
    }
}

/* ------------------------------------------------------------------ */
/* 占位页工厂                                                           */
/* ------------------------------------------------------------------ */
lv_obj_t *app_ui_nav_placeholder(const char *name, const char *symbol)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG_PAGE), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(scr);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(COL_BRAND), 0);
    lv_label_set_text(icon, symbol);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t *label = lv_label_create(scr);
    lv_obj_set_style_text_font(label, &noto_sans_sc_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(label, name);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 36);
    return scr;
}

/* ------------------------------------------------------------------ */
/* 公共接口                                                             */
/* ------------------------------------------------------------------ */
void app_ui_nav_init(const ui_page_def_t *pages, int count)
{
    s_pages = pages;
    s_count = (count > UI_PAGE_COUNT) ? UI_PAGE_COUNT : count;

    for (int i = 0; i < s_count; i++) {
        s_scr[i] = pages[i].create();
    }
    dots_create();

    /* 首次进入：瞬时载入第 0 页（与开机图衔接,保持现有开机体验无动画跳变） */
    lv_screen_load_anim(s_scr[0], LV_SCREEN_LOAD_ANIM_NONE, 0, 0, false);
    s_cur = 0;
    if (pages[0].on_show) pages[0].on_show();
    dots_update();

    lv_indev_add_event_cb(bsp_lcd_get_touch_indev(), gesture_cb, LV_EVENT_GESTURE, NULL);
    ESP_LOGI(TAG, "nav ready: %d pages, swipe to switch", s_count);
}

int app_ui_nav_current(void)
{
    return s_cur;
}

void app_ui_nav_goto(int idx, bool animate)
{
    if (s_anim_busy) return;
    if (idx > s_cur) {
        nav_load(idx, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, animate ? UI_NAV_ANIM_MS : 0);
    } else {
        nav_load(idx, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, animate ? UI_NAV_ANIM_MS : 0);
    }
}
