#include "voice_stream.h"
#include "voice_input.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "encoder/impl/esp_opus_enc.h"
#include "decoder/impl/esp_opus_dec.h"
#include "voice_output.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "voice_stream";

/* 地址与 deviceId 来自 device_config.h；握手头里的令牌由 VOICE_DEVICE_TOKEN 提供 */
#define VOICE_STREAM_URL DEVICE_VOICE_WS_URL
#define VOICE_STREAM_HELLO \
    "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\",\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration_ms\":20}}"
#define VOICE_STREAM_RX_QUEUE_LENGTH 32
#define VOICE_STREAM_MAX_PACKET_BYTES 512
#define VOICE_STREAM_PCM_BYTES 640

static esp_websocket_client_handle_t s_client = NULL;
static bool s_initialized = false;
static bool s_connected = false;
static bool s_ready = false;
static volatile bool s_listening = false;
static void *s_encoder = NULL;
static int s_encoder_frame_bytes = 0;
static int s_encoder_output_bytes = 0;
static QueueHandle_t s_rx_queue = NULL;
static TaskHandle_t s_decode_task = NULL;
static void *s_decoder = NULL;
static volatile bool s_playback_cleared = true;
static volatile bool s_playback_active = false;

typedef struct {
    size_t len;
    uint8_t data[VOICE_STREAM_MAX_PACKET_BYTES];
} voice_stream_packet_t;

static void voice_stream_decode_task(void *arg);

static bool voice_stream_text_contains(const esp_websocket_event_data_t *event, const char *text)
{
    size_t text_len = strlen(text);
    if (!event || !event->data_ptr || event->data_len < (int)text_len) {
        return false;
    }

    for (int offset = 0; offset <= event->data_len - (int)text_len; offset++) {
        if (memcmp(event->data_ptr + offset, text, text_len) == 0) {
            return true;
        }
    }
    return false;
}

