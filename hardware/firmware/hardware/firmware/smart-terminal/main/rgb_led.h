#ifndef RGB_LED_H
#define RGB_LED_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rgb_led_init(void);
void rgb_led_set_starting(void);
void rgb_led_set_normal(void);
void rgb_led_activity_start(void);
void rgb_led_activity_end(void);
void rgb_led_thinking_start(void);
void rgb_led_thinking_end(void);
void rgb_led_playback_start(void);
void rgb_led_playback_end(void);

#ifdef __cplusplus
}
#endif

#endif