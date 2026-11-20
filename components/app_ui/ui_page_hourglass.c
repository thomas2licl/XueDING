/* ui_page_hourglass — 沙漏页（U5,一级页 7/9,2026-11-20 交互定稿）
 *
 * 布局:
 *   左:沙漏 canvas 120x136（页面同底色+白描边白沙,上下盖板+收腰双腔）
 *   中:设置态=档位胶囊 2x3;运行态=mm:ss 大数字
 *   右上:竖版按钮——设置态"开始倒计时";运行态"暂停/继续"+"取消";完成"重来"+"返回"
 * 交互(2026-11-20 定稿):
 *   设置态沙全沉下腔(上空下满);**翻转设备启动**——横放掉头 90↔270 时方向自适应
 *   转屏的同一事件触发(物理上=倒置沙漏,视觉'翻过来就开漏'),运行中翻转不响应;
 *   手动"开始倒计时"按钮并存;暂停/取消/重来仅按钮。滴滴提示音 S3 接入。 */

#include <math.h>
#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "app_event.h"
#include "app_timer.h"
#include "app_ui_nav.h"
#include "bsp_lcd.h"
#include "ui_page_hourglass.h"
#include "ui_style.h"

static const char *TAG = "ui_hourglass";

/* ---- 沙漏 canvas 几何（120x136,经典沙漏） ---- */
#define HG_W            120
#define HG_H            136
#define HG_PLATE_HALF   55      /* 上下盖板半宽 */
#define HG_TOP_HALF     50      /* 腔体顶部半宽 */
#define HG_WAIST_HALF   7       /* 腰口半宽 */
#define HG_Y_TOP        8       /* 上腔顶 */
#define HG_Y_BOT        (HG_H - 9)  /* 下腔底 */
#define HG_Y_WAIST      (HG_H / 2)

static const uint32_t k_durations[6] = { 1, 3, 5, 10, 15, 30 };   /* 分钟 */

/* ---- 控件 ---- */
static lv_obj_t *s_scr;
static lv_obj_t *s_canvas;
static lv_obj_t *s_durs[6];           /* 档位胶囊 */
static lv_obj_t *s_lab_mmss;          /* mm:ss（运行态,64px） */
static lv_obj_t *s_lab_state;         /* 状态字（仅暂停/时间到） */
static lv_obj_t *s_btn_full;          /* 通栏主按钮（开始/重来） */
static lv_obj_t *s_btn_pause;         /* 暂停/继续（运行态左半） */
static lv_obj_t *s_btn_cancel;        /* 取消（运行态右半） */
static lv_timer_t *s_ui_timer;
static uint32_t s_sel_min = 5;
static int s_sel_idx = 2;
static bool s_ever_started = false;    /* 倒计时过(≥1 次):设置态下腔从平铺80%变为锥形85% */
static ll_timer_state_t s_last_ui_state = (ll_timer_state_t)-1;
static uint32_t s_last_ui_remain = 0xFFFFFFFF;

static uint8_t s_canvas_buf[HG_W * HG_H * 2] __attribute__((aligned(4)));

/* ------------------------------------------------------------------ */
/* 沙漏绘制                                                            */
/* ------------------------------------------------------------------ */
/* 24 位网页色 → RGB565(canvas buffer 逐像素写入必须转换——直接截断会把
 * 深色写成蓝色系:0x080A0E→0x0A0E→RGB(8,64,112),2026-11-20 真机踩坑) */
static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)((((rgb >> 16) & 0xFF) >> 3) << 11 |
                      ((((rgb >> 8) & 0xFF) >> 2) << 5) |
                      ((rgb & 0xFF) >> 3));
}

static void hg_px(int x, int y, uint32_t c)
{
    if (x < 0 || x >= HG_W || y < 0 || y >= HG_H) return;
    ((uint16_t *)s_canvas_buf)[y * HG_W + x] = rgb565(c);
}

/* 上腔(y<腰):顶宽腰窄;下腔(y>腰):腰窄底宽 */
static int chamber_half(int y)
{
    if (y <= HG_Y_WAIST) {
        const int t = HG_Y_WAIST - y;                      /* 距腰 */
        return HG_WAIST_HALF + (HG_TOP_HALF - HG_WAIST_HALF) * t / (HG_Y_WAIST - HG_Y_TOP);
    }
    const int t = y - HG_Y_WAIST;                          /* 距腰 */
    return HG_WAIST_HALF + (HG_TOP_HALF - HG_WAIST_HALF) * t / (HG_Y_BOT - HG_Y_WAIST);
}

