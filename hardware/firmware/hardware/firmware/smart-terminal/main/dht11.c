#include "dht11.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h" 
#include <esp_log.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/portmacro.h"

#define DHT11_GPIO 5 // DHT11引脚定义

static const char *TAG = "DHT11";

// 温度 湿度buffer
static uint8_t buffer[5];
static int64_t phase_duration[3] = {0};
static int64_t bit_duration_low[40] = {0};
static int64_t bit_duration_high[40] = {0};
static portMUX_TYPE s_dht11_mux = portMUX_INITIALIZER_UNLOCKED;

// 初始化DHT11传感器，引脚模式设定，并等待上电时间
void DHT11_Init()
{
    gpio_config_t cnf = {
        .mode = GPIO_MODE_OUTPUT,      // 改为推挽输出，确保电平绝对受控
        .pin_bit_mask = 1ULL << DHT11_GPIO,
        .pull_up_en = 0,               // 模块自带上拉，这里不需要内部上拉
    };
    gpio_config(&cnf);
    vTaskDelay(pdMS_TO_TICKS(1500));  // 等待传感器稳定
}

// 等待DHT11引脚进入期望的状态，超时则返回错误
static esp_err_t wait_pin_state(uint32_t timeout, int expected_pin_state)
{
    int64_t start_time = esp_timer_get_time();
    while (esp_timer_get_time() - start_time <= timeout)
    {
        if (gpio_get_level(DHT11_GPIO) == expected_pin_state)
            return ESP_OK;
        esp_rom_delay_us(1);
    }
    return ESP_FAIL;
}

// 读取DHT11传感器数据
esp_err_t DHT11_Read(int *temperature, int *humidity)
{
    memset(buffer, 0, sizeof(buffer));

    portENTER_CRITICAL(&s_dht11_mux);

    // 用强烈的推挽输出发送唤醒低电平
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT); 
    gpio_set_level(DHT11_GPIO, 0);
    portEXIT_CRITICAL(&s_dht11_mux);

    vTaskDelay(pdMS_TO_TICKS(22)); // 延长到 22ms，确保 DHT11 绝对能收到唤醒

    portENTER_CRITICAL(&s_dht11_mux);

    // 释放总线：先主动拉高，再瞬间切为输入
    gpio_set_level(DHT11_GPIO, 1);
    esp_rom_delay_us(40);          // 给总线 40微秒 的释放缓冲时间
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);

    // 接下来进入严格的等待状态
    // Phase A: 等待 DHT11 回应拉低
    if (wait_pin_state(150, 0) == ESP_FAIL) {
        portEXIT_CRITICAL(&s_dht11_mux);
        ESP_LOGE(TAG, "Phase A Fail, slave not set LOW.");
        return ESP_FAIL;
    }
    
    // Phase B: 等待 DHT11 重新拉高
    if (wait_pin_state(150, 1) == ESP_FAIL) {
        portEXIT_CRITICAL(&s_dht11_mux);
        ESP_LOGE(TAG, "Phase B Fail, slave not set HIGH.");
        return ESP_FAIL;
    }
    
    // Phase C: 等待 DHT11 再次拉低开始发送数据
    if (wait_pin_state(150, 0) == ESP_FAIL) {
        portEXIT_CRITICAL(&s_dht11_mux);
        ESP_LOGE(TAG, "Phase C Fail, slave not set LOW to start sending.");
        return ESP_FAIL;
    }
    
    // 3. 开始接收 40 位数据
    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 8; i++) {
            // 等待低电平结束 (每位数据开头的 50us 低电平)
            int64_t timeout_tick = esp_timer_get_time();
            while (gpio_get_level(DHT11_GPIO) == 0) {
                if (esp_timer_get_time() - timeout_tick > 80) {
                    portEXIT_CRITICAL(&s_dht11_mux);
                    ESP_LOGE(TAG, "Timeout waiting for high level.");
                    return ESP_FAIL;
                }
            }
            
            // 到了高电平，开始计算高电平持续时间以区分 0 和 1
            int64_t high_start = esp_timer_get_time();
            timeout_tick = esp_timer_get_time();
            while (gpio_get_level(DHT11_GPIO) == 1) {
                if (esp_timer_get_time() - timeout_tick > 100) {
                    portEXIT_CRITICAL(&s_dht11_mux);
                    ESP_LOGE(TAG, "Timeout waiting for low level.");
                    return ESP_FAIL;
                }
            }
            
            // DHT11 高电平：26us-28us 代表 '0'，70us 代表 '1'
            if ((esp_timer_get_time() - high_start) > 40) {
                buffer[j] |= (1U << (7 - i));
            }
        }
    }

    portEXIT_CRITICAL(&s_dht11_mux);

    // 4. 校验数据
    if (((buffer[0] + buffer[1] + buffer[2] + buffer[3]) & 0xFF) != buffer[4]) {
        ESP_LOGE(TAG, "Checksum error. [%02X %02X %02X %02X] != %02X", 
                 buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
        return ESP_FAIL;
    }

    *humidity = buffer[0];
    *temperature = buffer[2];
    ESP_LOGI(TAG, "Temperature: %d°C, Humidity: %d%%", *temperature, *humidity);
    return ESP_OK;
}
