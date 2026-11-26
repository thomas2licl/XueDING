/* ui_home_cards — 主页三槽卡片系统实现（U3b）
 * 卡片接口与槽位机制见 ui_home_cards.h。当前 4 张卡为临时样式
 * （时钟卡2/天气/倒计时/提醒）——用户设计定稿后逐卡重写内部 UI,框架不动。 */

#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"

#include "app_alarm.h"
#include "app_clock.h"
#include "app_timer.h"
#include "ui_home_cards.h"
#include "ui_style.h"

static const char *TAG = "home_cards";

/* ---- 槽几何：640 三等分 ---- */
#define SLOT_W      206
#define SLOT_X(i)   (8 + (i) * (SLOT_W + 6))   /* 8/220/434 起,右余 8 边距 */

/* ================================================================== */
/* 卡片一：时钟卡 2（日期星期/64px 时间/下一条闹钟 + 状态行:电池/WiFi/蓝牙） */
/* ================================================================== */

/* 时钟卡 2 布局（2026-11-21 用户设计,二轮调整）:日期星期(顶)/时间(垂直居中)/
 * 状态行(底)三块上下均匀分布。状态行=flex 行,顺序:电池 → WiFi(未连接右下
 * 红叹号) → 蓝牙(未连接隐藏) → 闹钟(行尾,无闹钟隐藏)。
 * 连接状态钩子: home_cards_set_net(wifi_ok, bt_ok)——U8 WiFi/S3 蓝牙接入时调用 */
static lv_obj_t *c_time, *c_date;
static lv_obj_t *c_row, *c_batt, *c_wifi, *c_wifi_excl, *c_bt, *c_alarm;
static ll_battery_state_t s_batt_snap;        /* 电池快照（on_battery 事件推送） */
static bool s_wifi_ok = false, s_bt_ok = false;
static uint8_t c_last_sec = 0xFF, c_last_min = 0xFF;   /* 去重(create 复位,防切卡后占位文本滞留) */

