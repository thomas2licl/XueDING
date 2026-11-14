/* app_power — 电源管理服务（S1.6 实体化,S1.6.6 改版：电压沿检测）
 * 5s 周期：VBAT 采集 → 百分比查表 → 串口日志 + LL_EV_BATTERY_STATE 上总线。
 * USB 在线检测（纯软件,零硬件,2026-11-14 用户方案,替代已弃用的 STAT 飞线）：
 *   插 USB 瞬间充电电流抬升端电压 +0.1V 级;拔出瞬间回落（满电最弱 ~0.03V）;
 *   正常放电缓变 ≤2mV/5s——按相邻采样差值的阶跃沿判定插拔。
 *   去抖采用"候选+一拍验证"：超阈值拍记为候选,下一拍出现 ≥15mV 反向回落
 *   则判负载瞬变假沿丢弃,否则确认（跳变只持续一拍,"连续两拍超阈"不成立）。
 * 低电保护：≤3.30V 屏幕提示 3s 强制关机（USB 在线豁免）;开机 1s 首检。
 * KEY1 长按 2s 软件关机（USB 供电时无效——USB 强制上电,硬件行为）。 */

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

/* 电压沿检测参数（阈值经噪声 ±12mV/缓变 2mV/满电弱沿 30mV 三方权衡） */
#define USB_PLUGIN_TH_MV        50      /* 上沿阈值:插 USB 瞬间抬升 */
#define USB_PLUGOUT_TH_MV       25      /* 下沿阈值:拔出回落（覆盖满电弱沿） */
#define USB_EDGE_CANCEL_MV      15      /* 候选后一拍反向回落此幅度 → 假沿丢弃 */
#define USB_INIT_ONLINE_MV      4100    /* 开机首测初值:≥此值判 USB 在线 */

/* 锂电电压→百分比（S1.6.2 标定后的典型曲线,单调递减）
 * 满电/90% 两档按供电状态区分（2026-11-13 定）：
 *   充电中（CV 被充电器顶在 4.2V）：≥4180 才算真满 100%,4100~4179 为 90%
 *   电池供电（充满拔线电压回落 ~4.1x）：≥4100 保持 100%,4000~4099 为 90%
 * 其余八档两态共用 */
static uint8_t vbat_to_percent(uint16_t mv, bool charging)
{
    if (charging) {
        if (mv >= 4180) return 100;
        if (mv >= 4100) return 90;
    }
    else {
        if (mv >= 4100) return 100;
        if (mv >= 4000) return 90;
    }
    static const struct { uint16_t mv; uint8_t pct; } table[] = {
        {4000, 80}, {3930, 70}, {3870, 60}, {3820, 50},
        {3780, 40}, {3700, 30}, {3600, 20}, {3450, 10},
    };
    for (int i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++) {
        if (mv >= table[i].mv) return table[i].pct;
    }
    return 5;   /* <3.45V:濒临关机（3.30V 由 power_task 强制关机保护） */
}

static void power_task(void *arg)
{
    bool first = true;
    bool usb_online = false;
    uint16_t last_mv = 0;
    int cand = 0;                 /* 沿候选:+1 上沿 / -1 下沿 / 0 无 */
    uint16_t cand_mv = 0;
    ll_battery_state_t st = {0};

    for (;;) {
        /* 开机后 1s 首测（低电电池开机即保护）,此后 5s 周期 */
        vTaskDelay(pdMS_TO_TICKS(first ? POWER_BOOT_DELAY_MS : POWER_POLL_PERIOD_MS));
        first = false;

        uint16_t mv = 0;
        if (bsp_power_read_vbat_mv(&mv) != ESP_OK) continue;

        if (last_mv == 0) {
            /* 首测：定 USB 在线初值（≥4100 大概率外电,含满电插着充电器） */
            last_mv = mv;
            usb_online = (mv >= USB_INIT_ONLINE_MV);
            ESP_LOGI(TAG, "first sample %umV, usb_online=%d", (unsigned)mv, usb_online);
        }
        else {
            const int delta = (int)mv - (int)last_mv;
            last_mv = mv;

            if (cand == 0) {
                /* 无候选:检测新沿 */
                if (delta >= USB_PLUGIN_TH_MV)        { cand = +1; cand_mv = mv; }
                else if (delta <= -USB_PLUGOUT_TH_MV) { cand = -1; cand_mv = mv; }
            }
            else {
                /* 有候选:一拍验证——反向回落 ≥15mV 判假沿（负载瞬变）,否则确认 */
                const int back = (int)mv - (int)cand_mv;
                if (cand == +1) {
                    if (back <= -USB_EDGE_CANCEL_MV) {
                        cand = 0;   /* 假沿丢弃 */
                    }
                    else if (!usb_online) {
                        usb_online = true;
                        ESP_LOGI(TAG, "USB plugged in (edge +%dmV)", delta);
                    }
                    /* 已在线的上沿无需处理 */
                }
                else {
                    if (back >= USB_EDGE_CANCEL_MV) {
                        cand = 0;
                    }
                    else if (usb_online) {
                        usb_online = false;
                        ESP_LOGI(TAG, "USB unplugged (edge %dmV)", delta);
                    }
                }
                if (cand != 0 && ((cand == +1) == usb_online)) cand = 0;   /* 候选已消化 */
            }
        }

        st.vbat_mv = mv;
        st.charging = usb_online;
        st.percent = vbat_to_percent(mv, usb_online);

        /* 串口日志（每周期一行,S1 验收口径） */
        ESP_LOGI(TAG, "vbat=%umV (~%u%%) %s",
                 (unsigned)st.vbat_mv, (unsigned)st.percent,
                 usb_online ? "USB" : "BATTERY");

        /* 每周期发事件（屏显 + S2 UI 订阅同源） */
        app_event_post(LL_EV_BATTERY_STATE, &st, sizeof(st), 0);

        /* 低电强制关机：≤3.30V 屏幕提示 3s 后断电。USB 在线豁免——充电可救回 */
        if (mv <= VBAT_FORCE_OFF_MV && !usb_online) {
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
