#ifndef APP_AUDIO_H
#define APP_AUDIO_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_audio — 音频中枢：播放会话仲裁 + 白噪音库 + 音量（S1 空壳） */
esp_err_t app_audio_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_AUDIO_H */
