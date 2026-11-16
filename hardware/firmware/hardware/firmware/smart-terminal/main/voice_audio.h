#ifndef VOICE_AUDIO_H
#define VOICE_AUDIO_H

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_SAMPLE_RATE_HZ 16000
#define VOICE_BITS_PER_SAMPLE 16
#define VOICE_CHANNEL_COUNT 1
#define VOICE_MIC_ENABLED 1

#define VOICE_I2S_BCLK_GPIO GPIO_NUM_10
#define VOICE_I2S_WS_GPIO GPIO_NUM_11
#define VOICE_I2S_MIC_SD_GPIO GPIO_NUM_12
#define VOICE_I2S_SPK_DIN_GPIO GPIO_NUM_13

esp_err_t voice_audio_bus_init(void);
esp_err_t voice_audio_rx_enable(void);
esp_err_t voice_audio_tx_enable(void);
esp_err_t voice_audio_tx_disable(void);
i2s_chan_handle_t voice_audio_rx_channel(void);
i2s_chan_handle_t voice_audio_tx_channel(void);

#ifdef __cplusplus
}
#endif

#endif