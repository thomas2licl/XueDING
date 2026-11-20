/* app_ui — UI 主页 v1（S2.4,首个正式界面;U1 起挂入横滑导航第 1 页）
 *
 * 布局（640×172 横条,深色夜间友好）——**夜间模式基准样式,勿删（需求 V0.8）**：
 *   左：时间大数字 HH:MM（48px,冒号每秒闪烁示活）
 *   中：日期 + 星期 + 闹钟数
 *   右：电池（图标档位+百分比,充电⚡标注）
 *   顶：响铃横幅（闹钟触发时弹出,STOP 停铃;30s 无操作自灭）
 * 中文字体未引入（U2 规划中）,日期星期暂用数字/英文。
 * 页面级布局/配色在 U3 主页 v2（设计稿三栏）时拆日间/夜间两套,本文件保留为夜间版。 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

#include "bsp_lcd.h"
#include "app_event.h"
#include "app_power.h"
#include "app_clock.h"
#include "app_alarm.h"
#include "app_ui.h"
#include "app_ui_nav.h"
#include "ui_page_alarms.h"
#include "ui_page_hourglass.h"

/* ============ 开机图（2026-11-19 素材已接入）============
 * 素材 2048×768 PNG → 640×172 RGB565 小端（转换脚本处理,见 boot_logo.c 注释）,
 * 开机显示 3s 后自动进入主界面;BOOT_LOGO_ENABLE 置 0 可跳过（调试/产测） */
extern const lv_image_dsc_t boot_logo_dsc;
#define BOOT_LOGO_ENABLE    1
#define BOOT_LOGO_IMG       (&boot_logo_dsc)
#define BOOT_LOGO_SHOW_MS   3000

/* ---- 中文字体（U2,fonts/ 目录,lv_font_conv 生成,再生成方法见 tools/fonts/）----
 * noto_sans_sc_16/24：GB2312 一级 3755 字+UI 符号（覆盖天气描述/语音识别任意中文）
 * noto_num_64：64px 数字等宽（adv_w 全 568,时间跳动不抖;仅 0-9 : % - +） */
LV_FONT_DECLARE(noto_sans_sc_16);
LV_FONT_DECLARE(noto_sans_sc_24);
LV_FONT_DECLARE(noto_num_64);

static const char *TAG = "app_ui";

/* ---- 主页控件 ---- */
static lv_obj_t *s_time_label;     /* HH:MM 大数字 */
static lv_obj_t *s_date_label;     /* 2026-11-18 */
static lv_obj_t *s_wday_label;     /* WED  🔔n */
static lv_obj_t *s_batt_label;     /* 电池 */
static lv_obj_t *s_alarm_banner;   /* 响铃横幅（默认隐藏） */
static lv_timer_t *s_home_timer;   /* 1s 刷新（导航 on_hide 挂起/on_show 恢复） */

static uint8_t s_last_sec = 0xFF;

/* ---- 配色 ---- */
#define COL_BG      0x101418
#define COL_FG      0xE8EAED
#define COL_DIM     0x9AA0A6
#define COL_ACCENT  0x8AB4F8

static const char *const k_weekday[7] = { "日", "一", "二", "三", "四", "五", "六" };

/* ------------------------------------------------------------------ */
/* 开机图（须持 LVGL 锁调用）：立绘一帧 → 让锁延时 → 重新持锁返回 */
/* ------------------------------------------------------------------ */
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
    /* 结束后不删图：app_ui_nav_init 直接切到主页 screen,boot 屏整体退场无残影 */
#endif
}

