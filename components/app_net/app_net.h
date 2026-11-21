#ifndef APP_NET_H
#define APP_NET_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_net — WiFi STA 管理 + 断线重连 + SNTP 对时（2026-11-21 临时版实体化）
 * SSID/密码硬编码于 user_config.h（WIFI_TMP_*）——连上即 SNTP 对时:
 * 同步成功发 LL_EV_TIME_SYNCED（载荷 bsp_rtc_datetime_t,由 app_clock 消费写 RTC）;
 * 连接状态变化发 LL_EV_NET_STATE（载荷 bool,UI 时钟卡 WiFi 图标用）。
 * 正式配网（扫描/设置页/NVS 存储）归 U8。 */
esp_err_t app_net_init(void);

/* WiFi 是否已连接（拿到 IP） */
bool app_net_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_NET_H */
