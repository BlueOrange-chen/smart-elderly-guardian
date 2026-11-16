#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化DHT11传感器
 * 
 * 该函数配置GPIO为开漏输出模式，并提供上电时间。
 */
void DHT11_Init(void);

/**
 * @brief 读取DHT11传感器的温湿度数据
 * 
 * 该函数从DHT11传感器读取温度和湿度数据，并进行校验。
 * 
 * @param[out] temperature  指针，存储读取的温度值（单位：摄氏度）。
 * @param[out] humidity     指针，存储读取的湿度值（单位：%）。
 * 
 * @return
 *   - ESP_OK: 读取成功
 *   - ESP_FAIL: 读取失败（可能是传感器未响应或校验失败）
 */
esp_err_t DHT11_Read(int *temperature, int *humidity);

#ifdef __cplusplus
}
#endif

#endif // DHT11_H