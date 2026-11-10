#ifndef APP_NET_H
#define APP_NET_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_net — WiFi STA 管理 + 断线重连 + SNTP 对时（S1 空壳） */
esp_err_t app_net_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_NET_H */
