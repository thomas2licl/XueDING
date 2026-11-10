#ifndef APP_POWER_H
#define APP_POWER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_power — 电源管理状态机：供电档位/电量/息屏/亮度/小夜灯（§11a/§11b）（S1 空壳） */
esp_err_t app_power_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_POWER_H */
