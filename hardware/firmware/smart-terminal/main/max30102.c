#include "max30102.h"
#include "esp_rom_sys.h"

static const char *TAG = "max30102";

#define MAX30102_I2C_SCL 4           // GPIO number used for I2C master clock
#define MAX30102_I2C_SDA 2            // GPIO number used for I2C master data

#define MAX30102_GPIO_INT 7 // GPIO number used for MAX30102 int

// | B7 | B6 | B5 | B4 | B3 | B2 | B1 | B0 | WRITE ADDRESS | READ ADDRESS |
// | 1  | 0  | 1  | 0  | 1  | 1  | 1  | R/W| 0xAE          | 0xAF         |
#define MAX30102_ADDR 0x57 //  I2C device MAX30102's 7-bit address
#define MAX30102_PART_ID_REG_ADDR 0xff
#define MAX30102_EXPECTED_PART_ID 0x15
#define MAX30102_PROBE_RETRIES 5
#define MAX30102_RESET_RETRIES 10
#define MAX30102_SOFT_I2C_DELAY_US 5

// static QueueHandle_t gpio_evt_queue = NULL;



int latest_spo2 = 0;
int latest_heart_rate = 0;

static bool s_use_soft_i2c = false;

static void soft_i2c_delay(void)
{
    esp_rom_delay_us(MAX30102_SOFT_I2C_DELAY_US);
}

static void soft_i2c_sda_set(int level)
{
    gpio_set_level(MAX30102_I2C_SDA, level);
}

static void soft_i2c_scl_set(int level)
{
    gpio_set_level(MAX30102_I2C_SCL, level);
}

static int soft_i2c_sda_get(void)
{
    return gpio_get_level(MAX30102_I2C_SDA);
}

static esp_err_t soft_i2c_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MAX30102_I2C_SDA) | (1ULL << MAX30102_I2C_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    soft_i2c_sda_set(1);
    soft_i2c_scl_set(1);
    soft_i2c_delay();
    return ESP_OK;
}

static void soft_i2c_start(void)
{
    soft_i2c_sda_set(1);
    soft_i2c_scl_set(1);
    soft_i2c_delay();
    soft_i2c_sda_set(0);
    soft_i2c_delay();
    soft_i2c_scl_set(0);
    soft_i2c_delay();
}

static void soft_i2c_stop(void)
{
    soft_i2c_sda_set(0);
    soft_i2c_delay();
    soft_i2c_scl_set(1);
    soft_i2c_delay();
    soft_i2c_sda_set(1);
    soft_i2c_delay();
}

static bool soft_i2c_write_byte(uint8_t data)
{
    for (int bit = 7; bit >= 0; bit--) {
        soft_i2c_sda_set((data >> bit) & 0x01);
        soft_i2c_delay();
        soft_i2c_scl_set(1);
        soft_i2c_delay();
        soft_i2c_scl_set(0);
        soft_i2c_delay();
    }

    soft_i2c_sda_set(1);
    soft_i2c_delay();
    soft_i2c_scl_set(1);
    soft_i2c_delay();
    bool ack = soft_i2c_sda_get() == 0;
    soft_i2c_scl_set(0);
    soft_i2c_delay();
    return ack;
}

static uint8_t soft_i2c_read_byte(bool ack)
{
    uint8_t data = 0;

    soft_i2c_sda_set(1);
    for (int bit = 7; bit >= 0; bit--) {
        soft_i2c_delay();
        soft_i2c_scl_set(1);
        soft_i2c_delay();
        if (soft_i2c_sda_get()) {
            data |= (1 << bit);
        }
        soft_i2c_scl_set(0);
        soft_i2c_delay();
    }

    soft_i2c_sda_set(ack ? 0 : 1);
    soft_i2c_delay();
    soft_i2c_scl_set(1);
    soft_i2c_delay();
    soft_i2c_scl_set(0);
    soft_i2c_sda_set(1);
    soft_i2c_delay();

    return data;
}

