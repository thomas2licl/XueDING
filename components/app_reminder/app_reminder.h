#ifndef APP_REMINDER_H
#define APP_REMINDER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_reminder — 提醒事项：增删改查、周期性、到点通知（S1 空壳） */
esp_err_t app_reminder_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_REMINDER_H */
