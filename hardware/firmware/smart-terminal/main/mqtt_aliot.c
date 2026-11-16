#include "mqtt_aliot.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mbedtls/md5.h"
#include "mbedtls/md.h"
#include "esp_wifi.h"
#include <stdio.h>
#include "aliot_dm.h"

#define TAG "aliot"

static esp_mqtt_client_handle_t   mqtt_handle = NULL;
static char s_is_mqtt_connect = 0;

extern const char* g_aliot_ca;

char * get_clientid(void)
{
    uint8_t mac[6];
    static char client_id[32]={0};
    esp_wifi_get_mac(WIFI_IF_STA,mac);//获取MAC地址
    snprintf(client_id,sizeof(client_id),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return client_id;
}

void  cal_md5(char* key,char* content,unsigned char* output)         //哈希值计算
{
    mbedtls_md_context_t   md5_ctx;
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);

    mbedtls_md_init(&md5_ctx);
    mbedtls_md_setup(&md5_ctx,md_info,1);   //算法摘要结构体，算法指针，是否使用hmac算法
    mbedtls_md_hmac_starts(&md5_ctx,(const unsigned char*)key,strlen(key));       //设置密钥
    mbedtls_md_hmac_update(&md5_ctx,(const unsigned char*)content,strlen(content));       //设置要计算的内容为content
    mbedtls_md_hmac_finish(&md5_ctx,output);
    mbedtls_md_free(&md5_ctx);

}


void hex2str(uint8_t*input,uint32_t input_len,char *output)      //hex转换成str
{
    char * upper= "0123456789ABCDEF";       //字符表
    int j=0;
    for(int i=0;i<input_len;i++)
    {
        output[j++] = upper[(input[i]>>4)&0xF];
        output[j++] = upper[input[i]&0xF];
    }
    output[j]=0;
}


void mqtt_event_callback(void* event_handler_arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data)
{
    (void)event_data;
    switch (event_id)           //不同事件的处理
    {
    case MQTT_EVENT_CONNECTED:              //连接MQTT成功
        // ESP_LOGI(TAG,"aliot mqtt connected");   
        s_is_mqtt_connect = 1;                   
        break;
    case MQTT_EVENT_DISCONNECTED:           //未连接MQTT
        // ESP_LOGI(TAG,"mqtt disconnected");
        s_is_mqtt_connect = 0;
        break;
    case MQTT_EVENT_PUBLISHED:              //发布MQTT信息
        // ESP_LOGI(TAG,"mqtt published ack");
        break;
    case MQTT_EVENT_SUBSCRIBED:             //订阅MQTT信息
        // ESP_LOGI(TAG,"mqtt subscribed ack");
        break;
    case MQTT_EVENT_DATA:                   //接收事件数据
        // ESP_LOGI(TAG,"topic ->%.*s",data->topic_len,data->topic);
        // ESP_LOGI(TAG,"payload ->%.*s",data->data_len,data->data);
        break;
    default:
        break;
    }
}

//启动阿里云iot连接

void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {0};        //填充config结构体
    mqtt_cfg.broker.address.uri = ALIOT_MQTT_URL;              //MQTT域名
    mqtt_cfg.broker.address.port = 8883;                    //端口 自签名证书用8883
    //client_id
    char client_id[128];
    snprintf(client_id,sizeof(client_id),"%s|securemode=2,signmethod=hmacmd5|",get_clientid());//格式化字符串
    mqtt_cfg.credentials.client_id = client_id;         //客户端ID

    //username
    char username[128];
    snprintf(username,sizeof(username),"%s&%s",ALIOT_DEVICENAME,ALIOT_PRODUCTKEY);
    mqtt_cfg.credentials.username = username;          //用户名

    //password
    char sign_content[256];
    snprintf(sign_content,sizeof(sign_content),"clientId%sdeviceName%sproductKey%s",get_clientid(),ALIOT_DEVICENAME,ALIOT_PRODUCTKEY);
    unsigned char password_hex[16];
    char password_str[33];
    cal_md5(ALIOT_DEVICESECRET,sign_content,password_hex);      //得到哈希值
    hex2str(password_hex,16,password_str);
    mqtt_cfg.credentials.authentication.password = password_str;   //密码

    //TLS证书
    mqtt_cfg.broker.verification.certificate = g_aliot_ca;

    //发起MQTT连接
    mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);           //初始化函数

    esp_mqtt_client_register_event(mqtt_handle,ESP_EVENT_ANY_ID,mqtt_event_callback,NULL);    //响应回调函数
    esp_mqtt_client_start(mqtt_handle);
}