/* 下腔沙（2026-11-20 定稿）:
 * - 计时中/结束/计过后的设置态:中央锥形（顶部圆头）,堆高映射到 0~60% 腔高封顶
 * - 从未计时过的初始设置态:沙面平铺 60% 腔高（沙静置状） */
#define HG_CONE_CAP_R   2       /* 锥顶圆头半径(px):去锐利尖角但保持尖感(4 过钝,2026-11-20 调) */

static void draw_lower_sand(uint32_t ratio, uint32_t c, bool flat60)
{
    const int chamber_h = HG_Y_BOT - HG_Y_WAIST - 2;

    if (flat60) {                                /* 初始:平铺 60% */
        const int top = HG_Y_BOT - chamber_h * 60 / 100;
        for (int y = top; y <= HG_Y_BOT; y++) {
            const int half = chamber_half(y) - 2;
            for (int x = HG_W / 2 - half; x <= HG_W / 2 + half; x++) hg_px(x, y, c);
        }
        return;
    }

    const int heap_h = (int)(chamber_h * 60 / 100 * (1000 - ratio) / 1000);
    if (heap_h <= 0) return;
    /* +1 修正 off-by-one:循环含 HG_Y_BOT 端点,堆顶少算一行会让 t 越过 heap_h,
     * heap_h==4 时 t=5 进入锥面分支 → 分母(heap_h-4)==0 除零复位(真机踩坑) */
    const int heap_top = HG_Y_BOT - heap_h + 1;

    for (int y = heap_top; y <= HG_Y_BOT; y++) {
        const int half_ch = chamber_half(y) - 2;
        const int t = y - heap_top + 1;                     /* 距堆顶 */
        int half_sand;
        if (t <= HG_CONE_CAP_R) {                           /* 圆头段:圆弧过渡 */
            const int d = HG_CONE_CAP_R - t;
            half_sand = (int)(sqrtf((float)(HG_CONE_CAP_R * HG_CONE_CAP_R - d * d)) + 0.5f);
        } else {                                            /* 锥面段:线性到贴壁 */
            half_sand = HG_CONE_CAP_R +
                        (half_ch - HG_CONE_CAP_R) * (t - HG_CONE_CAP_R) / (heap_h - HG_CONE_CAP_R);
        }
        for (int x = HG_W / 2 - half_sand; x <= HG_W / 2 + half_sand; x++) hg_px(x, y, c);
    }
}

/* ratio=剩余千分比;running=沙线;flat60=初始未计时态(下腔平铺60%)。
 * 全量重绘(250ms 一次 ~1 万 px,可接受)。造型:底色=页面背景+白描边+白沙 */
static void hourglass_draw(uint32_t ratio, bool running, bool flat60)
{
    const uint32_t bg = 0x080A0E, frame = 0xFFFFFF, sand = 0xFFFFFF;

    for (int y = 0; y < HG_H; y++)
        for (int x = 0; x < HG_W; x++) hg_px(x, y, bg);

    /* 上下盖板（经典沙漏横杠,略宽于腔顶） */
    for (int x = HG_W / 2 - HG_PLATE_HALF; x <= HG_W / 2 + HG_PLATE_HALF; x++) {
        for (int t = 0; t < 3; t++) {
            hg_px(x, HG_Y_TOP - 5 + t, frame);
            hg_px(x, HG_Y_BOT + 3 + t, frame);
        }
    }
    /* 腔壁描边 2px */
    for (int y = HG_Y_TOP; y <= HG_Y_BOT; y++) {
        const int half = chamber_half(y);
        for (int t = 0; t < 2; t++) {
            hg_px(HG_W / 2 - half + t, y, frame);
            hg_px(HG_W / 2 + half - 1 - t, y, frame);
        }
    }

    /* 上腔沙:沙面=腰上 ratio×60% 腔高（启动即 60% 满,与下腔对称守恒,2026-11-20） */
    const int upper_h = HG_Y_WAIST - HG_Y_TOP - 2;
    const int sand_h = (int)(upper_h * 60 / 100 * ratio / 1000);
    for (int y = HG_Y_WAIST - sand_h; y < HG_Y_WAIST; y++) {
        const int half = chamber_half(y) - 2;
        const int dip = (y < HG_Y_WAIST - sand_h + 2) ? 0 : 1;   /* 面口微凹 */
        for (int x = HG_W / 2 - half; x <= HG_W / 2 + half; x++) {
            hg_px(x, y + dip, sand);
        }
    }

    draw_lower_sand(ratio, sand, flat60);

    /* 沙线:运行中,腰下 3px 宽细流,直到接近堆顶 */
    if (running) {
        for (int y = HG_Y_WAIST; y < HG_H - 10; y++) {
            for (int t = -1; t <= 1; t++) hg_px(HG_W / 2 + t, y, sand);
        }
    }

    lv_obj_invalidate(s_canvas);
}

