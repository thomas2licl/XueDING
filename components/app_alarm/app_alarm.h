#ifndef APP_ALARM_H
#define APP_ALARM_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_alarm — 闹钟引擎：单次/每日/自定义星期、贪睡（S1 空壳） */
esp_err_t app_alarm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ALARM_H */
