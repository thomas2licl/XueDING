/* ui_page_alarms — 闹钟页（U4,一级页 3/9）+ 新建闹钟（二级页,首个二级页样板）
 *
 * 设计依据:XueDING 交互规范 4.5 + 03_alarms.png;新建页为缺失稿,按规范自行设计
 * （2026-11-19 已授权）。数据:app_alarm 引擎（S2.3,NVS 持久,8 条上限）。
 *
 * 布局（640×172）:
 *   列表页:标题"闹钟" | 列表(可纵向滚动,每条 时间+重复+开关) | 底部"+ 添加闹钟"
 *   新建页:返回 | 标题 | 时/分滚轮 | 单次/每天 | 保存
 * 交互:开关即时生效落 NVS;保存→pop 返回列表自动刷新(on_show 重读);
 *      二级页左边缘右滑=返回（nav 层手势）。 */

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "app_alarm.h"
#include "app_clock.h"
#include "app_ui_nav.h"
#include "ui_style.h"

static const char *TAG = "ui_alarms";

/* ------------------------------------------------------------------ */
/* 新建闹钟（二级页,常驻单例）                                          */
/* ------------------------------------------------------------------ */
static lv_obj_t *s_edit_scr = NULL;
static lv_obj_t *s_roller_h, *s_roller_m;
static lv_obj_t *s_mode_btn[4];          /* ll_alarm_repeat_t 4 档（2026-11-19 需求细化） */
static uint8_t s_mode = 0;

static const char *const k_repeat_name[4] = { "单次", "每天", "周一到五", "周一到六" };

static void edit_mode_set(uint8_t mode)
{
    s_mode = mode;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_mode_btn[i],
                                  lv_color_hex(i == mode ? UI_COL_BRAND : UI_COL_PANEL), 0);
        lv_obj_set_style_text_color(s_mode_btn[i],
                                    lv_color_hex(i == mode ? UI_COL_FG : UI_COL_DIM), 0);
    }
}

static void mode_btn_cb(lv_event_t *e)
{
    edit_mode_set((uint8_t)(intptr_t)lv_event_get_user_data(e));
}

static void save_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ll_alarm_t a = {
        .enabled = true,
        .hour = (uint8_t)lv_roller_get_selected(s_roller_h),
        .minute = (uint8_t)lv_roller_get_selected(s_roller_m),
        .repeat_mode = s_mode,
    };
    const int id = app_alarm_add(&a);
    ESP_LOGI(TAG, "add alarm id=%d %02u:%02u %s", id, a.hour, a.minute,
             s_mode == LL_ALARM_DAILY ? "daily" : "once");
    app_ui_nav_pop();                    /* 列表页 on_show 重读刷新 */
}

static void back_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_ui_nav_pop();
}

