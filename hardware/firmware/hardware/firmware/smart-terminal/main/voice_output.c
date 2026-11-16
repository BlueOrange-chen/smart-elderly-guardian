#include "voice_output.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "voice_audio.h"
#include "rgb_led.h"
#include "driver/gpio.h"
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

static const char *TAG = "voice_output";

static SemaphoreHandle_t s_play_lock = NULL;
static bool s_output_initialized = false;
static bool s_stream_playing = false;
// SD (shutdown/mute) control for MAX98357A
#ifndef MAX98357A_SD_GPIO
#define MAX98357A_SD_GPIO GPIO_NUM_1
#endif
#ifndef MAX98357A_SD_ACTIVE_HIGH
// If SD is active-high to enable output, set 1. If active-low, set 0.
#define MAX98357A_SD_ACTIVE_HIGH 1
#endif
static bool s_sd_initialized = false;

#define VOICE_TEST_TONE_HZ 1000
#define VOICE_TEST_TONE_MS 1000
#define VOICE_OUTPUT_SILENCE_TAIL_MS 100
#define VOICE_OUTPUT_WRITE_TIMEOUT_MS 500

static bool is_wav_data(const uint8_t *buffer, size_t len)
{
    return len > 44 && memcmp(buffer, "RIFF", 4) == 0 && memcmp(buffer + 8, "WAVE", 4) == 0;
}

esp_err_t voice_output_init(void)
{
    if (s_output_initialized) {
        return ESP_OK;
    }

    esp_err_t err = voice_audio_tx_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "voice tx enable failed: %s", esp_err_to_name(err));
        return err;
    }

#if MAX98357A_SD_GPIO >= 0
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << MAX98357A_SD_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&io_conf);
    if (err == ESP_OK) {
        s_sd_initialized = true;
        int muted_level = MAX98357A_SD_ACTIVE_HIGH ? 0 : 1;
        gpio_set_level(MAX98357A_SD_GPIO, muted_level);
        ESP_LOGI(TAG, "MAX98357A SD GPIO %d initialized, default muted level=%d", MAX98357A_SD_GPIO, muted_level);
    } else {
        ESP_LOGW(TAG, "Failed to init SD GPIO %d: %s", MAX98357A_SD_GPIO, esp_err_to_name(err));
    }
#else
    ESP_LOGI(TAG, "MAX98357A SD GPIO disabled");
#endif

    s_play_lock = xSemaphoreCreateMutex();
    if (!s_play_lock) {
        return ESP_ERR_NO_MEM;
    }

    s_output_initialized = true;
    ESP_LOGI(TAG, "voice output ready");
    return ESP_OK;
}

esp_err_t voice_output_mute(bool mute)
{
    if (!s_sd_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    int level = mute ? (MAX98357A_SD_ACTIVE_HIGH ? 0 : 1) : (MAX98357A_SD_ACTIVE_HIGH ? 1 : 0);
    esp_err_t err = gpio_set_level(MAX98357A_SD_GPIO, level) == 0 ? ESP_OK : ESP_FAIL;
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "voice_output_mute set %d (gpio %d)", level, MAX98357A_SD_GPIO);
    } else {
        ESP_LOGE(TAG, "voice_output_mute failed to set gpio %d", MAX98357A_SD_GPIO);
    }
    return err;
}

