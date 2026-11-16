#include <stdio.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/semphr.h"
#include "mqtt_aliot.h"
#include "driver/gpio.h"
#include "freertos/event_groups.h"
#include "aliot_dm.h"
#include "dht11.h"
#include "max30102.h"
#include "camera.h"
#include "servo.h"
#include "voice_input.h"
#include "voice_stream.h"
#include "voice_output.h"
#include "voice_stream.h"
#include "rgb_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

#define TAG     "mqtt"

/* Wi-Fi、设备编号、服务器地址与令牌统一由 device_config.h 提供：
 *   main/config.local.h   —— 本机真实值，已在 .gitignore 中忽略
 *   main/config.example.h —— 仓库模板，值全部为 CHANGE_ME
 */
#include "device_config.h"

#define TAG_1 "HTTP_UPLOAD"
#define CAMERA_ENABLED 1

extern int latest_spo2;
extern int latest_heart_rate;

int long_im = 132;
int high_im = 162;


static SemaphoreHandle_t s_wifi_connect_sem = NULL;

void wifi_event_handle(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data)
{
    if(event_base == WIFI_EVENT)
    {
        switch (event_id)
        {  
        case WIFI_EVENT_STA_START:                  //连接WIFI
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:              //成功连接WIFI
            ESP_LOGI(TAG,"esp32 connected to ap!");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            ESP_LOGI(TAG,"esp32 disconnected to ap!");
            break;
        default:
            break;
        }
    }
    else if(event_base == IP_EVENT)
    {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP:
                ESP_LOGI(TAG,"esp32 get ip address");
                xSemaphoreGive(s_wifi_connect_sem);
            break;
        }
    }
}

void upload_data_to_api(int heart_rate, int spo2, int tempure, int humi)
{
    char post_data[256];
    snprintf(post_data, sizeof(post_data),
            "{\"deviceId\":\"%s\",\"temperature\":%d,\"humidity\":%d,\"bloodOxygen\":%d,\"heartRate\":%d}",
            SENSOR_DEVICE_ID,
            tempure,
            humi,
            spo2,
                heart_rate);


    esp_http_client_config_t config = {
        .url = SENSOR_UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    /* 健康平台开启 DEVICE_API_TOKEN 时携带；DEVICE_HEALTH_TOKEN 留空表示不鉴权 */
    if (DEVICE_HEALTH_TOKEN[0] != '\0') {
        esp_http_client_set_header(client, "X-Device-Token", DEVICE_HEALTH_TOKEN);
    }
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);

        char buffer[128] = {0};
        int data_read = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
    if (data_read > 0)
        {
            buffer[data_read] = 0;
            ESP_LOGI(TAG_1, "sensor upload status=%d body=%s", status, buffer);
        }
    else
        {
            ESP_LOGI(TAG_1, "sensor upload status=%d no response body", status);
        }
    }
    else
    {
        ESP_LOGE(TAG_1, "sensor upload failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void app_main(void)
{   
    if (rgb_led_init() == ESP_OK) {
        rgb_led_set_starting();
    }

    /*初始化WIFI*/
    ESP_ERROR_CHECK(nvs_flash_init());       //前面的是错误检查函数，后面的是NVS初始化函数
    ESP_ERROR_CHECK(esp_netif_init());       //初始化TCP IP协议线
    ESP_ERROR_CHECK(esp_event_loop_create_default());        //创建事件系统循环
 
    esp_netif_create_default_wifi_sta();         //创建STA，返回一个STA网卡对象
    wifi_init_config_t cfg= WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));            //WIFI初始化函数    

    s_wifi_connect_sem = xSemaphoreCreateBinary();
 
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handle,NULL);  //注册回调函数的接口
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event_handle,NULL); //注册网络事件端口
 
 wifi_config_t wifi_config=
 {
     .sta.threshold.authmode =WIFI_AUTH_WPA2_PSK,            //设置WIFI加密模式
     .sta.pmf_cfg.capable=true,              //是否使用保护管理帧，启用增强安全性
     .sta.pmf_cfg.required=false,            //是否只和有保护管理帧功能的设备通信
 };
 
 memset(&wifi_config.sta.ssid,0,sizeof(wifi_config.sta.ssid));
 memcpy(wifi_config.sta.ssid,DEVICE_WIFI_SSID,strlen(DEVICE_WIFI_SSID));
 
 memset(&wifi_config.sta.password,0,sizeof(wifi_config.sta.password));
 memcpy(wifi_config.sta.password,DEVICE_WIFI_PASSWORD,strlen(DEVICE_WIFI_PASSWORD));
 
 
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
 ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));
 ESP_ERROR_CHECK(esp_wifi_start());
 
 xSemaphoreTake(s_wifi_connect_sem,portMAX_DELAY);
#if DEVICE_MQTT_ENABLED
    mqtt_start();
#else
    ESP_LOGW(TAG, "Aliyun MQTT disabled (DEVICE_MQTT_ENABLED=0)");
#endif

DHT11_Init();
int num=0;
int tempure=0,humi=0;

if (voice_output_init() != ESP_OK) {
    ESP_LOGE(TAG, "Voice output init failed");
} else {
    ESP_LOGI(TAG, "Voice output ready");
}

#if VOICE_INPUT_AUTO_LISTEN
bool voice_ready = true;
if (voice_input_init() != ESP_OK) {
    ESP_LOGE(TAG, "Voice input init failed");
    voice_ready = false;
}
if (!voice_ready) {
    ESP_LOGW(TAG, "Voice tasks not started because audio init failed");
}
#if VOICE_STREAM_ENABLED
if (voice_ready) {
    if (voice_stream_init() != ESP_OK || voice_stream_start() != ESP_OK) {
        ESP_LOGW(TAG, "Voice WebSocket stream unavailable, HTTP fallback remains active");
    }
}
#endif
#else
ESP_LOGI(TAG, "Voice auto listen disabled");
#endif

if (init_camera() != ESP_OK)
{
    ESP_LOGE(TAG, "Camera init failed");
}
else
{
    ESP_LOGI(TAG, "Camera init success");
    BaseType_t ret = xTaskCreate(
        camera_upload_task,
        "camera_upload",
        8192,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "camera task create ret=%d", ret);
}

#if VOICE_INPUT_AUTO_LISTEN
if (voice_ready) {
    xTaskCreate(voice_detect_task, "voice_detect", 4096, NULL, 6, NULL);
#if VOICE_UPLOAD_ENABLED
    xTaskCreate(voice_upload_task, "voice_upload", 8192, NULL, 4, NULL);
#endif
}
#endif

if (max30102_init() != ESP_OK) {
    ESP_LOGE(TAG, "MAX30102 init failed, continue without heart sensor");
}

if (sg90_init() != ESP_OK) {
    ESP_LOGE(TAG, "SG90 init failed");
} else if (sg90_start_command_task(NULL, NULL) != ESP_OK) {
    ESP_LOGE(TAG, "SG90 command task start failed");
}

rgb_led_set_normal();

vTaskDelay(pdMS_TO_TICKS(1000));

while (1)
{
    ESP_LOGI("time","%d",num);
    num++;
    aliot_post_property_int("num",num);
    DHT11_Read(&tempure,&humi);
    aliot_post_property_int("mtemp",tempure);
    aliot_post_property_int("mhumi",humi);

    aliot_post_property_int("HeartRate",latest_heart_rate);
    aliot_post_property_int("blox",latest_spo2);

    upload_data_to_api(latest_heart_rate,latest_spo2,tempure,humi);

    vTaskDelay(pdMS_TO_TICKS(1000));
};
}


