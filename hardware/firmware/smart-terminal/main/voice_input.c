#include "voice_input.h"
#include "rgb_led.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "voice_audio.h"
#include "voice_output.h"
#include "voice_stream.h"

static const char *TAG = "voice_input";

#define VOICE_CHUNK_MS 20
#define VOICE_CHUNK_SAMPLES ((VOICE_SAMPLE_RATE_HZ * VOICE_CHUNK_MS) / 1000)
#define VOICE_CHUNK_BYTES (VOICE_CHUNK_SAMPLES * sizeof(int16_t))
#define VOICE_RING_SECONDS 3
#define VOICE_RING_BYTES (VOICE_SAMPLE_RATE_HZ * sizeof(int16_t) * VOICE_RING_SECONDS)
#define VOICE_PRE_ROLL_MS 500
#define VOICE_PRE_ROLL_BYTES (VOICE_SAMPLE_RATE_HZ * sizeof(int16_t) * VOICE_PRE_ROLL_MS / 1000)
#define VOICE_TRIGGER_RMS 220.0f
#define VOICE_TRIGGER_PEAK 450
#define VOICE_TRIGGER_NOISE_MULTIPLIER 3.0f
#define VOICE_TRIGGER_MIN_CREST 1.1f
#define VOICE_TRIGGER_MIN_ZCR 0.005f
#define VOICE_TRIGGER_MAX_ZCR 0.800f
#define VOICE_CONTINUE_RMS 140.0f
#define VOICE_CONTINUE_NOISE_MULTIPLIER 2.0f
#define VOICE_DEBUG_LOG_MS 1000
#define VOICE_TRIGGER_HOLD_MS 120
#define VOICE_SILENCE_HOLD_MS 1200
#define VOICE_RETRIGGER_COOLDOWN_MS 1000
#define VOICE_MAX_UTTERANCE_MS 30000
#define VOICE_EMPTY_READ_STOP_MS 800
#define VOICE_MAX_UPLOAD_BYTES (VOICE_SAMPLE_RATE_HZ * sizeof(int16_t) * 30)
#define VOICE_HTTP_TIMEOUT_MS 20000
#define VOICE_REPLY_MAX_BYTES (2 * 1024 * 1024)

typedef enum {
    VOICE_MSG_START,
    VOICE_MSG_DATA,
    VOICE_MSG_STOP,
} voice_msg_type_t;

typedef struct {
    voice_msg_type_t type;
    uint8_t *data;
    size_t len;
    int64_t created_us;
} voice_msg_t;

typedef struct {
    uint8_t *data;
    size_t size;
    size_t write_pos;
    size_t filled;
    SemaphoreHandle_t lock;
} voice_ring_t;

#if VOICE_UPLOAD_ENABLED
static voice_ring_t s_ring = {0};
#endif
static QueueHandle_t s_upload_queue = NULL;
static bool s_input_initialized = false;
static volatile bool s_voice_busy = false;
static volatile bool s_voice_session_active = false;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t capacity;
    bool truncated;
    int64_t first_data_us;
} voice_http_response_t;

typedef struct {
    float rms;
    int32_t average_abs;
    int32_t peak_abs;
    float zero_crossing_rate;
} voice_features_t;

static esp_err_t voice_http_event_handler(esp_http_client_event_t *evt)
{
    voice_http_response_t *response = (voice_http_response_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !response || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }

    if (response->first_data_us == 0) {
        response->first_data_us = esp_timer_get_time();
    }

    if (response->len + evt->data_len > VOICE_REPLY_MAX_BYTES) {
        response->truncated = true;
        return ESP_OK;
    }

    size_t needed = response->len + evt->data_len;
    if (needed > response->capacity) {
        size_t new_capacity = response->capacity == 0 ? 4096 : response->capacity * 2;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }

        uint8_t *new_data = realloc(response->data, new_capacity);
        if (!new_data) {
            response->truncated = true;
            return ESP_ERR_NO_MEM;
        }
        response->data = new_data;
        response->capacity = new_capacity;
    }

    memcpy(response->data + response->len, evt->data, evt->data_len);
    response->len += evt->data_len;
    return ESP_OK;
}

static bool is_wav_response(const uint8_t *data, size_t len)
{
    return len > 44 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0;
}

bool voice_input_is_session_active(void)
{
    return s_voice_session_active;
}

