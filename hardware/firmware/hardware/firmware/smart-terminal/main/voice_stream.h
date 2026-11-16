#ifndef VOICE_STREAM_H
#define VOICE_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_stream_init(void);
esp_err_t voice_stream_start(void);
esp_err_t voice_stream_stop(void);
esp_err_t voice_stream_clear_playback(void);
bool voice_stream_is_connected(void);
bool voice_stream_is_ready(void);
bool voice_stream_is_listening(void);
bool voice_stream_is_playing(void);
esp_err_t voice_stream_send_listen_start(void);
esp_err_t voice_stream_send_listen_stop(void);
esp_err_t voice_stream_send_pcm_frame(const int16_t *pcm, size_t sample_count);
esp_err_t voice_stream_encode_pcm_frame(const int16_t *pcm, size_t sample_count,
										uint8_t *opus, size_t opus_capacity,
										size_t *opus_size);

#ifdef __cplusplus
}
#endif

#endif