/* ------------------------------------------------------------------ */
/* 主页 1s 刷新（lv_timer 回调跑在 LVGL 任务=锁内）                     */
/* ------------------------------------------------------------------ */
static void home_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    const bsp_rtc_datetime_t *now = app_clock_get_time();
    if (now == NULL) {
        lv_label_set_text(s_time_label, "--:--");
        return;
    }

    if (now->second != s_last_sec) {          /* 每秒一次（冒号闪烁+秒变化） */
        s_last_sec = now->second;
        lv_label_set_text_fmt(s_time_label, "%02u%c%02u",
                              now->hour, (now->second & 1) ? ':' : ' ', now->minute);

        /* 日期/星期/闹钟数只在分钟跳变时更新（避免每秒无谓重绘 2 个区域） */
        static uint8_t last_min = 0xFF;
        if (now->minute != last_min) {
            last_min = now->minute;
            lv_label_set_text_fmt(s_date_label, "%u月%02u日",
                                  now->month, now->day);
            ll_alarm_t alarms[APP_ALARM_MAX];
            const int n = app_alarm_get_all(alarms, APP_ALARM_MAX);
            int enabled = 0;
            for (int i = 0; i < n; i++) {
                if (alarms[i].enabled) enabled++;
            }
            lv_label_set_text_fmt(s_wday_label, "星期%s  " LV_SYMBOL_BELL " %u",
                                  k_weekday[now->weekday % 7], enabled);
        }
    }

    /* 响铃横幅随状态显隐（30s 无操作 app_alarm 自停后横幅收起） */
    const bool ringing = app_alarm_is_ringing();
    const bool shown = !lv_obj_has_flag(s_alarm_banner, LV_OBJ_FLAG_HIDDEN);
    if (ringing != shown) {
        (ringing ? lv_obj_clear_flag : lv_obj_add_flag)(s_alarm_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ------------------------------------------------------------------ */
/* 事件回调（app_power/app_alarm 任务上下文,操作 LVGL 须持锁）           */
/* ------------------------------------------------------------------ */
static void on_battery(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id);
    const ll_battery_state_t *st = (const ll_battery_state_t *)data;
    if (st == NULL) return;
    if (!bsp_lcd_lock(200)) return;

    /* 电量分档图标（LVGL 内置 symbol,含于 montserrat 字体） */
    const char *icon = LV_SYMBOL_BATTERY_FULL;
    if (st->percent < 20)      icon = LV_SYMBOL_BATTERY_1;
    else if (st->percent < 50) icon = LV_SYMBOL_BATTERY_2;
    else if (st->percent < 90) icon = LV_SYMBOL_BATTERY_3;
    lv_label_set_text_fmt(s_batt_label, "%s%s %u%%",
                          st->charging ? LV_SYMBOL_CHARGE : "",
                          icon, (unsigned)st->percent);
    if (st->vbat_mv <= 3300) {
        lv_obj_set_style_text_color(s_batt_label, lv_color_hex(0xF28B82), 0);   /* 低电红 */
    } else {
        lv_obj_set_style_text_color(s_batt_label, lv_color_hex(COL_FG), 0);
    }
    bsp_lcd_unlock();
}

static void on_orientation(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id);
    ESP_LOGI(TAG, "event bus OK: ORIENTATION -> %d", *(int *)data);
}

static void on_alarm_fired(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg); LV_UNUSED(base); LV_UNUSED(id);
    ESP_LOGI(TAG, "event bus OK: ALARM_FIRED, id=%d", *(int *)data);
    /* 横幅显隐由 home_timer_cb 每秒检查 is_ringing 统一处理（避免此处重复持锁逻辑） */
}

