/* ui_home_cards — 主页三槽卡片系统实现（U3b）
 * 卡片接口与槽位机制见 ui_home_cards.h。当前 4 张卡为临时样式
 * （时钟卡2/天气/倒计时/提醒）——用户设计定稿后逐卡重写内部 UI,框架不动。 */

#include <stdio.h>

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
static lv_obj_t *c_time, *c_date, *c_alarm, *c_status;
static ll_battery_state_t s_batt_snap;        /* 电池快照（on_battery 事件推送） */

static void card_clock_refresh(void)
{
    const bsp_rtc_datetime_t *now = app_clock_get_time();
    if (now == NULL) { lv_label_set_text(c_time, "--:--"); return; }

    static uint8_t last_sec = 0xFF, last_min = 0xFF;
    if (now->second != last_sec) {
        last_sec = now->second;
        lv_label_set_text_fmt(c_time, "%02u%c%02u",
                              now->hour, (now->second & 1) ? ':' : ' ', now->minute);

        if (now->minute != last_min) {
            last_min = now->minute;
            static const char *const wd[7] = { "日", "一", "二", "三", "四", "五", "六" };
            lv_label_set_text_fmt(c_date, "%u月%02u日 星期%s",
                                  now->month, now->day, wd[now->weekday % 7]);

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
            } else {
                lv_label_set_text(c_alarm, "无闹钟");
            }
        }
    }

    /* 状态行:电池(快照) + WiFi(未接入恒离线) + 蓝牙(未接入恒离线) */
    const char *bicon = LV_SYMBOL_BATTERY_FULL;
    if (s_batt_snap.percent < 20)      bicon = LV_SYMBOL_BATTERY_1;
    else if (s_batt_snap.percent < 50) bicon = LV_SYMBOL_BATTERY_2;
    else if (s_batt_snap.percent < 90) bicon = LV_SYMBOL_BATTERY_3;
    lv_label_set_text_fmt(c_status, "%s%s%u%%  " LV_SYMBOL_WIFI "  " LV_SYMBOL_BLUETOOTH,
                          s_batt_snap.charging ? LV_SYMBOL_CHARGE : "",
                          bicon, (unsigned)s_batt_snap.percent);
    lv_obj_set_style_text_color(c_status,
        lv_color_hex(s_batt_snap.vbat_mv <= 3300 ? UI_COL_RED : UI_COL_WEAK), 0);
}

static lv_obj_t *card_clock_create(lv_obj_t *parent)
{
    c_date = lv_label_create(parent);
    lv_obj_set_style_text_font(c_date, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(c_date, lv_color_hex(UI_COL_DIM), 0);
    lv_label_set_text(c_date, "--月--日");
    lv_obj_align(c_date, LV_ALIGN_TOP_LEFT, 4, 6);

    c_time = lv_label_create(parent);
    lv_obj_set_style_text_font(c_time, &noto_num_64, 0);
    lv_obj_set_style_text_color(c_time, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(c_time, "--:--");
    lv_obj_align(c_time, LV_ALIGN_TOP_LEFT, 4, 40);

    c_alarm = lv_label_create(parent);
    lv_obj_set_style_text_font(c_alarm, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_alarm, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_alarm, "无闹钟");
    lv_obj_align(c_alarm, LV_ALIGN_TOP_LEFT, 4, 122);

    c_status = lv_label_create(parent);
    lv_obj_set_style_text_font(c_status, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_status, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_status, "--%");
    lv_obj_align(c_status, LV_ALIGN_TOP_LEFT, 4, 146);
    return NULL;
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
        lv_label_set_text(c_hg_time, "--:--");
        break;
    }
}

static lv_obj_t *card_timer_create(lv_obj_t *parent)
{
    c_hg_state = lv_label_create(parent);
    lv_obj_set_style_text_font(c_hg_state, UI_FONT_TEXT, 0);
    lv_obj_set_style_text_color(c_hg_state, lv_color_hex(UI_COL_WEAK), 0);
    lv_label_set_text(c_hg_state, "倒计时未启动");
    lv_obj_align(c_hg_state, LV_ALIGN_TOP_LEFT, 4, 34);

    c_hg_time = lv_label_create(parent);
    lv_obj_set_style_text_font(c_hg_time, &noto_num_64, 0);
    lv_obj_set_style_text_color(c_hg_time, lv_color_hex(UI_COL_FG), 0);
    lv_label_set_text(c_hg_time, "--:--");
    lv_obj_align(c_hg_time, LV_ALIGN_TOP_LEFT, 4, 62);
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
    { .name = "时钟",  .create = card_clock_create,    .refresh = card_clock_refresh },
    { .name = "天气",  .create = card_weather_create,  .refresh = card_weather_refresh },
    { .name = "倒计时", .create = card_timer_create,   .refresh = card_timer_refresh },
    { .name = "提醒",  .create = card_reminder_create, .refresh = card_reminder_refresh },
};
#define CARD_CNT (sizeof(k_cards) / sizeof(k_cards[0]))

/* ---- 槽 ---- */
typedef struct {
    lv_obj_t *cont;      /* 槽容器(卡片父,卡片切换=重建内容) */
    int idx;             /* 当前卡索引 */
} slot_t;
static slot_t s_slots[3];
#define SLOT_CNT 3

/* 初始排列:槽0=时钟,槽1=天气,槽2=倒计时(提醒从任一槽滑出) */
static const int k_slot_init[SLOT_CNT] = { 0, 1, 2 };

static void card_fade_in_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
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

/* 装载卡 idx 进槽 s（重建内容+淡入+立即刷一次数据） */
static void slot_load(slot_t *s, int idx)
{
    s->idx = idx;
    lv_obj_clean(s->cont);                       /* 旧卡控件全清 */
    lv_obj_set_style_opa(s->cont, LV_OPA_TRANSP, 0);
    k_cards[idx].create(s->cont);
    k_cards[idx].refresh();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s->cont);
    lv_anim_set_exec_cb(&a, card_fade_in_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 150);
    lv_anim_start(&a);
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
        slot_load(&s_slots[i], k_slot_init[i]);
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

    /* 上滑(TOP)=下一张,下滑(BOTTOM)=上一张,循环;跳过其他槽正在显示的卡 */
    const int step = (dir == LV_DIR_TOP) ? 1 : -1;
    for (int k = 1; k <= (int)CARD_CNT; k++) {
        const int cand = (s->idx + step * k + (int)CARD_CNT * 4) % (int)CARD_CNT;
        if (!card_in_use(cand, s)) {
            slot_load(s, cand);
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
