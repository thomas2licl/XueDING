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
static lv_obj_t *s_dots[UI_PAGE_COUNT];   /* 页点（top layer 容器内） */
static int s_cur = -1;
static bool s_anim_busy = false;
static lv_obj_t *s_sec_scr = NULL;        /* 当前二级页（页面栈一层,最深两级） */
static void (*s_vertical_cb)(lv_dir_t, int32_t) = NULL;   /* 竖向手势接管(主页卡片) */

/* 页点自动隐藏（2026-11-19 用户建议,符合"安静"设计理念）:
 * 触摸淡入,3s 无操作淡出;二级页期间隐藏,程序切页点亮 3s。 */
#define UI_DOTS_IDLE_MS   3000
#define UI_DOTS_FADE_MS   150
static lv_obj_t *s_dots_cont = NULL;
static bool s_dots_shown = true;
static uint32_t s_touch_ms = 0;

static void dots_show(void);              /* 前向声明（nav_load/touch_cb 先用） */
static void dots_hide(void);

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
    if (!s_sec_scr) dots_show();          /* 程序切页也点亮页点 3s（语音/场景联动路径） */
    dots_update();
    ESP_LOGI(TAG, "page %d/%d: %s", idx + 1, s_count, s_pages[idx].name);
}

/* ------------------------------------------------------------------ */
/* 手势回调：indev 级事件（LVGL 任务上下文=锁内）                        */
/* ------------------------------------------------------------------ */
static int32_t s_press_start_x = -1;     /* 按下起点（边缘返回手势判定用） */

/* lv_indev_t 结构体 LVGL9 已私有化,拿不到 gesture_sum 回推起点——
 * 改由 indev 级 PRESSED/RELEASED 事件自记按下点;同时驱动页点显示/计时 */
static void touch_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = bsp_lcd_get_touch_indev();
    if (indev == NULL) return;

    s_touch_ms = lv_tick_get();           /* 任何触摸=有操作,重置页点 3s 计时 */
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        s_press_start_x = p.x;
    }
    if (!s_sec_scr) dots_show();          /* 二级页期间不出页点,返回一级再亮 */
}

/* 手势冷却:触摸芯片拖动报点间隔(200~400ms)超过去抖窗口(30ms),一次长滑会被
 * 拆成多次按下→多次手势→连切多页(2026-11-20 真机);冷却期内忽略新手势=一滑一页 */
#define UI_NAV_GESTURE_COOLDOWN_MS  350

static void gesture_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = bsp_lcd_get_touch_indev();
    if (indev == NULL) return;
    if (s_anim_busy) return;
    static uint32_t last_gesture_ms = 0;
    if (lv_tick_elaps(last_gesture_ms) < UI_NAV_GESTURE_COOLDOWN_MS) return;

    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    /* 二级页:仅"起点在左边缘的右滑"=返回;其余方向不消费（页面控件优先） */
    if (s_sec_scr != NULL) {
        if (dir == LV_DIR_RIGHT && s_press_start_x >= 0 && s_press_start_x <= UI_NAV_EDGE_PX) {
            if (app_ui_nav_pop()) last_gesture_ms = lv_tick_get();
        }
        return;
    }

    switch (dir) {
    case LV_DIR_LEFT:                                   /* 左滑 → 下一页,新屏从右滑入 */
        if (s_cur + 1 < s_count) {
            nav_load(s_cur + 1, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, UI_NAV_ANIM_MS);
            last_gesture_ms = lv_tick_get();
        }
        break;
    case LV_DIR_RIGHT:                                  /* 右滑 → 上一页,新屏从左滑入 */
        if (s_cur > 0) {
            nav_load(s_cur - 1, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, UI_NAV_ANIM_MS);
            last_gesture_ms = lv_tick_get();
        }
        break;
    default:                                            /* 上下滑:转发给页面注册的回调 */
        if (s_vertical_cb) s_vertical_cb(dir, s_press_start_x);
        break;
    }
}

void app_ui_nav_set_vertical_cb(void (*cb)(lv_dir_t dir, int32_t start_x))
{
    s_vertical_cb = cb;
}

