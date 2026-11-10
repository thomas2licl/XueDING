#ifndef APP_TIMER_H
#define APP_TIMER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_timer — 沙漏倒计时：≤30min、点击/竖放启动、漏沙动画联动、滴滴（S1 空壳） */
esp_err_t app_timer_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TIMER_H */
