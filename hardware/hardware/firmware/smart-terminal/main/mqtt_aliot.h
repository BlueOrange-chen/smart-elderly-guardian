#ifndef _MQTT_ALIOT_H_
#define _MQTT_ALIOT_H_


#include "device_config.h"

/* 产品/设备凭据来自 device_config.h，仓库中不出现真实值 */
#define ALIOT_DEVICENAME    DEVICE_MQTT_DEVICE_NAME

//产品密钥
#define ALIOT_PRODUCTKEY    DEVICE_MQTT_PRODUCT_KEY

//设备密钥
#define ALIOT_DEVICESECRET  DEVICE_MQTT_DEVICE_SECRET

//MQTT域名
#define ALIOT_MQTT_URL  "mqtts://"ALIOT_PRODUCTKEY".iot-as-mqtt."DEVICE_MQTT_REGION".aliyuncs.com"

//启动阿里云IOT连接
void mqtt_start(void);

//上报一个整型数据
void aliot_post_property_int(const char* name,int value);

//是否已经连接阿里云IOT
char is_mqtt_connect(void);

#endif