static void card_clock_refresh(void)
{
    const bsp_rtc_datetime_t *now = app_clock_get_time();
    if (now == NULL) { lv_label_set_text(c_time, "--:--"); return; }

    if (now->second != c_last_sec) {
        c_last_sec = now->second;
        lv_label_set_text_fmt(c_time, "%02u%c%02u",
                              now->hour, (now->second & 1) ? ':' : ' ', now->minute);

        if (now->minute != c_last_min) {
            c_last_min = now->minute;
            static const char *const wd[7] = { "日", "一", "二", "三", "四", "五", "六" };
            lv_label_set_text_fmt(c_date, "%u月%02u日 星期%s",
                                  now->month, now->day, wd[now->weekday % 7]);

            /* 下一条闹钟(行尾;无启用闹钟整段隐藏) */
            ll_alarm_t alarms[APP_ALARM_MAX];
            int ids[APP_ALARM_MAX];
            const int n = app_alarm_get_with_ids(alarms, ids, APP_ALARM_MAX);
            int best = -1;
            uint32_t best_d = 0xFFFFFFFF;
            for (int i = 0; i < n; i++) {
                if (!alarms[i].enabled) continue;
                const uint32_t t = alarms[i].hour * 60u + alarms[i].minute;
                const uint32_t nowm = now->hour * 60u + now->minute;
                const uint32_t d = (t > nowm) ? t - nowm : t + 1440u - nowm;
                if (d < best_d) { best_d = d; best = i; }
            }
            if (best >= 0) {
                lv_label_set_text_fmt(c_alarm, LV_SYMBOL_BELL " %02u:%02u",
                                      alarms[best].hour, alarms[best].minute);
                lv_obj_clear_flag(c_alarm, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(c_alarm, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    /* 电池段 */
    const char *bicon = LV_SYMBOL_BATTERY_FULL;
    if (s_batt_snap.percent < 20)      bicon = LV_SYMBOL_BATTERY_1;
    else if (s_batt_snap.percent < 50) bicon = LV_SYMBOL_BATTERY_2;
    else if (s_batt_snap.percent < 90) bicon = LV_SYMBOL_BATTERY_3;
    lv_label_set_text_fmt(c_batt, "%s%s%u%%",
                          s_batt_snap.charging ? LV_SYMBOL_CHARGE : "",
                          bicon, (unsigned)s_batt_snap.percent);
    lv_obj_set_style_text_color(c_batt,
        lv_color_hex(s_batt_snap.vbat_mv <= 3300 ? UI_COL_RED : UI_COL_WEAK), 0);
}

static lv_obj_t *card_clock_create(lv_obj_t *parent)
{
    /* 卡片重建=去重状态复位:slot_load 会紧跟 refresh,若沿用旧 last_min,
     * 切卡回来时分钟未跳变 → 日期/闹钟保持占位文本最长 1 分钟(真机踩坑) */
    c_last_sec = 0xFF;
    c_last_min = 0xFF;

    /* 三块垂直均匀分布:顶(日期) / 中(时间) / 底(状态行) */
    c_date = lv_label_create(parent);
    lv_obj_set_style_text_font(c_date, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(c_date, lv_color_hex(UI_COL_DIM), 0);
    lv_label_set_text(c_date, "--月--日");
    lv_obj_align(c_date, LV_ALIGN_TOP_LEFT, 4, 12);

    c_time = lv_label_create(parent);
    lv_obj_set_style_text_font(c_time, &noto_num_64, 0);
    lv_obj_set_style_text_color(c_time, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(c_time, "--:--");
    lv_obj_align(c_time, LV_ALIGN_LEFT_MID, 4, 0);

    /* 状态行:flex 行容器,子项自动横排 */
    c_row = lv_obj_create(parent);
    lv_obj_set_size(c_row, SLOT_W - 8, 22);
    lv_obj_align(c_row, LV_ALIGN_BOTTOM_LEFT, 4, -8);
    lv_obj_set_style_bg_opa(c_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c_row, 0, 0);
    lv_obj_set_style_pad_all(c_row, 0, 0);
    lv_obj_set_style_pad_column(c_row, 14, 0);
    lv_obj_set_flex_flow(c_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(c_row, LV_OBJ_FLAG_SCROLLABLE);

    c_batt = lv_label_create(c_row);
    lv_obj_set_style_text_font(c_batt, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_batt, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_batt, "--%");

    /* WiFi:图标 + 右下红叹号(未连接叠加;连接后隐藏叹号) */
    lv_obj_t *wifi_box = lv_obj_create(c_row);
    lv_obj_set_size(wifi_box, 30, 22);
    lv_obj_set_style_bg_opa(wifi_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_box, 0, 0);
    lv_obj_set_style_pad_all(wifi_box, 0, 0);
    lv_obj_clear_flag(wifi_box, LV_OBJ_FLAG_SCROLLABLE);
    c_wifi = lv_label_create(wifi_box);
    lv_obj_set_style_text_font(c_wifi, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_wifi, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(c_wifi, LV_ALIGN_LEFT_MID, 0, 0);
    c_wifi_excl = lv_label_create(wifi_box);
    lv_obj_set_style_text_font(c_wifi_excl, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_wifi_excl, lv_color_hex(UI_COL_ORANGE), 0);
    lv_label_set_text(c_wifi_excl, "!");
    lv_obj_align_to(c_wifi_excl, c_wifi, LV_ALIGN_OUT_RIGHT_BOTTOM, -2, 3);

    /* 蓝牙:未连接整体隐藏 */
    c_bt = lv_label_create(c_row);
    lv_obj_set_style_text_font(c_bt, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_bt, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_bt, LV_SYMBOL_BLUETOOTH);

    /* 闹钟:行尾 */
    c_alarm = lv_label_create(c_row);
    lv_obj_set_style_text_font(c_alarm, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_alarm, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_alarm, "-");

    /* 按上次钩子状态应用显隐（切卡重建时沿用,否则已联网仍显红叹号） */
    (s_wifi_ok ? lv_obj_add_flag : lv_obj_clear_flag)(c_wifi_excl, LV_OBJ_FLAG_HIDDEN);
    (s_bt_ok ? lv_obj_clear_flag : lv_obj_add_flag)(c_bt, LV_OBJ_FLAG_HIDDEN);
    return NULL;
}

/* 卡片层销毁前收尾:清零本卡持有的控件句柄。不清则滑走后句柄悬空,
 * home_cards_set_net 会写已释放对象——WiFi 连不上时每 2.4s 重试一次、每次发
 * NET_STATE 事件,恰逢时钟卡被滑走即触发（"滑动卡片有概率死机",2026-11-26 用户报）。
 * 仅当句柄确属被删卡片层时才清,防误清别槽新建的同名卡。 */
static void card_clock_destroy(lv_obj_t *card)
{
    if (c_time != NULL && lv_obj_get_parent(c_time) == card) {
        c_time = c_date = c_row = c_batt = c_wifi = c_wifi_excl = c_bt = c_alarm = NULL;
    }
}

/* ================================================================== */
/* 卡片二：天气（占位,U8 WiFi+和风接入后填真数据）                        */
/* ================================================================== */
static void card_weather_refresh(void) { /* 占位:无数据可刷 */ }

static lv_obj_t *card_weather_create(lv_obj_t *parent)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_obj_set_style_text_font(icon, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COL_SKY), 0);
    lv_label_set_text(icon, LV_SYMBOL_GPS);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 4, 34);

    lv_obj_t *temp = lv_label_create(parent);
    lv_obj_set_style_text_font(temp, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(temp, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(temp, "--°");
    lv_obj_align(temp, LV_ALIGN_TOP_LEFT, 40, 34);

    lv_obj_t *desc = lv_label_create(parent);
    lv_obj_set_style_text_font(desc, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(desc, "天气 · 待接入");
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 4, 78);
    return NULL;
}

/* ================================================================== */
/* 卡片三：倒计时摘要（app_timer 状态/剩余;点按进沙漏页 U5 后加）          */
/* ================================================================== */
static lv_obj_t *c_hg_state, *c_hg_time;

static void card_timer_refresh(void)
{
    const uint32_t remain = app_timer_get_remain_s();
    switch (app_timer_get_state()) {
    case LL_TIMER_RUNNING:
        lv_label_set_text(c_hg_state, "倒计时运行中");
        lv_obj_set_style_text_color(c_hg_state, lv_color_hex(UI_COL_GREEN), 0);
        lv_label_set_text_fmt(c_hg_time, "%02u:%02u",
                              (unsigned)(remain / 60), (unsigned)(remain % 60));
        break;
    case LL_TIMER_PAUSED:
        lv_label_set_text(c_hg_state, "倒计时已暂停");
        lv_obj_set_style_text_color(c_hg_state, lv_color_hex(UI_COL_ORANGE), 0);
        lv_label_set_text_fmt(c_hg_time, "%02u:%02u",
                              (unsigned)(remain / 60), (unsigned)(remain % 60));
        break;
    case LL_TIMER_DONE:
        lv_label_set_text(c_hg_state, "倒计时结束");
        lv_obj_set_style_text_color(c_hg_state, lv_color_hex(UI_COL_FG), 0);
        lv_label_set_text(c_hg_time, "00:00");
        break;
    default:
        lv_label_set_text(c_hg_state, "倒计时未启动");
        lv_obj_set_style_text_color(c_hg_state, lv_color_hex(UI_COL_WEAK), 0);
        lv_label_set_text(c_hg_time, "00:00");   /* 用户定稿:未启动显示 00:00 */
        break;
    }
}

static lv_obj_t *card_timer_create(lv_obj_t *parent)
{
    /* 2026-11-21 用户定稿：小字(状态行)贴顶,时间数字 LEFT_MID 居中——与时钟卡对齐 */
    c_hg_state = lv_label_create(parent);
    lv_obj_set_style_text_font(c_hg_state, UI_FONT_TITLE, 0);   /* 24px,与时钟卡日期同字号 */
    lv_obj_set_style_text_color(c_hg_state, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_hg_state, "倒计时未启动");
    lv_obj_align(c_hg_state, LV_ALIGN_TOP_LEFT, 4, 12);   /* 与时钟卡日期(y12)完全同位 */

    c_hg_time = lv_label_create(parent);
    lv_obj_set_style_text_font(c_hg_time, &noto_num_64, 0);
    lv_obj_set_style_text_color(c_hg_time, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(c_hg_time, "00:00");
    lv_obj_align(c_hg_time, LV_ALIGN_LEFT_MID, 4, 0);   /* 与时钟卡时间同位 */
    return NULL;
}

/* ================================================================== */
/* 卡片四：提醒摘要（app_reminder 引擎未做,占位）                          */
/* ================================================================== */
static void card_reminder_refresh(void) { /* 引擎 U9 接入后实现 */ }

static lv_obj_t *card_reminder_create(lv_obj_t *parent)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_obj_set_style_text_font(icon, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COL_DIM), 0);
    lv_label_set_text(icon, LV_SYMBOL_LIST);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 4, 34);

    lv_obj_t *desc = lv_label_create(parent);
    lv_obj_set_style_text_font(desc, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(desc, "提醒 · 待接入");
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 4, 78);
    return NULL;
}

/* ================================================================== */
/* 卡片注册表（新卡片在此登记即可被所有槽显示）                            */
/* ================================================================== */
static const home_card_t k_cards[] = {
    { .name = "时钟",  .create = card_clock_create,    .refresh = card_clock_refresh, .destroy = card_clock_destroy },
    { .name = "天气",  .create = card_weather_create,  .refresh = card_weather_refresh },
    { .name = "倒计时", .create = card_timer_create,   .refresh = card_timer_refresh },
    { .name = "提醒",  .create = card_reminder_create, .refresh = card_reminder_refresh },
};
#define CARD_CNT (sizeof(k_cards) / sizeof(k_cards[0]))

/* ---- 槽 ---- */
typedef struct {
    lv_obj_t *cont;      /* 槽容器(固定,卡片层挂其内) */
    lv_obj_t *card;      /* 当前卡层(整体可滑动,切换=滑出删旧+滑入建新) */
    int idx;             /* 当前卡索引 */
} slot_t;
static slot_t s_slots[3];
#define SLOT_CNT 3

/* 初始排列:槽0=时钟,槽1=天气,槽2=倒计时(提醒从任一槽滑出) */
static const int k_slot_init[SLOT_CNT] = { 0, 1, 2 };

static void card_slide_cb(void *obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void card_slide_del_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    const int idx = (int)(intptr_t)lv_obj_get_user_data(obj);   /* slot_load 写入的旧卡索引 */
    if (k_cards[idx].destroy) k_cards[idx].destroy(obj);        /* 先清句柄,再删对象 */
    lv_obj_delete(obj);
}

/* 卡片控件为单例（static 句柄）——同一卡不得同时在两槽显示:
 * 装载时若目标卡已被其他槽占用,顺延到下一张未占用卡 */
static bool card_in_use(int idx, const slot_t *except)
{
    for (int i = 0; i < SLOT_CNT; i++) {
        if (&s_slots[i] != except && s_slots[i].idx == idx) return true;
    }
    return false;
}

/* 装载卡 idx 进槽 s,dir=触发滑动方向(TOP=新卡从下滑入/BOTTOM=从上滑入;
 * LV_DIR_NONE=初次构建,无动画)。旧卡反向滑出后自动销毁。 */
static void slot_load(slot_t *s, int idx, lv_dir_t dir)
{
    const int old_idx = s->idx;    /* 旧卡索引须在改写前留存（销毁收尾钩子要用） */
    s->idx = idx;

    /* 旧卡层:反向滑出 + 动画完成销毁（无动画直接删）;两条路径删除前都先调卡片收尾钩子清句柄 */
    if (s->card) {
        lv_obj_t *old = s->card;
        if (dir == LV_DIR_NONE) {
            if (k_cards[old_idx].destroy) k_cards[old_idx].destroy(old);
            lv_obj_delete(old);
        } else {
            lv_obj_set_user_data(old, (void *)(intptr_t)old_idx);   /* 滑出动画完成时的收尾钩子要读 */
            const int32_t out_y = (dir == LV_DIR_TOP) ? -158 : 158;
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, old);
            lv_anim_set_exec_cb(&a, card_slide_cb);
            lv_anim_set_values(&a, 0, out_y);
            lv_anim_set_duration(&a, 160);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_completed_cb(&a, card_slide_del_cb);
            lv_anim_start(&a);
        }
    }

    /* 新卡层:整槽尺寸透明容器,起始偏移滑入归位 */
    s->card = lv_obj_create(s->cont);
    lv_obj_set_size(s->card, SLOT_W, 158);
    lv_obj_set_pos(s->card, 0, 0);
    lv_obj_set_style_bg_opa(s->card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s->card, 0, 0);
    lv_obj_set_style_pad_all(s->card, 0, 0);
    lv_obj_clear_flag(s->card, LV_OBJ_FLAG_SCROLLABLE);
    k_cards[idx].create(s->card);
    k_cards[idx].refresh();

    if (dir != LV_DIR_NONE) {
        const int32_t in_y = (dir == LV_DIR_TOP) ? 158 : -158;
        lv_obj_set_y(s->card, in_y);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s->card);
        lv_anim_set_exec_cb(&a, card_slide_cb);
        lv_anim_set_values(&a, in_y, 0);
        lv_anim_set_duration(&a, 160);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

/* ================================================================== */
/* 公共接口                                                             */
/* ================================================================== */
void home_cards_build(lv_obj_t *screen)
{
    for (int i = 0; i < SLOT_CNT; i++) {
        s_slots[i].cont = lv_obj_create(screen);
        lv_obj_set_size(s_slots[i].cont, SLOT_W, 158);
        lv_obj_set_pos(s_slots[i].cont, SLOT_X(i), 7);
        lv_obj_set_style_bg_opa(s_slots[i].cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_slots[i].cont, 0, 0);
        lv_obj_set_style_pad_all(s_slots[i].cont, 0, 0);
        lv_obj_clear_flag(s_slots[i].cont, LV_OBJ_FLAG_SCROLLABLE);
        slot_load(&s_slots[i], k_slot_init[i], LV_DIR_NONE);
    }

    /* 三等分淡分隔线 ×2（槽缝中间,上下不到屏幕边缘——沿用三栏版样式） */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *line = lv_obj_create(screen);
        lv_obj_set_size(line, 1, 144);
        lv_obj_set_pos(line, SLOT_X(i + 1) - 3, 14);   /* 缝中线:下槽起点-6/2 */
        lv_obj_set_style_bg_color(line, lv_color_hex(0x1B2330), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_pad_all(line, 0, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    ESP_LOGI(TAG, "3 slots x %u cards ready", (unsigned)CARD_CNT);
}

void home_cards_on_gesture(lv_dir_t dir, int32_t start_x)
{
    if (dir != LV_DIR_TOP && dir != LV_DIR_BOTTOM) return;
    if (start_x < 0) return;

    const int slot_i = start_x / (640 / SLOT_CNT);    /* 起点定槽 */
    if (slot_i < 0 || slot_i >= SLOT_CNT) return;
    slot_t *s = &s_slots[slot_i];

    /* 上滑(TOP)=下一张(新卡从下滑入),下滑(BOTTOM)=上一张;跳过占用卡 */
    const int step = (dir == LV_DIR_TOP) ? 1 : -1;
    for (int k = 1; k <= (int)CARD_CNT; k++) {
        const int cand = (s->idx + step * k + (int)CARD_CNT * 4) % (int)CARD_CNT;
        if (!card_in_use(cand, s)) {
            slot_load(s, cand, dir);
            break;
        }
    }
    ESP_LOGI(TAG, "slot%d -> %s", slot_i, k_cards[s->idx].name);
}

void home_cards_tick(void)
{
    for (int i = 0; i < SLOT_CNT; i++) {
        k_cards[s_slots[i].idx].refresh();
    }
}

void home_cards_set_battery(const ll_battery_state_t *st)
{
    if (st) s_batt_snap = *st;
}

void home_cards_set_net(bool wifi_ok, bool bt_ok)
{
    s_wifi_ok = wifi_ok;
    s_bt_ok = bt_ok;
    if (c_wifi == NULL) return;              /* 时钟卡未创建(不在任何槽) */
    (wifi_ok ? lv_obj_add_flag : lv_obj_clear_flag)(c_wifi_excl, LV_OBJ_FLAG_HIDDEN);
    (bt_ok ? lv_obj_clear_flag : lv_obj_add_flag)(c_bt, LV_OBJ_FLAG_HIDDEN);
}
