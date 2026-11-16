#ifndef VOICE_INPUT_H
#define VOICE_INPUT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "device_config.h"

/* 语音服务地址、deviceId、会话号与 X-Device-Token 全部来自 device_config.h */
#ifndef VOICE_UPLOAD_URL
#define VOICE_UPLOAD_URL DEVICE_VOICE_CHAT_URL
#endif

#ifndef VOICE_DOWNLOAD_URL
#define VOICE_DOWNLOAD_URL DEVICE_VOICE_TEST_WAV_URL
#endif

#ifndef VOICE_DEVICE_TOKEN
#define VOICE_DEVICE_TOKEN DEVICE_VOICE_TOKEN
#endif

#ifndef VOICE_INPUT_AUTO_LISTEN
#define VOICE_INPUT_AUTO_LISTEN DEVICE_VOICE_AUTO_LISTEN
#endif

#ifndef VOICE_UPLOAD_ENABLED
#define VOICE_UPLOAD_ENABLED DEVICE_VOICE_HTTP_UPLOAD
#endif

#ifndef VOICE_STREAM_ENABLED
#define VOICE_STREAM_ENABLED DEVICE_VOICE_STREAM
#endif

esp_err_t voice_input_init(void);
void voice_detect_task(void *arg);
void voice_upload_task(void *arg);
bool voice_input_is_session_active(void);
esp_err_t upload_voice(const uint8_t *pcm_data, size_t pcm_len);
esp_err_t voice_download_and_play(const char *url);

#ifdef __cplusplus
}
#endif

#endif