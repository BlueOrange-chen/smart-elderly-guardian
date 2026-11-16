#include "rgb_led.h"

#include "driver/gpio.h"
#include "driver/rmt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>

#define RGB_LED_GPIO GPIO_NUM_38
#define RGB_LED_CHANNEL RMT_CHANNEL_0
#define RGB_LED_CLK_DIV 2

#define WS2812_T0H 14
#define WS2812_T0L 32
#define WS2812_T1H 30
#define WS2812_T1L 16
#define WS2812_RESET 20000
#define RGB_THINKING_TRANSITION_MS 1000
#define RGB_THINKING_STEP_MS 50

static const char *TAG = "rgb_led";
static SemaphoreHandle_t s_lock;
static uint8_t s_activity_count;
static bool s_initialized;

typedef enum {
    RGB_LED_STATE_NORMAL,
    RGB_LED_STATE_STARTING,
    RGB_LED_STATE_ACTIVITY,
    RGB_LED_STATE_THINKING,
    RGB_LED_STATE_PLAYBACK,
} rgb_led_state_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb_led_color_t;

static rgb_led_state_t s_state = RGB_LED_STATE_NORMAL;
static TaskHandle_t s_thinking_task;
static bool s_playback_active;
static uint32_t s_thinking_generation;

static const rgb_led_color_t s_thinking_colors[] = {
    {32, 0, 0},
    {32, 24, 0},
    {0, 0, 32},
    {0, 32, 0},
};

static uint8_t interpolate_channel(uint8_t from, uint8_t to, int step, int steps)
{
    int delta = (int)to - (int)from;
    return (uint8_t)((int)from + (delta * step) / steps);
}

static void write_color_locked(uint8_t red, uint8_t green, uint8_t blue)
{
    rmt_item32_t items[24] = {0};
    uint8_t grb[3] = {green, red, blue};

    for (size_t byte = 0; byte < 3; ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            bool one = (grb[byte] & (1U << (7 - bit))) != 0;
            rmt_item32_t *item = &items[byte * 8 + bit];
            item->level0 = 1;
            item->duration0 = one ? WS2812_T1H : WS2812_T0H;
            item->level1 = 0;
            item->duration1 = one ? WS2812_T1L : WS2812_T0L;
        }
    }

    rmt_write_items(RGB_LED_CHANNEL, items, 24, true);
    rmt_wait_tx_done(RGB_LED_CHANNEL, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_initialized || !s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    write_color_locked(red, green, blue);
    xSemaphoreGive(s_lock);
}

static void rgb_led_thinking_task(void *arg)
{
    (void)arg;
    size_t color_index = 0;
    uint32_t observed_generation = 0;
    const int transition_steps = RGB_THINKING_TRANSITION_MS / RGB_THINKING_STEP_MS;

    while (1) {
        if (!s_initialized || !s_lock) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool thinking_active = s_state == RGB_LED_STATE_THINKING;
        uint32_t current_generation = s_thinking_generation;
        xSemaphoreGive(s_lock);

        if (!thinking_active) {
            color_index = 0;
            observed_generation = current_generation;
            vTaskDelay(pdMS_TO_TICKS(RGB_THINKING_STEP_MS));
            continue;
        }

        if (current_generation != observed_generation) {
            observed_generation = current_generation;
            color_index = 0;
        }

        size_t next_index = (color_index + 1) % (sizeof(s_thinking_colors) / sizeof(s_thinking_colors[0]));
        const rgb_led_color_t *from = &s_thinking_colors[color_index];
        const rgb_led_color_t *to = &s_thinking_colors[next_index];
        bool restart_transition = false;

        for (int step = 0; step <= transition_steps; step++) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool thinking = s_state == RGB_LED_STATE_THINKING;
            current_generation = s_thinking_generation;
            bool same_generation = current_generation == observed_generation;
            if (thinking && same_generation) {
                write_color_locked(
                    interpolate_channel(from->red, to->red, step, transition_steps),
                    interpolate_channel(from->green, to->green, step, transition_steps),
                    interpolate_channel(from->blue, to->blue, step, transition_steps));
            }
            xSemaphoreGive(s_lock);

            if (!thinking) {
                break;
            }
            if (!same_generation) {
                observed_generation = current_generation;
                color_index = 0;
                restart_transition = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(RGB_THINKING_STEP_MS));
        }

        if (restart_transition) {
            continue;
        }
        color_index = next_index;
    }
}

esp_err_t rgb_led_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    rmt_config_t config = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RGB_LED_CHANNEL,
        .gpio_num = RGB_LED_GPIO,
        .clk_div = RGB_LED_CLK_DIV,
        .mem_block_num = 1,
        .flags = 0,
        .tx_config = {
            .loop_en = false,
            .carrier_en = false,
            .idle_output_en = true,
            .idle_level = RMT_IDLE_LEVEL_LOW,
        },
    };

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_driver_install(RGB_LED_CHANNEL, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    xTaskCreate(rgb_led_thinking_task, "rgb_thinking", 2048, NULL, 3, &s_thinking_task);
    rgb_led_set_starting();
    ESP_LOGI(TAG, "RGB LED ready on GPIO%d", RGB_LED_GPIO);
    return ESP_OK;
}

void rgb_led_set_starting(void)
{
    s_state = RGB_LED_STATE_STARTING;
    set_color(0, 0, 32);
}

void rgb_led_set_normal(void)
{
    s_state = RGB_LED_STATE_NORMAL;
    set_color(0, 32, 0);
}

void rgb_led_activity_start(void)
{
    if (!s_initialized || !s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_activity_count < UINT8_MAX) {
        ++s_activity_count;
    }
    s_state = RGB_LED_STATE_ACTIVITY;
    write_color_locked(32, 24, 0);
    xSemaphoreGive(s_lock);
}

void rgb_led_activity_end(void)
{
    if (!s_initialized || !s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_activity_count > 0) {
        --s_activity_count;
    }
    if (s_activity_count == 0 && s_state != RGB_LED_STATE_THINKING && s_state != RGB_LED_STATE_PLAYBACK) {
        s_state = RGB_LED_STATE_NORMAL;
        write_color_locked(0, 32, 0);
    }
    xSemaphoreGive(s_lock);
}

void rgb_led_thinking_start(void)
{
    if (!s_initialized || !s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    ++s_thinking_generation;
    s_state = RGB_LED_STATE_THINKING;
    const rgb_led_color_t *color = &s_thinking_colors[0];
    write_color_locked(color->red, color->green, color->blue);
    xSemaphoreGive(s_lock);
}

void rgb_led_thinking_end(void)
{
    if (!s_initialized || !s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == RGB_LED_STATE_THINKING) {
        s_state = RGB_LED_STATE_NORMAL;
        write_color_locked(0, 32, 0);
    }
    xSemaphoreGive(s_lock);
}

void rgb_led_playback_start(void)
{
    if (!s_initialized || !s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_playback_active && s_activity_count < UINT8_MAX) {
        ++s_activity_count;
    }
    s_playback_active = true;
    s_state = RGB_LED_STATE_PLAYBACK;
    write_color_locked(32, 0, 0);
    xSemaphoreGive(s_lock);
}

void rgb_led_playback_end(void)
{
    if (!s_initialized || !s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_playback_active && s_activity_count > 0) {
        --s_activity_count;
    }
    s_playback_active = false;
    if (s_activity_count == 0 && s_state == RGB_LED_STATE_PLAYBACK) {
        s_state = RGB_LED_STATE_NORMAL;
        write_color_locked(0, 32, 0);
    }
    xSemaphoreGive(s_lock);
}