esp_err_t voice_play(const uint8_t *buffer, size_t len)
{
    if (!s_output_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!buffer || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *pcm = buffer;
    size_t pcm_len = len;
    if (is_wav_data(buffer, len)) {
        pcm = buffer + 44;
        pcm_len = len - 44;
    }

    xSemaphoreTake(s_play_lock, portMAX_DELAY);
    esp_err_t tx_err = voice_audio_tx_enable();
    if (tx_err != ESP_OK) {
        xSemaphoreGive(s_play_lock);
        ESP_LOGE(TAG, "voice tx enable before play failed: %s", esp_err_to_name(tx_err));
        return tx_err;
    }

    rgb_led_playback_start();
    // ensure unmuted for playback
    if (s_sd_initialized) {
        esp_err_t merr = voice_output_mute(false);
        if (merr != ESP_OK) {
            ESP_LOGW(TAG, "failed to unmute before play: %s", esp_err_to_name(merr));
        }
    }

    esp_err_t write_err = ESP_OK;
    size_t offset = 0;
    while (offset < pcm_len) {
        size_t written = 0;
        size_t chunk = pcm_len - offset;
        if (chunk > 1024) {
            chunk = 1024;
        }

        esp_err_t err = i2s_channel_write(voice_audio_tx_channel(), pcm + offset, chunk, &written, pdMS_TO_TICKS(VOICE_OUTPUT_WRITE_TIMEOUT_MS));
        if (err != ESP_OK) {
            write_err = err;
            ESP_LOGE(TAG, "voice play write failed: %s", esp_err_to_name(err));
            break;
        }
        offset += written;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    static const int16_t silence[64] = {0};
    size_t silence_left = VOICE_SAMPLE_RATE_HZ * VOICE_OUTPUT_SILENCE_TAIL_MS / 1000 * sizeof(int16_t);
    while (silence_left > 0) {
        size_t silence_written = 0;
        size_t silence_chunk = silence_left > sizeof(silence) ? sizeof(silence) : silence_left;
        esp_err_t silence_err = i2s_channel_write(voice_audio_tx_channel(), silence, silence_chunk, &silence_written, pdMS_TO_TICKS(VOICE_OUTPUT_WRITE_TIMEOUT_MS));
        if (silence_err != ESP_OK) {
            if (write_err == ESP_OK) {
                write_err = silence_err;
            }
            ESP_LOGE(TAG, "voice silence write failed: %s", esp_err_to_name(silence_err));
            break;
        }
        silence_left -= silence_written;
    }

    // restore muted state after playback (best-effort)
    if (s_sd_initialized) {
        esp_err_t merr = voice_output_mute(true);
        if (merr != ESP_OK) {
            ESP_LOGW(TAG, "failed to re-mute after play: %s", esp_err_to_name(merr));
        }
    }

    xSemaphoreGive(s_play_lock);
    rgb_led_playback_end();

    if (write_err == ESP_OK) {
        ESP_LOGI(TAG, "voice play done bytes=%u", (unsigned int)pcm_len);
    }
    return write_err;
}

esp_err_t voice_output_test_tone(void)
{
    int sample_count = VOICE_SAMPLE_RATE_HZ * VOICE_TEST_TONE_MS / 1000;
    int16_t *samples = malloc(sample_count * sizeof(int16_t));
    if (!samples) {
        return ESP_ERR_NO_MEM;
    }

    for (int index = 0; index < sample_count; index++) {
        float phase = 2.0f * (float)M_PI * (float)VOICE_TEST_TONE_HZ * (float)index / (float)VOICE_SAMPLE_RATE_HZ;
        samples[index] = (int16_t)(sinf(phase) * 9000.0f);
    }

    ESP_LOGI(TAG, "play speaker test tone %dHz %dms", VOICE_TEST_TONE_HZ, VOICE_TEST_TONE_MS);
    esp_err_t err = voice_play((const uint8_t *)samples, sample_count * sizeof(int16_t));
    free(samples);
    return err;
}

esp_err_t voice_output_stream_start(void)
{
    if (!s_output_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_play_lock, portMAX_DELAY);
    esp_err_t err = voice_audio_tx_enable();
    if (err == ESP_OK) {
        s_stream_playing = true;
        rgb_led_playback_start();
        if (s_sd_initialized) {
            voice_output_mute(false);
        }
    }
    xSemaphoreGive(s_play_lock);
    return err;
}

esp_err_t voice_output_stream_write(const uint8_t *pcm, size_t len)
{
    if (!s_output_initialized || !s_stream_playing || !pcm || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_play_lock, portMAX_DELAY);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(voice_audio_tx_channel(), pcm, len, &written,
                                      pdMS_TO_TICKS(VOICE_OUTPUT_WRITE_TIMEOUT_MS));
    xSemaphoreGive(s_play_lock);
    return err == ESP_OK && written == len ? ESP_OK : (err == ESP_OK ? ESP_ERR_TIMEOUT : err);
}

esp_err_t voice_output_stream_stop(void)
{
    if (!s_output_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_play_lock, portMAX_DELAY);
    bool was_playing = s_stream_playing;
    s_stream_playing = false;
    if (s_sd_initialized) {
        voice_output_mute(true);
    }
    xSemaphoreGive(s_play_lock);
    if (was_playing) {
        rgb_led_playback_end();
    }
    return ESP_OK;
}

esp_err_t voice_output_stream_clear(void)
{
    if (!s_output_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_play_lock, portMAX_DELAY);
    s_stream_playing = false;
    esp_err_t err = voice_audio_tx_disable();
    if (err == ESP_OK) {
        err = voice_audio_tx_enable();
    }
    if (s_sd_initialized) {
        voice_output_mute(true);
    }
    xSemaphoreGive(s_play_lock);
    rgb_led_playback_end();
    return err;
}