/* ------------------------------------------------------------------ */
/* 状态刷新                                                            */
/* ------------------------------------------------------------------ */
static void durs_refresh(void)
{
    for (int i = 0; i < 6; i++) {
        lv_obj_set_style_bg_color(s_durs[i],
                                  lv_color_hex(i == s_sel_idx ? UI_COL_BRAND : UI_COL_PANEL), 0);
    }
}

static void refresh_ui(void)
{
    const ll_timer_state_t st = app_timer_get_state();
    const uint32_t remain = app_timer_get_remain_s();
    if (st == s_last_ui_state && remain == s_last_ui_remain) return;
    const bool state_changed = (st != s_last_ui_state);   /* 状态迁移才切控件 */
    s_last_ui_state = st;
    s_last_ui_remain = remain;

    const bool setup = (st == LL_TIMER_IDLE);

    /* 控件切换仅在状态迁移帧做——每秒 tick 只更新 mm:ss+canvas,
     * 避免按钮文字被无谓重绘(重绘放大启动闪烁面,2026-11-20 帧日志定位) */
    if (state_changed) {
        for (int i = 0; i < 6; i++) {
            (setup ? lv_obj_clear_flag : lv_obj_add_flag)(s_durs[i], LV_OBJ_FLAG_HIDDEN);
        }
        (!setup ? lv_obj_clear_flag : lv_obj_add_flag)(s_lab_mmss, LV_OBJ_FLAG_HIDDEN);

        if (setup) {
            lv_obj_add_flag(s_lab_state, LV_OBJ_FLAG_HIDDEN);   /* 设置态无状态字 */
            lv_obj_clear_flag(s_btn_full, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_btn_pause, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_btn_cancel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_btn_full, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_btn_pause, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_btn_cancel, LV_OBJ_FLAG_HIDDEN);

            switch (st) {
            case LL_TIMER_RUNNING:
                lv_obj_add_flag(s_lab_state, LV_OBJ_FLAG_HIDDEN);   /* 运行中无状态字 */
                lv_label_set_text(lv_obj_get_child(s_btn_pause, 0), "暂 停");
                lv_label_set_text(lv_obj_get_child(s_btn_cancel, 0), "取 消");
                break;
            case LL_TIMER_PAUSED:
                lv_label_set_text(s_lab_state, "已暂停");
                lv_obj_set_style_text_color(s_lab_state, lv_color_hex(UI_COL_ORANGE), 0);
                lv_obj_clear_flag(s_lab_state, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lv_obj_get_child(s_btn_pause, 0), "继 续");
                lv_label_set_text(lv_obj_get_child(s_btn_cancel, 0), "取 消");
                break;
            case LL_TIMER_DONE:
                lv_label_set_text(s_lab_state, "时间到");
                lv_obj_set_style_text_color(s_lab_state, lv_color_hex(UI_COL_FG), 0);
                lv_obj_clear_flag(s_lab_state, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lv_obj_get_child(s_btn_pause, 0), "重 来");
                lv_label_set_text(lv_obj_get_child(s_btn_cancel, 0), "返 回");
                break;
            default:
                break;
            }
        }
    }

    if (!setup) {
        lv_label_set_text_fmt(s_lab_mmss, "%02u:%02u",
                              (unsigned)(remain / 60), (unsigned)(remain % 60));
    }
    const bool done = (st == LL_TIMER_DONE);

    /* 沙量:设置态=沙全沉下腔(初始未计时=平铺60%,计过=锥形60%圆头);
     * 运行/暂停=剩余比例(上腔沙量,下腔锥形 0~60% 映射);完成=锥 60% */
    uint32_t ratio;
    if (setup) ratio = 0;
    else if (done) ratio = 0;
    else {
        const uint32_t total = s_sel_min * 60;
        ratio = (total > 0) ? remain * 1000 / total : 0;
        if (ratio > 1000) ratio = 1000;
    }
    hourglass_draw(ratio, st == LL_TIMER_RUNNING, setup && !s_ever_started);
    /* 注意:此处不可调 lv_refr_now——refresh_ui 会被 on_orientation/on_fired 在
     * esp_event 任务上下文调用,refr_now 驱动完整刷新循环在非 LVGL 任务执行
     * 会复位(2026-11-20 真机:翻转启动必重启);跨任务只做持锁 set/invalidate,
     * 与 app_ui on_battery 同模式,渲染交给 LVGL 任务 */
}

