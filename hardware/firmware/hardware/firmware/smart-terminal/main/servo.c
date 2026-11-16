#include "servo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "servo";

#define SERVO_GPIO GPIO_NUM_48
#define SERVO_LEDC_TIMER LEDC_TIMER_1
#define SERVO_LEDC_CHANNEL LEDC_CHANNEL_1
#define SERVO_LEDC_MODE LEDC_LOW_SPEED_MODE
#define SERVO_FREQUENCY_HZ 50
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2500
#define SERVO_STEP_DELAY_MS 50

static int s_current_angle = 0;
static int s_min_angle = -40;
static int s_max_angle = 40;
static bool s_servo_initialized = false;
static char s_report_url[256] = {0};

typedef struct {
    char *buffer;
    int capacity;
    int len;
} http_response_buffer_t;

static esp_err_t servo_http_event_handler(esp_http_client_event_t *evt)
{
    http_response_buffer_t *response = (http_response_buffer_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && response && evt->data && evt->data_len > 0) {
        int copy_len = evt->data_len;
        if (response->len + copy_len >= response->capacity) {
            copy_len = response->capacity - response->len - 1;
        }
        if (copy_len > 0) {
            memcpy(response->buffer + response->len, evt->data, copy_len);
            response->len += copy_len;
            response->buffer[response->len] = '\0';
        }
    }
    return ESP_OK;
}

static uint32_t angle_to_duty(int angle)
{
    if (angle < s_min_angle) angle = s_min_angle;
    if (angle > s_max_angle) angle = s_max_angle;
    int pulse_angle = angle + 90;
    uint32_t min_duty = (SERVO_MIN_PULSE_US * 8191) / (1000000 / SERVO_FREQUENCY_HZ);
    uint32_t max_duty = (SERVO_MAX_PULSE_US * 8191) / (1000000 / SERVO_FREQUENCY_HZ);
    return min_duty + (uint32_t)((float)(max_duty - min_duty) * pulse_angle / 180.0f);
}

static esp_err_t servo_apply_angle(int angle)
{
    uint32_t duty = angle_to_duty(angle);
    esp_err_t err = ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty failed: %s", esp_err_to_name(err));
        return err;
    }
    err = ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t sg90_init(void)
{
    if (s_servo_initialized) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SERVO_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = SERVO_LEDC_MODE,
        .timer_num = SERVO_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = SERVO_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channel_cfg = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = angle_to_duty(s_current_angle),
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    s_servo_initialized = true;
    ESP_LOGI(TAG, "SG90 initialized on GPIO %d", SERVO_GPIO);
    return ESP_OK;
}

esp_err_t sg90_set_angle(int angle)
{
    if (!s_servo_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (angle < s_min_angle) {
        angle = s_min_angle;
    }
    if (angle > s_max_angle) {
        angle = s_max_angle;
    }
    int step = (angle > s_current_angle) ? 1 : -1;
    while (s_current_angle != angle) {
        s_current_angle += step;
        esp_err_t err = servo_apply_angle(s_current_angle);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_DELAY_MS));
    }
    ESP_LOGI(TAG, "SG90 set angle %+d", angle);
    return ESP_OK;
}

static esp_err_t report_servo_status(int angle, const char *reason)
{
    if (s_report_url[0] == '\0') {
        ESP_LOGW(TAG, "Report URL not configured");
        return ESP_ERR_INVALID_STATE;
    }

    char body[256];
    int len = snprintf(body, sizeof(body), "{\"angle\":%d,\"reason\":\"%s\"}", angle, reason ? reason : "status");
    if (len < 0 || len >= sizeof(body)) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = s_report_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, len);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "report failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "report sent angle=%d reason=%s", angle, reason ? reason : "status");
    }
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t fetch_target_angle(const char *fetch_url, int *target_angle)
{
    if (!fetch_url || !target_angle) {
        return ESP_ERR_INVALID_ARG;
    }

    char buffer[1024] = {0};
    http_response_buffer_t response = {
        .buffer = buffer,
        .capacity = sizeof(buffer),
        .len = 0,
    };

    esp_http_client_config_t config = {
        .url = fetch_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .event_handler = servo_http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fetch failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    if (response.len == 0) {
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        ESP_LOGW(TAG, "servo command empty response status=%d", status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "servo command response: %s", buffer);
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        ESP_LOGW(TAG, "JSON parse failed: %s", buffer);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *angle_item = cJSON_GetObjectItem(json, "target_angle");
    if (!cJSON_IsNumber(angle_item)) {
        angle_item = cJSON_GetObjectItem(json, "targetAngle");
    }
    if (!cJSON_IsNumber(angle_item)) {
        angle_item = cJSON_GetObjectItem(json, "angle");
    }
    if (!cJSON_IsNumber(angle_item)) {
        cJSON *data = cJSON_GetObjectItem(json, "data");
        if (cJSON_IsObject(data)) {
            angle_item = cJSON_GetObjectItem(data, "target_angle");
            if (!cJSON_IsNumber(angle_item)) {
                angle_item = cJSON_GetObjectItem(data, "targetAngle");
            }
            if (!cJSON_IsNumber(angle_item)) {
                angle_item = cJSON_GetObjectItem(data, "angle");
            }
        }
    }
    if (!cJSON_IsNumber(angle_item)) {
        ESP_LOGW(TAG, "angle field not found in response: %s", buffer);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *target_angle = angle_item->valueint;
    cJSON_Delete(json);
    return ESP_OK;
}

typedef struct {
    char fetch_url[256];
    char report_url[256];
} servo_command_args_t;

static void servo_command_task(void *arg)
{
    servo_command_args_t *args = (servo_command_args_t *)arg;
    const char *fetch_url = args->fetch_url;
    const char *report_url = args->report_url;

    snprintf(s_report_url, sizeof(s_report_url), "%s", report_url);

    while (1) {
        int target_angle = 0;
        if (fetch_target_angle(fetch_url, &target_angle) == ESP_OK) {
            ESP_LOGI(TAG, "Fetched target_angle=%d", target_angle);
            if (target_angle < s_min_angle) target_angle = s_min_angle;
            if (target_angle > s_max_angle) target_angle = s_max_angle;
            if (target_angle != s_current_angle) {
                sg90_set_angle(target_angle);
                if (target_angle == s_min_angle || target_angle == s_max_angle) {
                    report_servo_status(target_angle, "limit_reached");
                } else {
                    report_servo_status(target_angle, "moved");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t sg90_start_command_task(const char *fetch_url, const char *report_url)
{
    if (!s_servo_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!fetch_url) {
        fetch_url = SERVO_COMMAND_FETCH_URL;
    }
    if (!report_url) {
        report_url = SERVO_STATUS_REPORT_URL;
    }

    servo_command_args_t *args = calloc(1, sizeof(servo_command_args_t));
    if (!args) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(args->fetch_url, fetch_url, sizeof(args->fetch_url));
    strlcpy(args->report_url, report_url, sizeof(args->report_url));

    BaseType_t ret = xTaskCreate(servo_command_task, "servo_cmd", 8192, args, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create servo command task");
        free(args);
        return ESP_FAIL;
    }
    return ESP_OK;
}
