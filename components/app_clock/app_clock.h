#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_clock — 时间源管理：SNTP↔RTC 同步、当前时间查询（S1 空壳） */
esp_err_t app_clock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOCK_H */
