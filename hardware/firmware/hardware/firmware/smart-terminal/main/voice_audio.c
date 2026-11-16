#include "voice_audio.h"
#include "voice_input.h"
#include "esp_log.h"

static const char *TAG = "voice_audio";

static i2s_chan_handle_t s_tx_chan = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;
static bool s_bus_initialized = false;
static bool s_rx_enabled = false;
static bool s_tx_enabled = false;

#define VOICE_RX_ENABLED (VOICE_MIC_ENABLED && VOICE_INPUT_AUTO_LISTEN)

esp_err_t voice_audio_bus_init(void)
{
    if (s_bus_initialized) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;

    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, VOICE_RX_ENABLED ? &s_rx_chan : NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOICE_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = VOICE_I2S_BCLK_GPIO,
            .ws = VOICE_I2S_WS_GPIO,
            .dout = VOICE_I2S_SPK_DIN_GPIO,
            .din = VOICE_I2S_MIC_SD_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init tx std mode failed: %s", esp_err_to_name(err));
        return err;
    }

#if VOICE_RX_ENABLED
    err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init rx std mode failed: %s", esp_err_to_name(err));
        return err;
    }
#endif

    s_bus_initialized = true;
    ESP_LOGI(TAG, "I2S voice bus ready BCLK=%d WS=%d MIC_SD=%d SPK_DIN=%d",
             VOICE_I2S_BCLK_GPIO,
             VOICE_I2S_WS_GPIO,
             VOICE_I2S_MIC_SD_GPIO,
             VOICE_I2S_SPK_DIN_GPIO);
    return ESP_OK;
}

esp_err_t voice_audio_rx_enable(void)
{
    if (!VOICE_RX_ENABLED) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = voice_audio_bus_init();
    if (err != ESP_OK || s_rx_enabled) {
        return err;
    }
    if (s_rx_chan == NULL) {
        ESP_LOGE(TAG, "voice rx channel is not available");
        return ESP_ERR_INVALID_STATE;
    }

    err = i2s_channel_enable(s_rx_chan);
    if (err == ESP_OK) {
        s_rx_enabled = true;
        ESP_LOGI(TAG, "I2S voice rx enabled");
    }
    return err;
}

esp_err_t voice_audio_tx_enable(void)
{
    esp_err_t err = voice_audio_bus_init();
    if (err != ESP_OK || s_tx_enabled) {
        return err;
    }

    err = i2s_channel_enable(s_tx_chan);
    if (err == ESP_OK) {
        s_tx_enabled = true;
    }
    return err;
}

esp_err_t voice_audio_tx_disable(void)
{
    if (!s_tx_enabled || s_tx_chan == NULL) {
        return ESP_OK;
    }

    esp_err_t err = i2s_channel_disable(s_tx_chan);
    if (err == ESP_OK) {
        s_tx_enabled = false;
    }
    return err;
}

i2s_chan_handle_t voice_audio_rx_channel(void)
{
    return s_rx_chan;
}

i2s_chan_handle_t voice_audio_tx_channel(void)
{
    return s_tx_chan;
}