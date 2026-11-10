#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_storage — NVS 封装：设置/闹钟/提醒/地点/天气缓存（S1 空壳） */
esp_err_t app_storage_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STORAGE_H */