static void ui_tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    refresh_ui();
}

/* ------------------------------------------------------------------ */
/* 按钮与事件                                                           */
/* ------------------------------------------------------------------ */
/* 统一启动入口:置"计时过"标志(设置态沙型从平铺80%切换为锥形85%的依据) */
static void start_countdown(void)
{
    if (app_timer_start(s_sel_min * 60) == ESP_OK) s_ever_started = true;
}

static void full_btn_cb(lv_event_t *e)      /* 设置态:开始倒计时 */
{
    LV_UNUSED(e);
    start_countdown();
    refresh_ui();
    /* 270 方向局部脏区有残留旧帧问题(2026-11-20 真机,draw 序列已证正常,
     * 属上屏层):启动瞬间全屏 invalidate + 立即渲染,一帧全刷 ~20ms 绕过 */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
}

static void pause_btn_cb(lv_event_t *e)      /* 运行态:暂停/继续;完成态:重来 */
{
    LV_UNUSED(e);
    const ll_timer_state_t st = app_timer_get_state();
    if (st == LL_TIMER_RUNNING) app_timer_pause();
    else if (st == LL_TIMER_PAUSED) app_timer_resume();
    else if (st == LL_TIMER_DONE) {           /* 重来=同时长再启 */
        app_timer_cancel();
        start_countdown();
    }
    refresh_ui();
    lv_refr_now(NULL);                        /* 同上:按钮路径立即上屏 */
}

static void cancel_btn_cb(lv_event_t *e)     /* 运行态:取消;完成态:返回设置 */
{
    LV_UNUSED(e);
    app_timer_cancel();
    refresh_ui();
}

static void dur_btn_cb(lv_event_t *e)
{
    if (app_timer_get_state() != LL_TIMER_IDLE) return;
    s_sel_idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel_min = k_durations[s_sel_idx];
    durs_refresh();
    refresh_ui();
}

/* 翻转启动（2026-11-20 交互定稿,替代竖放手势）:横放掉头 90↔270 时
 * app_motion 自动转屏并发 LL_EV_ORIENTATION——物理上=把沙漏倒置,
 * 沙漏页前台且设置态即启动。运行中翻转只转屏不响应。 */
static void on_orientation(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id);
    const lv_display_rotation_t rot = *(const lv_display_rotation_t *)data;
    if (app_ui_nav_current() != UI_PAGE_IDX_HOURGLASS) return;
    if (app_timer_get_state() != LL_TIMER_IDLE) return;

    if (bsp_lcd_lock(500)) {
        start_countdown();
        s_last_ui_state = (ll_timer_state_t)-1;
        refresh_ui();
        lv_obj_invalidate(lv_screen_active());   /* 同按钮路径:全屏强刷绕过 270 残留 */
        bsp_lcd_unlock();
        ESP_LOGI(TAG, "flip(rot=%d) -> start %umin", (int)rot, (unsigned)s_sel_min);
    }
}