//上报一个整型数据
void aliot_post_property_int(const char* name,int value)
{
    /*开始JSON生成*/
    //生成一个物模型描述结构体
    ALIOT_DM_DES *dm = aliot_malloc_des();
    //往物模型描述结构体添加属性值
    aliot_set_dm_int(dm,name,value);
    //生成CJSON字符串，保存在dm_js_str
    aliot_dm_serialize(dm);
    /*JSON生成完成*/

    char topic[128];
    snprintf(topic,sizeof(topic),"/sys/%s/%s/thing/event/property/post",ALIOT_PRODUCTKEY,ALIOT_DEVICENAME);
    esp_mqtt_client_publish(mqtt_handle,topic,dm->dm_js_str,strlen(dm->dm_js_str),1,0);
    //释放物模型描述结构体
    aliot_dm_free(dm);
}

//是否已经连接阿里云IOT
char is_mqtt_connected(void)
{

    return s_is_mqtt_connect;
}


const char* g_aliot_ca = "-----BEGIN CERTIFICATE-----\n"
"MIID3zCCAsegAwIBAgISfiX6mTa5RMUTGSC3rQhnestIMA0GCSqGSIb3DQEBCwUA"
"MHcxCzAJBgNVBAYTAkNOMREwDwYDVQQIDAhaaGVqaWFuZzERMA8GA1UEBwwISGFu"
"Z3pob3UxEzARBgNVBAoMCkFsaXl1biBJb1QxEDAOBgNVBAsMB1Jvb3QgQ0ExGzAZ"
"BgNVBAMMEkFsaXl1biBJb1QgUm9vdCBDQTAgFw0yMzA3MDQwNjM2NThaGA8yMDUz"
"MDcwNDA2MzY1OFowdzELMAkGA1UEBhMCQ04xETAPBgNVBAgMCFpoZWppYW5nMREw"
"DwYDVQQHDAhIYW5nemhvdTETMBEGA1UECgwKQWxpeXVuIElvVDEQMA4GA1UECwwH"
"Um9vdCBDQTEbMBkGA1UEAwwSQWxpeXVuIElvVCBSb290IENBMIIBIjANBgkqhkiG"
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAoK//6vc2oXhnvJD7BVhj6grj7PMlN2N4iNH4"
"GBmLmMdkF1z9eQLjksYc4Zid/FX67ypWFtdycOei5ec0X00m53Gvy4zLGBo2uKgi"
"T9IxMudmt95bORZbaph4VK82gPNU4ewbiI1q2loRZEHRdyPORTPpvNLHu8DrYBnY"
"Vg5feEYLLyhxg5M1UTrT/30RggHpaa0BYIPxwsKyylQ1OskOsyZQeOyPe8t8r2D4"
"RBpUGc5ix4j537HYTKSyK3Hv57R7w1NzKtXoOioDOm+YySsz9sTLFajZkUcQci4X"
"aedyEeguDLAIUKiYicJhRCZWljVlZActorTgjCY4zRajodThrQIDAQABo2MwYTAO"
"BgNVHQ8BAf8EBAMCAQYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUkWHoKi2h"
"DlS1/rYpcT/Ue+aKhP8wHwYDVR0jBBgwFoAUkWHoKi2hDlS1/rYpcT/Ue+aKhP8w"
"DQYJKoZIhvcNAQELBQADggEBADrrLcBY7gDXN8/0KHvPbGwMrEAJcnF9z4MBxRvt"
"rEoRxhlvRZzPi7w/868xbipwwnksZsn0QNIiAZ6XzbwvIFG01ONJET+OzDy6ZqUb"
"YmJI09EOe9/Hst8Fac2D14Oyw0+6KTqZW7WWrP2TAgv8/Uox2S05pCWNfJpRZxOv"
"Lr4DZmnXBJCMNMY/X7xpcjylq+uCj118PBobfH9Oo+iAJ4YyjOLmX3bflKIn1Oat"
"vdJBtXCj3phpfuf56VwKxoxEVR818GqPAHnz9oVvye4sQqBp/2ynrKFxZKUaJtk0"
"7UeVbtecwnQTrlcpWM7ACQC0OO0M9+uNjpKIbksv1s11xu0="
"-----END CERTIFICATE-----";