#if VOICE_UPLOAD_ENABLED
static void ring_write(const uint8_t *data, size_t len)
{
    if (!s_ring.data || !data || len == 0) {
        return;
    }

    xSemaphoreTake(s_ring.lock, portMAX_DELAY);
    for (size_t index = 0; index < len; index++) {
        s_ring.data[s_ring.write_pos] = data[index];
        s_ring.write_pos = (s_ring.write_pos + 1) % s_ring.size;
        if (s_ring.filled < s_ring.size) {
            s_ring.filled++;
        }
    }
    xSemaphoreGive(s_ring.lock);
}

static uint8_t *ring_snapshot(size_t requested_len, size_t *out_len)
{
    if (!out_len || !s_ring.data) {
        return NULL;
    }

    xSemaphoreTake(s_ring.lock, portMAX_DELAY);
    size_t len = requested_len < s_ring.filled ? requested_len : s_ring.filled;
    uint8_t *copy = malloc(len);
    if (!copy) {
        xSemaphoreGive(s_ring.lock);
        *out_len = 0;
        return NULL;
    }

    size_t start = (s_ring.write_pos + s_ring.size - len) % s_ring.size;
    for (size_t index = 0; index < len; index++) {
        copy[index] = s_ring.data[(start + index) % s_ring.size];
    }
    xSemaphoreGive(s_ring.lock);

    *out_len = len;
    return copy;
}
#endif

static voice_features_t calculate_voice_features(const int16_t *samples, size_t sample_count)
{
    voice_features_t features = {0};
    int64_t sum = 0;

    for (size_t index = 0; index < sample_count; index++) {
        sum += samples[index];
    }

    if (sample_count == 0) {
        return features;
    }

    int32_t mean = (int32_t)(sum / (int64_t)sample_count);
    int64_t square_sum = 0;
    int64_t abs_sum = 0;
    int zero_crossings = 0;
    int32_t previous = (int32_t)samples[0] - mean;
    for (size_t index = 0; index < sample_count; index++) {
        int32_t centered = (int32_t)samples[index] - mean;
        square_sum += (int64_t)centered * centered;
        int32_t abs_value = abs(centered);
        abs_sum += abs_value;
        if (abs_value > features.peak_abs) {
            features.peak_abs = abs_value;
        }
        if (index > 0 && ((previous < 0 && centered >= 0) || (previous >= 0 && centered < 0))) {
            zero_crossings++;
        }
        previous = centered;
    }

    features.average_abs = (int32_t)(abs_sum / sample_count);
    features.rms = sqrtf((float)square_sum / (float)sample_count);
    features.zero_crossing_rate = (float)zero_crossings / (float)sample_count;
    return features;
}

static bool is_voice_start_candidate(const voice_features_t *features, float noise_rms)
{
    float crest = features->rms > 1.0f ? (float)features->peak_abs / features->rms : 0.0f;
    bool loud_enough = features->rms >= VOICE_TRIGGER_RMS
        && features->rms >= noise_rms * VOICE_TRIGGER_NOISE_MULTIPLIER
        && features->peak_abs >= VOICE_TRIGGER_PEAK;
    bool not_extreme_noise = features->zero_crossing_rate >= VOICE_TRIGGER_MIN_ZCR
        && features->zero_crossing_rate <= VOICE_TRIGGER_MAX_ZCR;
    return loud_enough && not_extreme_noise && crest >= VOICE_TRIGGER_MIN_CREST;
}

static bool is_voice_continue_candidate(const voice_features_t *features, float noise_rms)
{
    return features->rms >= VOICE_CONTINUE_RMS
        && features->rms >= noise_rms * VOICE_CONTINUE_NOISE_MULTIPLIER;
}

static void queue_voice_msg(voice_msg_type_t type, uint8_t *data, size_t len)
{
#if !VOICE_UPLOAD_ENABLED
    (void)type;
    (void)len;
    free(data);
    return;
#endif

    voice_msg_t msg = {
        .type = type,
        .data = data,
        .len = len,
        .created_us = esp_timer_get_time(),
    };

    if (xQueueSend(s_upload_queue, &msg, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "voice upload queue full, drop msg type=%d len=%u", type, (unsigned int)len);
        free(data);
    }
}