/* ------------------------------------------------------------------ */
/* 页点指示器：top layer 底部居中（非 clickable → 不挡页面触摸）          */
/* 3s 无操作自动淡出,触摸淡入（2026-11-19）;二级页期间隐藏               */
/* ------------------------------------------------------------------ */
static void dots_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void dots_hidden_cb(lv_anim_t *a)
{
    lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN);
}

static void dots_show(void)
{
    s_touch_ms = lv_tick_get();           /* 显示即重置 3s 计时 */
    if (s_dots_shown) return;
    s_dots_shown = true;
    lv_obj_clear_flag(s_dots_cont, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_dots_cont);
    lv_anim_set_exec_cb(&a, dots_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, UI_DOTS_FADE_MS);
    lv_anim_start(&a);
}

static void dots_hide(void)
{
    if (!s_dots_shown) return;
    s_dots_shown = false;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_dots_cont);
    lv_anim_set_exec_cb(&a, dots_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, UI_DOTS_FADE_MS);
    lv_anim_set_completed_cb(&a, dots_hidden_cb);
    lv_anim_start(&a);
}

/* 500ms 检查一次空闲超时（隐藏延迟精度要求低,不必高频） */
static void dots_idle_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (s_dots_shown && lv_tick_elaps(s_touch_ms) >= UI_DOTS_IDLE_MS) {
        dots_hide();
    }
}

static void dots_create(void)
{
    lv_display_t *disp = lv_display_get_default();
    const int32_t w = lv_display_get_horizontal_resolution(disp);
    const int32_t h = lv_display_get_vertical_resolution(disp);
    const int dot = 4, gap = 8;
    const int total = s_count * dot + (s_count - 1) * gap;

    s_dots_cont = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_dots_cont, total, dot);
    lv_obj_set_pos(s_dots_cont, (w - total) / 2, h - 12);
    lv_obj_set_style_bg_opa(s_dots_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dots_cont, 0, 0);
    lv_obj_set_style_pad_all(s_dots_cont, 0, 0);
    lv_obj_clear_flag(s_dots_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < s_count; i++) {
        lv_obj_t *d = lv_obj_create(s_dots_cont);
        lv_obj_set_size(d, dot, dot);
        lv_obj_set_pos(d, i * (dot + gap), 0);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_dots[i] = d;
    }
    s_touch_ms = lv_tick_get();
    lv_timer_create(dots_idle_cb, 500, NULL);
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
    lv_indev_add_event_cb(bsp_lcd_get_touch_indev(), touch_cb, LV_EVENT_PRESSED, NULL);
    lv_indev_add_event_cb(bsp_lcd_get_touch_indev(), touch_cb, LV_EVENT_RELEASED, NULL);
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

/* ------------------------------------------------------------------ */
/* 二级页面栈                                                           */
/* ------------------------------------------------------------------ */
static void anim_busy_arm(void)
{
    s_anim_busy = true;
    lv_timer_t *t = lv_timer_create(anim_busy_clear_cb, UI_NAV_ANIM_BUSY_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void app_ui_nav_push(lv_obj_t *scr)
{
    if (scr == NULL || s_sec_scr != NULL || s_anim_busy) return;
    if (s_cur >= 0 && s_pages[s_cur].on_hide) s_pages[s_cur].on_hide();
    anim_busy_arm();
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, UI_NAV_ANIM_MS, 0, false);
    s_sec_scr = scr;
    dots_hide();                          /* 二级页期间不出页点 */
    ESP_LOGI(TAG, "push secondary page");
}

bool app_ui_nav_pop(void)
{
    if (s_sec_scr == NULL || s_anim_busy) return false;
    anim_busy_arm();
    lv_screen_load_anim(s_scr[s_cur], LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, UI_NAV_ANIM_MS, 0, false);
    s_sec_scr = NULL;
    if (s_pages[s_cur].on_show) s_pages[s_cur].on_show();
    dots_show();                          /* 回一级页点亮 3s */
    ESP_LOGI(TAG, "pop back to: %s", s_pages[s_cur].name);
    return true;
}

bool app_ui_nav_is_secondary(void)
{
    return s_sec_scr != NULL;
}
