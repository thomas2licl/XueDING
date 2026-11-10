#ifndef APP_VOICE_H
#define APP_VOICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_voice — ESP-SR（AFE→WakeNet→MultiNet）+ esp-tts 封装（S1 空壳） */
esp_err_t app_voice_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_VOICE_H */