static void append_upload_data(uint8_t **buffer, size_t *len, size_t *capacity, const uint8_t *data, size_t data_len)
{
    if (!buffer || !len || !capacity || !data || data_len == 0) {
        return;
    }

    if (*len + data_len > VOICE_MAX_UPLOAD_BYTES) {
        return;
    }

    if (*len + data_len > *capacity) {
        size_t new_capacity = *capacity == 0 ? data_len : *capacity * 2;
        while (new_capacity < *len + data_len) {
            new_capacity *= 2;
        }

        uint8_t *new_buffer = realloc(*buffer, new_capacity);
        if (!new_buffer) {
            ESP_LOGE(TAG, "realloc upload buffer failed");
            return;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *len, data, data_len);
    *len += data_len;
}

static void write_wav_header(uint8_t *header, size_t pcm_len)
{
    uint32_t sample_rate = VOICE_SAMPLE_RATE_HZ;
    uint16_t bits_per_sample = VOICE_BITS_PER_SAMPLE;
    uint16_t channels = VOICE_CHANNEL_COUNT;
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;
    uint32_t riff_size = (uint32_t)(pcm_len + 36);
    uint32_t data_size = (uint32_t)pcm_len;

    memcpy(header, "RIFF", 4);
    memcpy(header + 4, &riff_size, 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    memcpy(header + 16, &fmt_size, 4);
    memcpy(header + 20, &audio_format, 2);
    memcpy(header + 22, &channels, 2);
    memcpy(header + 24, &sample_rate, 4);
    memcpy(header + 28, &byte_rate, 4);
    memcpy(header + 32, &block_align, 2);
    memcpy(header + 34, &bits_per_sample, 2);
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &data_size, 4);
}

esp_err_t upload_voice(const uint8_t *pcm_data, size_t pcm_len)
{
    if (!pcm_data || pcm_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t upload_started_us = esp_timer_get_time();

    if (VOICE_DEVICE_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "VOICE_DEVICE_TOKEN is empty, set it before voice chat");
        return ESP_ERR_INVALID_STATE;
    }

    size_t wav_len = pcm_len + 44;
    uint8_t *wav = malloc(wav_len);
    if (!wav) {
        return ESP_ERR_NO_MEM;
    }

    write_wav_header(wav, pcm_len);
    memcpy(wav + 44, pcm_data, pcm_len);

    voice_http_response_t response = {0};

    esp_http_client_config_t config = {
        .url = VOICE_UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = voice_http_event_handler,
        .user_data = &response,
        .buffer_size_tx = 4096,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(wav);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "X-Device-Token", VOICE_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_post_field(client, (const char *)wav, wav_len);

    int64_t request_started_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int64_t response_received_us = esp_timer_get_time();
    int status = esp_http_client_get_status_code(client);
    int64_t first_data_us = response.first_data_us > 0 ? response.first_data_us : response_received_us;
    ESP_LOGI(TAG, "voice latency pcm_prepare=%lld ms first_byte=%lld ms complete_response=%lld ms pcm=%u wav=%u",
             (long long)((request_started_us - upload_started_us) / 1000),
             (long long)((first_data_us - request_started_us) / 1000),
             (long long)((response_received_us - request_started_us) / 1000),
             (unsigned int)pcm_len,
             (unsigned int)wav_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "voice upload failed: %s status=%d", esp_err_to_name(err), status);
    } else if (status != 200 && status != 204) {
        ESP_LOGW(TAG, "voice chat error status=%d body=%.*s", status, (int)response.len, response.data ? (char *)response.data : "");
        err = ESP_FAIL;
    } else if (status == 204) {
        ESP_LOGI(TAG, "voice upload ok wav=%u status=204 no reply", (unsigned int)wav_len);
    } else if (response.truncated || !is_wav_response(response.data, response.len)) {
        ESP_LOGE(TAG, "voice reply invalid wav len=%u truncated=%d", (unsigned int)response.len, response.truncated);
        err = ESP_ERR_INVALID_RESPONSE;
    } else {
        ESP_LOGI(TAG, "voice chat ok upload_wav=%u reply_wav=%u", (unsigned int)wav_len, (unsigned int)response.len);
        s_voice_busy = true;
        int64_t playback_started_us = esp_timer_get_time();
        err = voice_play(response.data, response.len);
        ESP_LOGI(TAG, "voice playback duration=%lld ms request_to_play=%lld ms",
                 (long long)((esp_timer_get_time() - playback_started_us) / 1000),
                 (long long)((playback_started_us - request_started_us) / 1000));
        s_voice_busy = false;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "voice reply play failed: %s", esp_err_to_name(err));
        }
    }

    esp_http_client_cleanup(client);
    free(response.data);
    free(wav);
    return err;
}

