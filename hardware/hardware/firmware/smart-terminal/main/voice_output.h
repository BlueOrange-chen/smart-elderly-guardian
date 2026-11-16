#ifndef VOICE_OUTPUT_H
#define VOICE_OUTPUT_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_output_init(void);
esp_err_t voice_play(const uint8_t *buffer, size_t len);
esp_err_t voice_output_stream_start(void);
esp_err_t voice_output_stream_write(const uint8_t *pcm, size_t len);
esp_err_t voice_output_stream_stop(void);
esp_err_t voice_output_stream_clear(void);
esp_err_t voice_output_test_tone(void);
/**
 * Set output mute state using MAX98357A SD pin.
 * @param mute true to mute (shutdown), false to unmute
 */
esp_err_t voice_output_mute(bool mute);

#ifdef __cplusplus
}
#endif

#endif