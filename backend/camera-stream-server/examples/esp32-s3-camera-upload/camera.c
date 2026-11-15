#include "camera.h"
#include "esp_camera.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"

static const char *TAG = "camera";

// OV2640 pinout for ESP32-S3 using only the pins you can physically wire up.
// Avoiding GPIO 2/4/5/7, which are already used by MAX30102/DHT11.
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   14
#define CAM_PIN_SIOD   15
#define CAM_PIN_SIOC   16
#define CAM_PIN_D7     17
#define CAM_PIN_D6     18
#define CAM_PIN_D5     21
#define CAM_PIN_D4     47
#define CAM_PIN_D3     39
#define CAM_PIN_D2     40
#define CAM_PIN_D1     41
#define CAM_PIN_D0     42
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   8
#define CAM_PIN_PCLK   9

// Stable mode: 1 FPS, QVGA. Use this to verify the whole link first.
// Smooth mode target: 5-8 FPS, QQVGA/QVGA with stronger JPEG compression.
// This token is the ingest token, not the viewer token.
#define CAMERA_UPLOAD_URL "https://api.example.com/api/cameras/room-1/frame?token=YOUR_INGEST_TOKEN"
#define CAMERA_UPLOAD_INTERVAL_MS 1000
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY 18
#define CAMERA_FB_COUNT 1

// Suggested smooth mode after stable upload:
// #define CAMERA_UPLOAD_INTERVAL_MS 200
// #define CAMERA_FRAME_SIZE FRAMESIZE_QQVGA
// #define CAMERA_JPEG_QUALITY 22
// #define CAMERA_FB_COUNT 1

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = CAMERA_FRAME_SIZE,
    .jpeg_quality = CAMERA_JPEG_QUALITY,
    .fb_count = CAMERA_FB_COUNT,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
};

static bool is_complete_jpeg(const camera_fb_t *fb)
{
    if (fb == NULL || fb->buf == NULL || fb->len < 4) {
        return false;
    }

    bool has_start = fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
    bool has_end = fb->buf[fb->len - 2] == 0xFF && fb->buf[fb->len - 1] == 0xD9;
    return has_start && has_end;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ERROR) {
        ESP_LOGW(TAG, "HTTP event error");
    }
    return ESP_OK;
}

static esp_err_t camera_http_post(const uint8_t *data, size_t len)
{
    esp_http_client_config_t config = {
        .url = CAMERA_UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .event_handler = http_event_handler,
        .buffer_size = 2048,
        .buffer_size_tx = 8192,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    char content_length[16];
    snprintf(content_length, sizeof(content_length), "%u", (unsigned int)len);

    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    esp_http_client_set_header(client, "Content-Length", content_length);
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_header(client, "User-Agent", "esp32-s3-camera");
    esp_http_client_set_post_field(client, (const char *)data, len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: err=%s status=%d", esp_err_to_name(err), status);
    } else if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "HTTP POST non-2xx: status=%d response_len=%d", status, content_len);
        err = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "HTTP POST ok: status=%d frame_bytes=%zu", status, len);
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t init_camera(void)
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        sensor->set_framesize(sensor, CAMERA_FRAME_SIZE);
        sensor->set_quality(sensor, CAMERA_JPEG_QUALITY);
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_awb_gain(sensor, 1);
        sensor->set_whitebal(sensor, 1);
    }

    ESP_LOGI(TAG, "Camera initialized");
    return ESP_OK;
}

void camera_upload_task(void *arg)
{
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGE(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(CAMERA_UPLOAD_INTERVAL_MS));
            continue;
        }

        ESP_LOGI(TAG, "Captured frame: len=%zu format=%d", fb->len, fb->format);

        if (fb->format != PIXFORMAT_JPEG || !is_complete_jpeg(fb)) {
            ESP_LOGW(TAG, "Skip invalid JPEG frame: len=%zu format=%d first=%02x%02x last=%02x%02x",
                     fb->len,
                     fb->format,
                     fb->len > 1 ? fb->buf[0] : 0,
                     fb->len > 1 ? fb->buf[1] : 0,
                     fb->len > 1 ? fb->buf[fb->len - 2] : 0,
                     fb->len > 1 ? fb->buf[fb->len - 1] : 0);
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(CAMERA_UPLOAD_INTERVAL_MS));
            continue;
        }

        esp_err_t err = camera_http_post(fb->buf, fb->len);
        esp_camera_fb_return(fb);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Upload failed, wait before retrying");
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(CAMERA_UPLOAD_INTERVAL_MS));
        }
    }
}
