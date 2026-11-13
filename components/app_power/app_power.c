/* app_power — 电源管理服务（S1.6 实体化：电量采集与事件）
 * 5s 周期：VBAT 采集 → 百分比查表 → 串口日志 + LL_EV_BATTERY_STATE 上总线。
 * 低电保护（用户需求 2026-11-13）：≤3.30V 屏幕提示 3s 后强制关机（防过放）;
 * 开机后 1s 首检（同样阈值,低电电池按键开机后即提示关机）。
 * KEY1 长按 2s 软件关机（USB 供电时关机无效——USB 强制上电,属硬件行为）。 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_power.h"
#include "app_event.h"
#include "bsp_power.h"

static const char *TAG = "app_power";

#define POWER_POLL_PERIOD_MS    5000    /* 采集周期 */
#define POWER_BOOT_DELAY_MS     1000    /* 开机后首测延时（低电保护尽早介入） */
#define KEY_SHUTDOWN_HOLD_MS    2000    /* KEY1 长按关机阈值 */
#define VBAT_FORCE_OFF_MV       3300    /* ≤此值强制关机（屏幕提示 3s） */

/* 锂电电压→百分比查表（S1.6.2 标定前为典型曲线初版,按 3.45~4.20V 十档） */
static uint8_t vbat_to_percent(uint16_t mv)
{
    static const struct { uint16_t mv; uint8_t pct; } table[] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3930, 70}, {3870, 60},
        {3820, 50},  {3780, 40}, {3700, 30}, {3600, 20}, {3450, 10},
    };
    for (int i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++) {
        if (mv >= table[i].mv) return table[i].pct;
    }
    return 5;   /* <3.45V:濒临关机 */
}

static void power_task(void *arg)
{
    bool first = true;
    ll_battery_state_t st = {0};

    for (;;) {
        /* 开机后 1s 首测（低电电池开机即保护）,此后 5s 周期 */
        vTaskDelay(pdMS_TO_TICKS(first ? POWER_BOOT_DELAY_MS : POWER_POLL_PERIOD_MS));
        first = false;

        uint16_t mv = 0;
        if (bsp_power_read_vbat_mv(&mv) != ESP_OK) continue;
        st.vbat_mv = mv;
        st.percent = vbat_to_percent(mv);
        st.charging = bsp_power_is_charging();

        /* 串口日志（每周期一行,S1 验收口径） */
        ESP_LOGI(TAG, "vbat=%umV (~%u%%) %s",
                 (unsigned)st.vbat_mv, (unsigned)st.percent,
                 st.charging ? "CHARGING" : "battery/idle");

        /* 每周期发事件（S1.6 屏显 + S2 UI 订阅同源） */
        app_event_post(LL_EV_BATTERY_STATE, &st, sizeof(st), 0);

        /* 低电强制关机：≤3.30V 屏幕提示（事件已带最新值）3s 后断电。
         * 充电中（USB 在线）豁免——充电可救回 */
        if (mv <= VBAT_FORCE_OFF_MV && !st.charging) {
            ESP_LOGW(TAG, "LOW BATTERY %umV! shutdown in 3s", (unsigned)mv);
            vTaskDelay(pdMS_TO_TICKS(3000));
            bsp_power_shutdown();
            /* 电池模式此时断电;若期间插入 USB 则不会断——休眠等待人工处理 */
            for (;;) vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }
}

/* KEY1 长按关机检测（快速轮询,100ms 粒度） */
static void key_task(void *arg)
{
    int hold_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (bsp_power_is_key_pressed()) {
            hold_ms += 100;
            if (hold_ms >= KEY_SHUTDOWN_HOLD_MS) {
                ESP_LOGW(TAG, "KEY1 long-press: shutdown");
                bsp_power_shutdown();
                hold_ms = -1000000;   /* 等待断电;USB 模式则不再重复触发 */
            }
        }
        else {
            hold_ms = 0;
        }
    }
}

esp_err_t app_power_init(void)
{
    /* 电源硬件失败不阻塞整机（电源功能降级） */
    esp_err_t err = bsp_power_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power hw init failed, service degraded: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    if (xTaskCreate(power_task, "pwr_mon", 3 * 1024, NULL, 2, NULL) != pdPASS ||
        xTaskCreate(key_task, "pwr_key", 2 * 1024, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create task failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "power service running (poll %dms)", POWER_POLL_PERIOD_MS);
    return ESP_OK;
}