static void edit_create(void)
{
    s_edit_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_edit_scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_clear_flag(s_edit_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶栏:返回 + 标题 */
    lv_obj_t *back = lv_button_create(s_edit_scr);
    lv_obj_set_size(back, 84, 30);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lab = lv_label_create(back);
    lv_obj_set_style_text_font(back_lab, UI_FONT_TEXT, 0);
    lv_label_set_text(back_lab, LV_SYMBOL_LEFT " 返回");
    lv_obj_center(back_lab);

    lv_obj_t *title = lv_label_create(s_edit_scr);
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(title, "新建闹钟");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 56, 8);   /* 视觉居中偏右（2026-11-19 反馈微调） */

    /* 时/分滚轮（3 行可见,居中偏左） */
    static char opts_h[24 * 3 + 1], opts_m[60 * 3 + 1];
    char *p = opts_h;
    for (int i = 0; i < 24; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
    p = opts_m;
    for (int i = 0; i < 60; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);

    s_roller_h = lv_roller_create(s_edit_scr);
    lv_roller_set_options(s_roller_h, opts_h, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller_h, 3);
    lv_obj_set_style_text_font(s_roller_h, UI_FONT_TITLE, 0);
    lv_obj_set_style_bg_color(s_roller_h, lv_color_hex(UI_COL_PANEL), 0);
    lv_obj_set_style_border_width(s_roller_h, 0, 0);
    lv_obj_align(s_roller_h, LV_ALIGN_LEFT_MID, 120, -12);

    lv_obj_t *colon = lv_label_create(s_edit_scr);
    lv_obj_set_style_text_font(colon, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(UI_COL_DIM), 0);
    lv_label_set_text(colon, ":");
    lv_obj_align_to(colon, s_roller_h, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

    s_roller_m = lv_roller_create(s_edit_scr);
    lv_roller_set_options(s_roller_m, opts_m, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller_m, 3);
    lv_obj_set_style_text_font(s_roller_m, UI_FONT_TITLE, 0);
    lv_obj_set_style_bg_color(s_roller_m, lv_color_hex(UI_COL_PANEL), 0);
    lv_obj_set_style_border_width(s_roller_m, 0, 0);
    lv_obj_align_to(s_roller_m, colon, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

    /* 重复模式胶囊（2×2 网格:单次/每天/周一到五/周一到六） */
    for (int i = 0; i < 4; i++) {
        s_mode_btn[i] = lv_button_create(s_edit_scr);
        lv_obj_set_size(s_mode_btn[i], 118, 30);
        lv_obj_set_pos(s_mode_btn[i], 352 + (i % 2) * 126, 50 + (i / 2) * 38);
        lv_obj_add_event_cb(s_mode_btn[i], mode_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lab = lv_label_create(s_mode_btn[i]);
        lv_obj_set_style_text_font(lab, UI_FONT_TEXT, 0);
        lv_label_set_text(lab, k_repeat_name[i]);
        lv_obj_center(lab);
    }

    /* 保存（通栏主按钮） */
    lv_obj_t *save = lv_button_create(s_edit_scr);
    lv_obj_set_size(save, 240, 36);
    lv_obj_align(save, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(save, lv_color_hex(UI_COL_BRAND), 0);
    lv_obj_add_event_cb(save, save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lab = lv_label_create(save);
    lv_obj_set_style_text_font(save_lab, UI_FONT_TITLE, 0);
    lv_label_set_text(save_lab, "保 存");
    lv_obj_center(save_lab);
}

static void edit_open(void)
{
    if (s_edit_scr == NULL) edit_create();

    /* 默认值:当前时间 +1h */
    const bsp_rtc_datetime_t *now = app_clock_get_time();
    if (now) {
        lv_roller_set_selected(s_roller_h, (now->hour + 1) % 24, LV_ANIM_OFF);
        lv_roller_set_selected(s_roller_m, 0, LV_ANIM_OFF);
    }
    edit_mode_set(LL_ALARM_ONCE);
    app_ui_nav_push(s_edit_scr);
}

/* ------------------------------------------------------------------ */
/* 闹钟列表（一级页）                                                    */
/* ------------------------------------------------------------------ */
static lv_obj_t *s_list = NULL;          /* 列表容器 */
static lv_obj_t *s_empty_hint = NULL;
static void rebuild(void);               /* 前向声明（trash_cb 先用到） */

static void switch_cb(lv_event_t *e)
{
    const int id = (int)(intptr_t)lv_event_get_user_data(e);   /* 真实槽位 id */
    lv_obj_t *sw = lv_event_get_target(e);
    ll_alarm_t alarms[APP_ALARM_MAX];
    int ids[APP_ALARM_MAX];
    const int n = app_alarm_get_with_ids(alarms, ids, APP_ALARM_MAX);

    for (int i = 0; i < n; i++) {
        if (ids[i] != id) continue;
        alarms[i].enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        app_alarm_update(id, &alarms[i]);
        ESP_LOGI(TAG, "alarm slot %d %s", id, alarms[i].enabled ? "on" : "off");
        break;
    }
}

static void add_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    edit_open();
}

/* 删除条目（槽位 id;直接删,误删重加成本低——手机闹钟习惯） */
static void trash_btn_cb(lv_event_t *e)
{
    const int id = (int)(intptr_t)lv_event_get_user_data(e);
    app_alarm_remove(id);
    ESP_LOGI(TAG, "alarm slot %d removed", id);
    rebuild();
}

/* 重建列表（on_show/数据变化后调用,LVGL 上下文） */
static void rebuild(void)
{
    lv_obj_clean(s_list);                /* 清空子对象 */

    ll_alarm_t alarms[APP_ALARM_MAX];
    int ids[APP_ALARM_MAX];
    const int n = app_alarm_get_with_ids(alarms, ids, APP_ALARM_MAX);

    lv_obj_add_flag(s_empty_hint, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, 604, 40);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_PANEL), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

        lv_obj_t *time_lab = lv_label_create(row);
        lv_obj_set_style_text_font(time_lab, UI_FONT_TITLE, 0);
        lv_obj_set_style_text_color(time_lab, lv_color_hex(UI_COL_FG), 0);
        lv_label_set_text_fmt(time_lab, "%02u:%02u", alarms[i].hour, alarms[i].minute);
        lv_obj_align(time_lab, LV_ALIGN_LEFT_MID, 12, 0);

        lv_obj_t *rep_lab = lv_label_create(row);
        lv_obj_set_style_text_font(rep_lab, UI_FONT_TEXT, 0);
        lv_obj_set_style_text_color(rep_lab, lv_color_hex(UI_COL_DIM), 0);
        lv_label_set_text(rep_lab, k_repeat_name[alarms[i].repeat_mode % 4]);
        lv_obj_align_to(rep_lab, time_lab, LV_ALIGN_OUT_RIGHT_MID, 16, 0);

        /* 删除（开关左侧,trash 图标） */
        lv_obj_t *trash = lv_button_create(row);
        lv_obj_set_size(trash, 44, 28);
        lv_obj_align(trash, LV_ALIGN_RIGHT_MID, -72, 0);
        lv_obj_set_style_bg_color(trash, lv_color_hex(UI_COL_PANEL), 0);
        lv_obj_add_event_cb(trash, trash_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ids[i]);
        lv_obj_t *trash_lab = lv_label_create(trash);
        lv_obj_set_style_text_font(trash_lab, UI_FONT_TEXT, 0);
        lv_obj_set_style_text_color(trash_lab, lv_color_hex(UI_COL_WEAK), 0);
        lv_label_set_text(trash_lab, LV_SYMBOL_TRASH);
        lv_obj_center(trash_lab);

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 52, 26);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_style_bg_color(sw, lv_color_hex(UI_COL_BRAND), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (alarms[i].enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)ids[i]);
    }

    if (n == 0) {
        lv_obj_clear_flag(s_empty_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *ui_alarms_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(title, "闹钟");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 8);

    /* 列表容器:仅纵向滚动（横向手势留给切页,scroll_dir 限定） */
    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, 616, 82);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    /* 空列表提示 */
    s_empty_hint = lv_label_create(scr);
    lv_obj_set_style_text_font(s_empty_hint, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(s_empty_hint, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(s_empty_hint, "暂无闹钟");
    lv_obj_align(s_empty_hint, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_add_flag(s_empty_hint, LV_OBJ_FLAG_HIDDEN);

    /* 底部添加按钮（通栏主按钮） */
    lv_obj_t *add = lv_button_create(scr);
    lv_obj_set_size(add, 616, 34);
    lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(add, lv_color_hex(UI_COL_BRAND), 0);
    lv_obj_add_event_cb(add, add_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *add_lab = lv_label_create(add);
    lv_obj_set_style_text_font(add_lab, UI_FONT_TITLE, 0);
    lv_label_set_text(add_lab, "+ 添加闹钟");
    lv_obj_center(add_lab);

    return scr;
}

void ui_alarms_on_show(void)
{
    rebuild();
}