static esp_err_t soft_i2c_write_reg(uint8_t reg_addr, uint8_t data)
{
    soft_i2c_start();
    if (!soft_i2c_write_byte((MAX30102_ADDR << 1) | 0)) {
        soft_i2c_stop();
        return ESP_ERR_TIMEOUT;
    }
    if (!soft_i2c_write_byte(reg_addr) || !soft_i2c_write_byte(data)) {
        soft_i2c_stop();
        return ESP_ERR_TIMEOUT;
    }
    soft_i2c_stop();
    return ESP_OK;
}

static esp_err_t soft_i2c_read_reg(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    soft_i2c_start();
    if (!soft_i2c_write_byte((MAX30102_ADDR << 1) | 0) || !soft_i2c_write_byte(reg_addr)) {
        soft_i2c_stop();
        return ESP_ERR_TIMEOUT;
    }

    soft_i2c_start();
    if (!soft_i2c_write_byte((MAX30102_ADDR << 1) | 1)) {
        soft_i2c_stop();
        return ESP_ERR_TIMEOUT;
    }

    for (size_t index = 0; index < len; index++) {
        data[index] = soft_i2c_read_byte(index + 1 < len);
    }
    soft_i2c_stop();
    return ESP_OK;
}

/**
 * @brief Read a sequence of bytes from a MAX30102 registers
 */
static esp_err_t max30102_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return soft_i2c_read_reg(reg_addr, data, len);
}

/**
 * @brief Write a byte to a MAX30102 register
 */
static esp_err_t max30102_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    return soft_i2c_write_reg(reg_addr, data);
}