esp_err_t voice_download_and_play(const char *url)
{
    const char *request_url = (url && url[0] != '\0') ? url : VOICE_DOWNLOAD_URL;
    if (VOICE_DEVICE_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "VOICE_DEVICE_TOKEN is empty, set it before voice download");
        return ESP_ERR_INVALID_STATE;
    }

    voice_http_response_t response = {0};
    esp_http_client_config_t config = {
        .url = request_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = VOICE_HTTP_TIMEOUT_MS,
        .event_handler = voice_http_event_handler,
        .user_data = &response,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Accept", "audio/wav");
    esp_http_client_set_header(client, "X-Device-Token", VOICE_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "voice download failed: %s status=%d", esp_err_to_name(err), status);
    } else if (status != 200) {
        ESP_LOGW(TAG, "voice download error status=%d body=%.*s", status, (int)response.len, response.data ? (char *)response.data : "");
        err = ESP_FAIL;
    } else if (response.truncated || !is_wav_response(response.data, response.len)) {
        ESP_LOGE(TAG, "voice download invalid wav len=%u truncated=%d", (unsigned int)response.len, response.truncated);
        err = ESP_ERR_INVALID_RESPONSE;
    } else {
        ESP_LOGI(TAG, "voice download ok wav=%u", (unsigned int)response.len);
        err = voice_play(response.data, response.len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "voice download play failed: %s", esp_err_to_name(err));
        }
    }

    esp_http_client_cleanup(client);
    free(response.data);
    return err;
}

esp_err_t voice_input_init(void)
{
    if (s_input_initialized) {
        return ESP_OK;
    }

    esp_err_t err = voice_audio_rx_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "voice rx enable failed: %s", esp_err_to_name(err));
        return err;
    }

#if VOICE_UPLOAD_ENABLED
    s_ring.data = malloc(VOICE_RING_BYTES);
    s_ring.size = VOICE_RING_BYTES;
    s_ring.lock = xSemaphoreCreateMutex();
    s_upload_queue = xQueueCreate(16, sizeof(voice_msg_t));
    if (!s_ring.data || !s_ring.lock || !s_upload_queue) {
        ESP_LOGE(TAG, "voice input allocation failed");
        return ESP_ERR_NO_MEM;
    }
#endif

    s_input_initialized = true;
#if VOICE_UPLOAD_ENABLED
    ESP_LOGI(TAG, "voice input ready, ring=%u bytes, threshold=%.1f", VOICE_RING_BYTES, VOICE_TRIGGER_RMS);
#else
    ESP_LOGI(TAG, "voice input ready, local vad only, threshold=%.1f", VOICE_TRIGGER_RMS);
#endif
    return ESP_OK;
}