static void on_fired(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id); LV_UNUSED(data);
    if (bsp_lcd_lock(500)) {
        if (app_ui_nav_current() != UI_PAGE_IDX_HOURGLASS) {
            app_ui_nav_goto(UI_PAGE_IDX_HOURGLASS, true);
        } else {
            s_last_ui_state = (ll_timer_state_t)-1;
            refresh_ui();
        }
        bsp_lcd_unlock();
    }
    /* TODO S3:滴滴提示音 */
}

/* ------------------------------------------------------------------ */
/* 页面构建                                                            */
/* ------------------------------------------------------------------ */
/* 右上竖版大按钮（窄而高,两行文字;y44 起与胶囊网格同带垂直居中） */
static lv_obj_t *btn_tall_make(lv_obj_t *parent, const char *text,
                               lv_event_cb_t cb, int32_t x, uint32_t color)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 108, 84);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, x, 44);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab = lv_label_create(b);
    lv_obj_set_style_text_font(lab, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lab, text);
    lv_obj_center(lab);
    return b;
}

lv_obj_t *ui_hourglass_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 左:沙漏（120x136,y18~154 居左;初始=上空+下腔平铺80%） */
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, HG_W, HG_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_TOP_LEFT, 24, 18);
    hourglass_draw(0, false, true);

    /* 档位胶囊 2x3 网格（开始按钮左侧,整带垂直居中:x164~498,y50~126） */
    for (int i = 0; i < 6; i++) {
        s_durs[i] = lv_button_create(s_scr);
        lv_obj_set_size(s_durs[i], 106, 34);
        lv_obj_set_pos(s_durs[i], 164 + (i % 3) * 114, 50 + (i / 3) * 42);
        lv_obj_add_event_cb(s_durs[i], dur_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lab = lv_label_create(s_durs[i]);
        lv_obj_set_style_text_font(lab, UI_FONT_TEXT, 0);
        lv_label_set_text_fmt(lab, "%u分", (unsigned)k_durations[i]);
        lv_obj_center(lab);
    }
    durs_refresh();

    /* 运行态 mm:ss（沙漏右侧,64px;LEFT_MID 居中+6px 补偿字形偏上,与按钮中心 y86 对齐） */
    s_lab_mmss = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lab_mmss, &noto_num_64, 0);
    lv_obj_set_style_text_color(s_lab_mmss, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(s_lab_mmss, "00:00");
    lv_obj_align(s_lab_mmss, LV_ALIGN_LEFT_MID, 164, 6);
    lv_obj_add_flag(s_lab_mmss, LV_OBJ_FLAG_HIDDEN);

    /* 状态字:仅暂停/时间到显示（mm:ss 下方） */
    s_lab_state = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lab_state, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_lab_state, lv_color_hex(UI_COL_ORANGE), 0);
    lv_label_set_text(s_lab_state, "已暂停");
    lv_obj_align(s_lab_state, LV_ALIGN_TOP_LEFT, 164, 132);
    lv_obj_add_flag(s_lab_state, LV_OBJ_FLAG_HIDDEN);

    /* 右上竖版按钮区（右缘统一对齐 x628;运行态 pause+cancel 右对齐排布） */
    s_btn_full = btn_tall_make(s_scr, "开始\n倒计时", full_btn_cb, -12, UI_COL_BRAND);
    s_btn_pause = btn_tall_make(s_scr, "暂 停", pause_btn_cb, -132, UI_COL_BRAND);
    s_btn_cancel = btn_tall_make(s_scr, "取 消", cancel_btn_cb, -12, UI_COL_PANEL);
    lv_obj_add_flag(s_btn_pause, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_cancel, LV_OBJ_FLAG_HIDDEN);

    s_ui_timer = lv_timer_create(ui_tick_cb, 250, NULL);
    lv_timer_pause(s_ui_timer);

    app_event_subscribe(LL_EV_ORIENTATION, on_orientation, NULL);
    app_event_subscribe(LL_EV_TIMER_FIRED, on_fired, NULL);
    return s_scr;
}

void ui_hourglass_on_show(void)
{
    lv_timer_resume(s_ui_timer);
    s_last_ui_state = (ll_timer_state_t)-1;
    refresh_ui();
}

void ui_hourglass_on_hide(void)
{
    lv_timer_pause(s_ui_timer);   /* 计时引擎照跑,仅停 UI 刷新 */
}