static esp_err_t max30102_probe(void)
{
    uint8_t part_id = 0;
    esp_err_t err = ESP_FAIL;

    for (int attempt = 1; attempt <= MAX30102_PROBE_RETRIES; attempt++) {
        err = max30102_register_read(MAX30102_PART_ID_REG_ADDR, &part_id, 1);
        if (err == ESP_OK && part_id == MAX30102_EXPECTED_PART_ID) {
            ESP_LOGI(TAG, "MAX30102 found, part id=0x%02x", part_id);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "MAX30102 probe attempt %d failed: err=%s part_id=0x%02x", attempt, esp_err_to_name(err), part_id);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (err == ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 unexpected part id: 0x%02x", part_id);
        return ESP_ERR_NOT_FOUND;
    }
    return err;
}

static esp_err_t max30102_wait_reset_done(void)
{
    uint8_t mode_config = 0;
    esp_err_t err = ESP_FAIL;

    for (int attempt = 1; attempt <= MAX30102_RESET_RETRIES; attempt++) {
        err = max30102_register_read(0x09, &mode_config, 1);
        if (err == ESP_OK && (mode_config & 0x40) == 0) {
            return ESP_OK;
        }

        ESP_LOGW(TAG, "MAX30102 reset wait attempt %d: err=%s mode=0x%02x", attempt, esp_err_to_name(err), mode_config);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return err == ESP_OK ? ESP_ERR_TIMEOUT : err;
}

/*void max30102_task(void *arg)
{
    uint8_t byte[6];
    int data[2];  // [0] = RED, [1] = IR
    float R, spo2;

    while (1)
    {
        esp_err_t err = max30102_register_read(0x07, byte, 6);
        if (err == ESP_OK)
        {
            data[0] = ((byte[0] << 16 | byte[1] << 8 | byte[2]) & 0x03FFFF); // RED
            data[1] = ((byte[3] << 16 | byte[4] << 8 | byte[5]) & 0x03FFFF); // IR

            if (data[1] > 100000)  // 用 IR 判断是否有手指
            {
                R = (float)data[0] / (float)data[1];
                spo2 = 110.0 - 25.0 * R;

                if (spo2 > 100.0) spo2 = 100.0;
                if (spo2 < 70.0) spo2 = 70.0;

                printf("估算 SpO2 = %.2f%% (R=%.3f)\n", spo2, R);
            }
            else
            {
                printf("没有手指检测\n");
            }
        }
        else
        {
            printf("读取数据失败，错误码：%d\n", err);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
} */


/*static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}*/

/**
 * @brief init the gpio intr for MAX30102
 */
/*static esp_err_t max30102_gpio_intr_init()
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << MAX30102_GPIO_INT);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // start gpio task
    xTaskCreate(gpio_intr_task, "gpio_intr_task", 2048, NULL, 10, NULL);
    // install gpio isr service
    gpio_install_isr_service(0);
    // hook isr handler for specific gpio pin
    gpio_isr_handler_add(MAX30102_GPIO_INT, gpio_isr_handler, (void *)MAX30102_GPIO_INT);
    return ESP_OK;
}*/

void get_temp()
{
    uint8_t byte[2];
    float temp;
    ESP_ERROR_CHECK(max30102_register_write_byte(0x21, 0x01));
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(max30102_register_read(0x1f, &byte[0], 1));
    ESP_ERROR_CHECK(max30102_register_read(0x20, &byte[1], 1));
    temp = (int8_t)(byte[0]) + byte[1] * 0.0625;
    printf("Temp: %f\n", temp);

    // FIFO
    ESP_ERROR_CHECK(max30102_register_write_byte(0x04, 0x00)); // clear FIFO Write Pointer
    ESP_ERROR_CHECK(max30102_register_write_byte(0x05, 0x00)); // clear FIFO Overflow Counter
    ESP_ERROR_CHECK(max30102_register_write_byte(0x06, 0x00)); // clear FIFO Read Pointer

    // clear PPG_RDY ! Cannot receive the first interrupt without clearing !
    uint8_t data;
    ESP_ERROR_CHECK(max30102_register_read(0x00, &data, 1));
    ESP_ERROR_CHECK(max30102_register_read(0x01, &data, 1));
}

static esp_err_t max30102_get_fifo_pointers(uint8_t *write_ptr, uint8_t *read_ptr)
{
    esp_err_t ret;
    ret = max30102_register_read(0x04, write_ptr, 1); // FIFO_WR_PTR寄存器地址0x04
    if (ret != ESP_OK) return ret;
    ret = max30102_register_read(0x06, read_ptr, 1);  // FIFO_RD_PTR寄存器地址0x06
    return ret;
}

static esp_err_t max30102_read_fifo(int *red, int *ir)
{
    uint8_t buffer[6];
    esp_err_t ret = max30102_register_read(0x07, buffer, 6);  // FIFO_DATA寄存器地址0x07，读6字节
    if (ret != ESP_OK) return ret;

    *red = ((buffer[0] << 16) | (buffer[1] << 8) | buffer[2]) & 0x03FFFF;
    *ir  = ((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]) & 0x03FFFF;

    return ESP_OK;
}


void max30102_task(void *arg)
{
    uint8_t write_ptr, read_ptr;
    int red, ir;
    float ratio;
    int spo2, heart_rate;

    int spo2_sum = 0;
    int heart_rate_sum = 0;
    int finger_on_count = 0;
    bool sensor_active_logged = false;

    TickType_t last_print_tick = xTaskGetTickCount();

    while (1)
    {
        // 读写指针
        if (max30102_get_fifo_pointers(&write_ptr, &read_ptr) != ESP_OK)
        {
            printf("读取FIFO指针失败\n");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int samples = (write_ptr - read_ptr) & 0x1F;

        for (int i = 0; i < samples; i++)
        {
            if (max30102_read_fifo(&red, &ir) != ESP_OK)
            {
                printf("读取FIFO数据失败\n");
                break;
            }

            if (ir > 100000) // 判断有手指
            {
                ratio = (float)red / (float)ir;
                spo2 = (int)(110.0 - 25.0 * ratio);
                if (spo2 > 100) spo2 = 100;
                if (spo2 < 70) spo2 = 70;

                heart_rate = (int)(30.354 * ratio + 94.845 - 45.060 * ratio * ratio);

                spo2_sum += spo2;
                heart_rate_sum += heart_rate;
                finger_on_count++;
            }
        }

        // 每500ms打印一次平均值
        if (xTaskGetTickCount() - last_print_tick >= pdMS_TO_TICKS(500))
        {
            if (finger_on_count > 0)
            {
                if (!sensor_active_logged) {
                    ESP_LOGI(TAG, "MAX30102 active: first valid samples red=%d ir=%d spo2=%d heart_rate=%d", red, ir, spo2, heart_rate);
                    sensor_active_logged = true;
                }
                printf("平均血氧: %d%%, 平均心率: %d bpm\n", spo2_sum / finger_on_count, heart_rate_sum / finger_on_count);
                latest_spo2 = spo2;
                latest_heart_rate = heart_rate;
            }
            else
            {
                //printf("没有检测到手指\n");
                latest_spo2 = 0 ;
                latest_heart_rate = 0 ;
            }

            // 重置计数和累计
            spo2_sum = 0;
            heart_rate_sum = 0;
            finger_on_count = 0;

            last_print_tick = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}





esp_err_t max30102_init(void)
{
    esp_err_t err = soft_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 software I2C init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_use_soft_i2c = true;
    ESP_LOGI(TAG, "MAX30102 I2C initialized successfully (software)");

    err = max30102_probe();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 not detected on SDA GPIO%d/SCL GPIO%d, addr=0x%02x: %s", MAX30102_I2C_SDA, MAX30102_I2C_SCL, MAX30102_ADDR, esp_err_to_name(err));
        return err;
    }

    //max30102_gpio_intr_init();
    ESP_LOGI(TAG, "MAX30102 GPIO INTR disabled, polling FIFO");

    // reset
    err = max30102_register_write_byte(0x09, 0x40);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 reset failed: %s", esp_err_to_name(err));
        return err;
    }
    err = max30102_wait_reset_done();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 reset timeout: %s", esp_err_to_name(err));
        return err;
    }

    /*// Interrupt Enable
    ESP_ERROR_CHECK(max30102_register_write_byte(0x02, 0xc0)); // enable interrupts: A_FULL: FIFO Almost Full Flag and PPG_RDY: New FIFO Data Ready
    ESP_ERROR_CHECK(max30102_register_write_byte(0x03, 0x02)); // enable interrupt: DIE_TEMP_RDY: Internal Temperature Ready Flag   */
    // 不使能任何中断
    err = max30102_register_write_byte(0x02, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 interrupt disable failed: %s", esp_err_to_name(err));
        return err;
    }
    err = max30102_register_write_byte(0x03, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 interrupt disable failed: %s", esp_err_to_name(err));
        return err;
    }

    err = max30102_register_write_byte(0x09, 0x03); // SpO2 mode: enable red and IR LEDs
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 mode config failed: %s", esp_err_to_name(err));
        return err;
    }

    // SpO2 Configuration
    err = max30102_register_write_byte(0x0a, 0x47); // SPO2_ADC_RGE = 0b10: 8192, SPO2_SR = 0b001: 100 SAMPLES PER SECOND,
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 spo2 config failed: %s", esp_err_to_name(err));
        return err;
    }
                                                               // LED_PW = 0b11: PULSE WIDTH 411, ADC RESOLUTION 18

    // LED Pulse Amplitude
    err = max30102_register_write_byte(0x0c, 0x50); // LED1_PA(red) = 0x24, LED CURRENT 16mA
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 red led config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = max30102_register_write_byte(0x0d, 0x50); // LED2_PA(IR) = 0x24, LED CURRENT 16mA
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 ir led config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = max30102_register_write_byte(0x10, 0x50); // PILOT_PA = 0x24, LED CURRENT 16mA
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 pilot led config failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t mode_config = 0;
    uint8_t red_pa = 0;
    uint8_t ir_pa = 0;
    ESP_ERROR_CHECK(max30102_register_read(0x09, &mode_config, 1));
    ESP_ERROR_CHECK(max30102_register_read(0x0c, &red_pa, 1));
    ESP_ERROR_CHECK(max30102_register_read(0x0d, &ir_pa, 1));
    ESP_LOGI(TAG, "MAX30102 sampling enabled: mode=0x%02x red_pa=0x%02x ir_pa=0x%02x", mode_config, red_pa, ir_pa);

    // clear PPG_RDY ! Cannot receive the first interrupt without clearing !
    uint8_t data;
    err = max30102_register_read(0x00, &data, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 status1 read failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Interrupt Status 1: 0x%x", data);
    err = max30102_register_read(0x01, &data, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 status2 read failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Interrupt Status 2: 0x%x", data);

    xTaskCreate(max30102_task, "max30102_task", 4096, NULL, 3, NULL);
    return ESP_OK;
}