/* 响铃横幅 STOP 按钮 */
static void alarm_stop_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_alarm_stop();
    lv_obj_add_flag(s_alarm_banner, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------ */
/* 主页构建（独立 screen,U1 起由 app_ui_nav 持有）                        */
/* ------------------------------------------------------------------ */
static lv_obj_t *home_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COL_BG), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 时间大数字（中偏左,垂直居中）——64px 自定义等宽数字（U2）。
     * 注意:勿用 transform_scale 放大文本——变换对象走图层渲染路径
     * （每帧 malloc 大缓冲+重绘,LVGL 任务占满 core0 饿死 IDLE 触发看门狗,
     * 2026-11-18 实测） */
    s_time_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_time_label, &noto_num_64, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(COL_FG), 0);
    lv_label_set_text(s_time_label, "--:--");
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, -150, 0);

    /* 日期（中,中文 24px） */
    s_date_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_date_label, &noto_sans_sc_24, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(s_date_label, "--月--日");
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 40, -32);

    /* 星期 + 闹钟数（中,日期下方,中文 24px） */
    s_wday_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_wday_label, &noto_sans_sc_24, 0);
    lv_obj_set_style_text_color(s_wday_label, lv_color_hex(COL_ACCENT), 0);
    lv_label_set_text(s_wday_label, "星期-");
    lv_obj_align(s_wday_label, LV_ALIGN_CENTER, 40, 6);

    /* 电池（右） */
    s_batt_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_batt_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_batt_label, lv_color_hex(COL_FG), 0);
    lv_label_set_text(s_batt_label, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_align(s_batt_label, LV_ALIGN_RIGHT_MID, -30, 0);

    /* 响铃横幅（顶部,默认隐藏） */
    s_alarm_banner = lv_obj_create(screen);
    lv_obj_set_size(s_alarm_banner, 640, 40);
    lv_obj_align(s_alarm_banner, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_alarm_banner, lv_color_hex(0xB3261E), 0);
    lv_obj_set_style_border_width(s_alarm_banner, 0, 0);
    lv_obj_set_style_radius(s_alarm_banner, 0, 0);
    lv_obj_clear_flag(s_alarm_banner, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *banner_text = lv_label_create(s_alarm_banner);
    lv_obj_set_style_text_font(banner_text, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(banner_text, lv_color_white(), 0);
    lv_label_set_text(banner_text, LV_SYMBOL_BELL " Alarm");
    lv_obj_align(banner_text, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *btn_stop = lv_button_create(s_alarm_banner);
    lv_obj_set_size(btn_stop, 90, 30);
    lv_obj_align(btn_stop, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(btn_stop, alarm_stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lab = lv_label_create(btn_stop);
    lv_label_set_text(btn_lab, "STOP");
    lv_obj_center(btn_lab);

    lv_obj_add_flag(s_alarm_banner, LV_OBJ_FLAG_HIDDEN);

    /* 1s 刷新（时间/秒闪烁/响铃横幅） */
    s_home_timer = lv_timer_create(home_timer_cb, 1000, NULL);
    return screen;
}

/* 导航生命周期：离开主页挂起刷新,回主页恢复（LVGL 任务上下文,免锁） */
static void home_on_hide(void)
{
    if (s_home_timer) lv_timer_pause(s_home_timer);
}

static void home_on_show(void)
{
    if (s_home_timer) lv_timer_resume(s_home_timer);
}

/* ------------------------------------------------------------------ */
/* 一级页面表（UI 跟踪 §1 顺序定稿;占位页随 U3~U11 逐页替换为真页面）      */
/* ------------------------------------------------------------------ */
static lv_obj_t *ph_weather(void)  { return app_ui_nav_placeholder("天气",  LV_SYMBOL_GPS); }
static lv_obj_t *ph_reminders(void){ return app_ui_nav_placeholder("提醒",   LV_SYMBOL_LIST); }
static lv_obj_t *ph_sound(void)    { return app_ui_nav_placeholder("声音",   LV_SYMBOL_AUDIO); }
static lv_obj_t *ph_photos(void)   { return app_ui_nav_placeholder("相册",   LV_SYMBOL_IMAGE); }
static lv_obj_t *ph_nightlight(void){ return app_ui_nav_placeholder("夜灯",  LV_SYMBOL_EYE_OPEN); }
static lv_obj_t *ph_settings(void) { return app_ui_nav_placeholder("设置",   LV_SYMBOL_SETTINGS); }

static const ui_page_def_t k_pages[UI_PAGE_COUNT] = {
    { .name = "主页",  .create = home_create, .on_show = home_on_show, .on_hide = home_on_hide },
    { .name = "天气",  .create = ph_weather },
    { .name = "闹钟",  .create = ui_alarms_create, .on_show = ui_alarms_on_show },
    { .name = "提醒",  .create = ph_reminders },
    { .name = "声音",  .create = ph_sound },
    { .name = "相册",  .create = ph_photos },
    { .name = "沙漏",  .create = ui_hourglass_create, .on_show = ui_hourglass_on_show, .on_hide = ui_hourglass_on_hide },
    { .name = "夜灯",  .create = ph_nightlight },
    { .name = "设置",  .create = ph_settings },
};

esp_err_t app_ui_init(void)
{
    show_boot_logo();
    app_ui_nav_init(k_pages, UI_PAGE_COUNT);   /* 建 9 页+页点+手势,瞬时进主页 */

    app_event_subscribe(LL_EV_BATTERY_STATE, on_battery, NULL);
    app_event_subscribe(LL_EV_ORIENTATION, on_orientation, NULL);
    app_event_subscribe(LL_EV_ALARM_FIRED, on_alarm_fired, NULL);

    ESP_LOGI(TAG, "UI ready: home v1 + swipe nav (%d pages)", UI_PAGE_COUNT);
    return ESP_OK;
}
