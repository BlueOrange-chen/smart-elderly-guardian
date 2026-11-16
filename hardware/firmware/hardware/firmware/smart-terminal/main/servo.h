#ifndef SERVO_H
#define SERVO_H

#include <esp_err.h>
#include "device_config.h"

/* 云台指令/状态地址与摄像头帧上传共用同一个 STREAM_INGEST 令牌 */
#define SERVO_COMMAND_FETCH_URL DEVICE_SERVO_COMMAND_URL
#define SERVO_STATUS_REPORT_URL DEVICE_SERVO_STATUS_URL

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the SG90 PWM driver.
 * This configures a free GPIO pin and LEDC channel for servo control.
 */
esp_err_t sg90_init(void);

/**
 * Set servo angle in degrees, clamped to [-90, 90].
 * -90 is left, 0 is center, 90 is right.
 */
esp_err_t sg90_set_angle(int angle);

/**
 * Start the background command polling task.
 * @param fetch_url URL used to pull movement commands from the server.
 * @param report_url URL used to report limit-reached or movement status.
 */
esp_err_t sg90_start_command_task(const char *fetch_url, const char *report_url);

#ifdef __cplusplus
}
#endif

#endif // SERVO_H