static void voice_stream_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    esp_websocket_event_data_t *event = (esp_websocket_event_data_t *)event_data;
    if (!event) {
        return;
    }

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        s_ready = false;
        s_listening = false;
        ESP_LOGI(TAG, "websocket connected, sending hello");
        esp_websocket_client_send_text(s_client, VOICE_STREAM_HELLO, strlen(VOICE_STREAM_HELLO), portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        s_ready = false;
        s_listening = false;
        s_playback_cleared = true;
        s_playback_active = false;
        ESP_LOGW(TAG, "websocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (event->op_code == 0x1) {
            if (voice_stream_text_contains(event, "\"type\":\"hello\"")) {
                s_ready = true;
                ESP_LOGI(TAG, "websocket hello accepted");
            } else if (voice_stream_text_contains(event, "listen.started")) {
                s_listening = true;
                ESP_LOGI(TAG, "server listening started");
            } else if (voice_stream_text_contains(event, "playback.clear") ||
                       voice_stream_text_contains(event, "response.done") ||
                       voice_stream_text_contains(event, "tts.stop")) {
                voice_stream_clear_playback();
            } else {
                ESP_LOGI(TAG, "text frame received len=%d", event->data_len);
            }
        } else if (event->op_code == 0x2) {
            if (event->data_ptr && event->data_len > 0 && event->data_len <= VOICE_STREAM_MAX_PACKET_BYTES) {
                voice_stream_packet_t packet = {.len = (size_t)event->data_len};
                memcpy(packet.data, event->data_ptr, packet.len);
                if (!s_rx_queue || xQueueSend(s_rx_queue, &packet, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Opus receive queue full, drop len=%d", event->data_len);
                }
            } else {
                ESP_LOGW(TAG, "invalid binary frame len=%d", event->data_len);
            }
        } else {
            ESP_LOGW(TAG, "unsupported websocket opcode=0x%x len=%d", event->op_code, event->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        s_connected = false;
        s_ready = false;
        ESP_LOGE(TAG, "websocket error");
        break;
    default:
        break;
    }
}

esp_err_t voice_stream_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_websocket_client_config_t config = {
        .uri = VOICE_STREAM_URL,
        .headers = "X-Device-Token: " VOICE_DEVICE_TOKEN "\r\n",
        .disable_auto_reconnect = true,
    };
    s_client = esp_websocket_client_init(&config);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }

    s_rx_queue = xQueueCreate(VOICE_STREAM_RX_QUEUE_LENGTH, sizeof(voice_stream_packet_t));
    if (!s_rx_queue) {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_opus_dec_cfg_t decoder_config = ESP_OPUS_DEC_CONFIG_DEFAULT();
    decoder_config.sample_rate = 16000;
    decoder_config.channel = 1;
    decoder_config.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS;
    decoder_config.self_delimited = false;
    esp_audio_err_t decoder_err = esp_opus_dec_open(&decoder_config, sizeof(decoder_config), &s_decoder);
    if (decoder_err != ESP_AUDIO_ERR_OK || !s_decoder) {
        ESP_LOGE(TAG, "failed to initialize Opus decoder: %d", decoder_err);
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_opus_enc_config_t opus_config = ESP_OPUS_ENC_CONFIG_DEFAULT();
    opus_config.sample_rate = 16000;
    opus_config.channel = 1;
    opus_config.bits_per_sample = 16;
    opus_config.bitrate = 20000;
    opus_config.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
    opus_config.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    opus_config.complexity = 0;
    opus_config.enable_fec = false;
    opus_config.enable_dtx = false;
    opus_config.enable_vbr = true;

    esp_audio_err_t opus_err = esp_opus_enc_open(&opus_config, sizeof(opus_config), &s_encoder);
    if (opus_err != ESP_AUDIO_ERR_OK || s_encoder == NULL) {
        ESP_LOGE(TAG, "failed to initialize Opus encoder: %d", opus_err);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return ESP_ERR_NOT_SUPPORTED;
    }
    opus_err = esp_opus_enc_get_frame_size(s_encoder, &s_encoder_frame_bytes, &s_encoder_output_bytes);
    if (opus_err != ESP_AUDIO_ERR_OK || s_encoder_frame_bytes != 320 || s_encoder_output_bytes <= 0) {
        ESP_LOGE(TAG, "unexpected Opus frame size: input=%d output=%d error=%d",
                 s_encoder_frame_bytes, s_encoder_output_bytes, opus_err);
        esp_opus_enc_close(s_encoder);
        s_encoder = NULL;
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, voice_stream_event_handler, NULL);
    if (err != ESP_OK) {
        esp_opus_dec_close(s_decoder);
        s_decoder = NULL;
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    if (xTaskCreate(voice_stream_decode_task, "voice_opus_decode", 6144, NULL, 6, &s_decode_task) != pdPASS) {
        esp_opus_dec_close(s_decoder);
        s_decoder = NULL;
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "websocket stream ready (disabled=%d)", VOICE_STREAM_ENABLED == 0);
    return ESP_OK;
}

esp_err_t voice_stream_start(void)
{
    if (!VOICE_STREAM_ENABLED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!s_initialized || !s_client) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_websocket_client_start(s_client);
}

esp_err_t voice_stream_stop(void)
{
    if (!s_client || !s_initialized) {
        return ESP_OK;
    }
    s_connected = false;
    voice_stream_clear_playback();
    return esp_websocket_client_stop(s_client);
}

esp_err_t voice_stream_clear_playback(void)
{
    if (!s_rx_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    voice_stream_packet_t packet;
    while (xQueueReceive(s_rx_queue, &packet, 0) == pdTRUE) {
    }
    s_playback_cleared = true;
    s_playback_active = false;
    return voice_output_stream_clear();
}

bool voice_stream_is_connected(void)
{
    return s_connected;
}

bool voice_stream_is_ready(void)
{
    return s_ready;
}

bool voice_stream_is_listening(void)
{
    return s_listening;
}

bool voice_stream_is_playing(void)
{
    return s_playback_active;
}

static esp_err_t voice_stream_send_control(const char *message)
{
    if (!s_ready || !s_client || !message) {
        return ESP_ERR_INVALID_STATE;
    }

    int sent = esp_websocket_client_send_text(s_client, message, strlen(message), pdMS_TO_TICKS(1000));
    return sent == (int)strlen(message) ? ESP_OK : ESP_FAIL;
}

esp_err_t voice_stream_send_listen_start(void)
{
    s_listening = false;
    return voice_stream_send_control("{\"type\":\"listen\",\"state\":\"start\"}");
}

esp_err_t voice_stream_send_listen_stop(void)
{
    s_listening = false;
    return voice_stream_send_control("{\"type\":\"listen\",\"state\":\"stop\"}");
}

esp_err_t voice_stream_send_pcm_frame(const int16_t *pcm, size_t sample_count)
{
    if (!s_ready || !s_client || !pcm || sample_count != 320 || s_encoder_output_bytes <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t opus[400];
    size_t opus_size = 0;
    esp_err_t err = voice_stream_encode_pcm_frame(pcm, sample_count, opus, sizeof(opus), &opus_size);
    if (err != ESP_OK || opus_size == 0) {
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }

    int sent = esp_websocket_client_send_bin(s_client, (const char *)opus, opus_size, pdMS_TO_TICKS(1000));
    return sent == (int)opus_size ? ESP_OK : ESP_FAIL;
}

esp_err_t voice_stream_encode_pcm_frame(const int16_t *pcm, size_t sample_count,
                                        uint8_t *opus, size_t opus_capacity,
                                        size_t *opus_size)
{
    if (!s_encoder || !pcm || !opus || !opus_size || sample_count != 320 ||
        opus_capacity < (size_t)s_encoder_output_bytes) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_audio_enc_in_frame_t input = {
        .buffer = (uint8_t *)pcm,
        .len = (uint32_t)(sample_count * sizeof(int16_t)),
    };
    esp_audio_enc_out_frame_t output = {
        .buffer = opus,
        .len = (uint32_t)opus_capacity,
        .encoded_bytes = 0,
    };
    esp_audio_err_t err = esp_opus_enc_process(s_encoder, &input, &output);
    if (err != ESP_AUDIO_ERR_OK) {
        *opus_size = 0;
        return ESP_FAIL;
    }

    *opus_size = output.encoded_bytes;
    return ESP_OK;
}

static void voice_stream_decode_task(void *arg)
{
    (void)arg;
    uint8_t pcm[VOICE_STREAM_PCM_BYTES];
    bool playing = false;

    while (1) {
        voice_stream_packet_t packet = {0};
        if (xQueueReceive(s_rx_queue, &packet, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_audio_dec_in_raw_t raw = {
            .buffer = packet.data,
            .len = (uint32_t)packet.len,
            .consumed = 0,
        };
        esp_audio_dec_out_frame_t frame = {
            .buffer = pcm,
            .len = sizeof(pcm),
            .decoded_size = 0,
        };
        esp_audio_dec_info_t info = {0};
        esp_audio_err_t err = esp_opus_dec_decode(s_decoder, &raw, &frame, &info);
        if (err != ESP_AUDIO_ERR_OK || frame.decoded_size == 0) {
            ESP_LOGW(TAG, "Opus decode failed err=%d packet=%u", err, (unsigned int)packet.len);
            continue;
        }

        if (!playing || s_playback_cleared) {
            voice_output_stream_clear();
            if (voice_output_stream_start() != ESP_OK) {
                ESP_LOGW(TAG, "stream playback start failed");
                continue;
            }
            playing = true;
            s_playback_cleared = false;
            s_playback_active = true;
            ESP_LOGI(TAG, "stream playback started on first PCM frame");
        }
        if (voice_output_stream_write(frame.buffer, frame.decoded_size) != ESP_OK) {
            ESP_LOGW(TAG, "stream PCM write failed");
            voice_output_stream_stop();
            playing = false;
            s_playback_active = false;
        }
    }
}