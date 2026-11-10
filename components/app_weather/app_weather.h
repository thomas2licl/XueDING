#ifndef APP_WEATHER_H
#define APP_WEATHER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_weather — 和风天气 API、多地点、通勤分析、今昨对比+穿衣建议、缓存降级（S1 空壳） */
esp_err_t app_weather_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WEATHER_H */