void voice_detect_task(void *arg)
{
    (void)arg;
    int16_t samples[VOICE_CHUNK_SAMPLES];
    bool speaking = false;
    int voiced_ms = 0;
    int silence_ms = 0;
    int speaking_ms = 0;
    int empty_read_ms = 0;
    int cooldown_ms = 0;
    int debug_log_ms = 0;
    bool pre_activity = false;
    float noise_rms = 80.0f;

    while (1) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(voice_audio_rx_channel(), samples, sizeof(samples), &bytes_read, pdMS_TO_TICKS(100));
        if (s_voice_busy || (VOICE_STREAM_ENABLED && voice_stream_is_playing())) {
            if (speaking) {
                rgb_led_activity_end();
            }
            speaking = false;
            voiced_ms = 0;
            silence_ms = 0;
            speaking_ms = 0;
            empty_read_ms = 0;
            cooldown_ms = VOICE_RETRIGGER_COOLDOWN_MS;
            if (pre_activity) {
                rgb_led_activity_end();
                pre_activity = false;
            }
            vTaskDelay(pdMS_TO_TICKS(VOICE_CHUNK_MS));
            continue;
        }
        if (bytes_read == 0) {
            empty_read_ms += VOICE_CHUNK_MS;
            if (empty_read_ms >= VOICE_EMPTY_READ_STOP_MS && speaking) {
                ESP_LOGI(TAG, "voice stop: empty i2s reads for %d ms", empty_read_ms);
                queue_voice_msg(VOICE_MSG_STOP, NULL, 0);
                rgb_led_thinking_start();
                rgb_led_activity_end();
                speaking = false;
                voiced_ms = 0;
                silence_ms = 0;
                speaking_ms = 0;
                cooldown_ms = VOICE_RETRIGGER_COOLDOWN_MS;
                empty_read_ms = 0;
                pre_activity = false;
            } else if (empty_read_ms == VOICE_CHUNK_MS || empty_read_ms % 1000 == 0) {
                ESP_LOGW(TAG, "i2s read failed: %s bytes=%u", esp_err_to_name(err), (unsigned int)bytes_read);
            }
            vTaskDelay(pdMS_TO_TICKS(VOICE_CHUNK_MS));
            continue;
        } else if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "i2s read failed: %s bytes=%u", esp_err_to_name(err), (unsigned int)bytes_read);
        }
        empty_read_ms = 0;

        if (cooldown_ms > 0) {
            cooldown_ms -= VOICE_CHUNK_MS;
            voiced_ms = 0;
            if (pre_activity) {
                rgb_led_activity_end();
                pre_activity = false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

    #if VOICE_UPLOAD_ENABLED
        ring_write((const uint8_t *)samples, bytes_read);
    #endif

        voice_features_t features = calculate_voice_features(samples, bytes_read / sizeof(int16_t));
        debug_log_ms += VOICE_CHUNK_MS;
        if (debug_log_ms >= VOICE_DEBUG_LOG_MS) {
            debug_log_ms = 0;
            ESP_LOGI(TAG, "mic level rms=%.1f noise=%.1f peak=%ld avg=%ld zcr=%.3f bytes=%u speaking=%d",
                     features.rms,
                     noise_rms,
                     features.peak_abs,
                     features.average_abs,
                     features.zero_crossing_rate,
                     (unsigned int)bytes_read,
                     speaking);
        }

        bool start_candidate = is_voice_start_candidate(&features, noise_rms);
        bool has_voice = speaking
            ? is_voice_continue_candidate(&features, noise_rms)
            : start_candidate;

        if (!speaking && !start_candidate) {
            noise_rms = noise_rms * 0.98f + features.rms * 0.02f;
            if (noise_rms < 40.0f) {
                noise_rms = 40.0f;
            } else if (noise_rms > 5000.0f) {
                noise_rms = 5000.0f;
            }
        }

        if (!speaking) {
            voiced_ms = has_voice ? voiced_ms + VOICE_CHUNK_MS : 0;
            if (has_voice && !pre_activity) {
                rgb_led_thinking_end();
                rgb_led_activity_start();
                pre_activity = true;
            } else if (!has_voice && pre_activity) {
                rgb_led_activity_end();
                pre_activity = false;
            }
            if (voiced_ms >= VOICE_TRIGGER_HOLD_MS) {
                size_t pre_len = 0;
                uint8_t *pre_roll = NULL;
#if VOICE_UPLOAD_ENABLED
                pre_roll = ring_snapshot(VOICE_PRE_ROLL_BYTES, &pre_len);
#endif
                speaking = true;
                s_voice_session_active = true;
                silence_ms = 0;
                speaking_ms = 0;
                rgb_led_thinking_end();
                if (!pre_activity) {
                    rgb_led_activity_start();
                }
                pre_activity = false;
                ESP_LOGI(TAG, "voice start rms=%.1f noise=%.1f peak=%ld zcr=%.3f pre=%u",
                         features.rms,
                         noise_rms,
                         features.peak_abs,
                         features.zero_crossing_rate,
                         (unsigned int)pre_len);
                queue_voice_msg(VOICE_MSG_START, pre_roll, pre_len);
            }
        } else {
            speaking_ms += VOICE_CHUNK_MS;
#if VOICE_UPLOAD_ENABLED
            uint8_t *chunk = malloc(bytes_read);
            if (chunk) {
                memcpy(chunk, samples, bytes_read);
                queue_voice_msg(VOICE_MSG_DATA, chunk, bytes_read);
            }
#endif

            silence_ms = has_voice ? 0 : silence_ms + VOICE_CHUNK_MS;
            if (silence_ms >= VOICE_SILENCE_HOLD_MS || speaking_ms >= VOICE_MAX_UTTERANCE_MS) {
                ESP_LOGI(TAG, "voice stop rms=%.1f noise=%.1f peak=%ld zcr=%.3f silence=%dms duration=%dms",
                         features.rms,
                         noise_rms,
                         features.peak_abs,
                         features.zero_crossing_rate,
                         silence_ms,
                         speaking_ms);
                queue_voice_msg(VOICE_MSG_STOP, NULL, 0);
                speaking = false;
                voiced_ms = 0;
                silence_ms = 0;
                speaking_ms = 0;
                cooldown_ms = VOICE_RETRIGGER_COOLDOWN_MS;
                rgb_led_thinking_start();
                rgb_led_activity_end();
                pre_activity = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void voice_upload_task(void *arg)
{
    (void)arg;
    uint8_t *upload_buffer = NULL;
    size_t upload_len = 0;
    size_t upload_capacity = 0;
    bool collecting = false;
    bool truncated = false;
    bool streaming = false;

    while (1) {
        voice_msg_t msg = {0};
        if (xQueueReceive(s_upload_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (msg.type == VOICE_MSG_START) {
            free(upload_buffer);
            upload_buffer = NULL;
            upload_len = 0;
            upload_capacity = 0;
            collecting = true;
            streaming = false;
            truncated = false;
            append_upload_data(&upload_buffer, &upload_len, &upload_capacity, msg.data, msg.len);
            ESP_LOGI(TAG, "voice upload collect start pre=%u", (unsigned int)msg.len);
#if VOICE_STREAM_ENABLED
            if (voice_stream_is_ready() && voice_stream_send_listen_start() == ESP_OK) {
                int wait_ms = 0;
                while (!voice_stream_is_listening() && wait_ms < 1000) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                    wait_ms += 20;
                }
                if (voice_stream_is_listening()) {
                    streaming = true;
                    size_t offset = 0;
                    while (offset + VOICE_CHUNK_BYTES <= msg.len) {
                        if (voice_stream_send_pcm_frame((const int16_t *)(msg.data + offset), VOICE_CHUNK_SAMPLES) != ESP_OK) {
                            ESP_LOGW(TAG, "stream pre-roll send failed, use HTTP fallback");
                            streaming = false;
                            voice_stream_send_listen_stop();
                            break;
                        }
                        offset += VOICE_CHUNK_BYTES;
                    }
                    ESP_LOGI(TAG, "voice stream listen active pre=%u", (unsigned int)msg.len);
                } else {
                    ESP_LOGW(TAG, "stream listen confirmation timed out, use HTTP fallback");
                }
            }
#endif
        } else if (msg.type == VOICE_MSG_DATA && collecting) {
            if (upload_len + msg.len > VOICE_MAX_UPLOAD_BYTES) {
                if (!truncated) {
                    ESP_LOGW(TAG, "voice upload buffer limit reached, truncate current utterance");
                    truncated = true;
                }
            } else {
                append_upload_data(&upload_buffer, &upload_len, &upload_capacity, msg.data, msg.len);
            }
#if VOICE_STREAM_ENABLED
            if (streaming && (msg.len != VOICE_CHUNK_BYTES ||
                              voice_stream_send_pcm_frame((const int16_t *)msg.data, VOICE_CHUNK_SAMPLES) != ESP_OK)) {
                ESP_LOGW(TAG, "stream frame send failed, use HTTP fallback");
                streaming = false;
                voice_stream_send_listen_stop();
            }
#endif
        } else if (msg.type == VOICE_MSG_STOP && collecting) {
            collecting = false;
            int64_t upload_started_us = esp_timer_get_time();
            ESP_LOGI(TAG, "voice upload collect stop pcm=%u", (unsigned int)upload_len);
            ESP_LOGI(TAG, "voice latency vad_to_upload_task=%lld ms",
                     (long long)((upload_started_us - msg.created_us) / 1000));
            if (streaming) {
                voice_stream_send_listen_stop();
            } else if (upload_len > 0) {
                upload_voice(upload_buffer, upload_len);
            }
            s_voice_session_active = false;
            free(upload_buffer);
            upload_buffer = NULL;
            upload_len = 0;
            upload_capacity = 0;
        }

        free(msg.data);
    }
}