/**
 * @file device_config.h
 * @brief 配置总入口：优先读 config.local.h，缺失时回退到 config.example.h。
 *
 * 所有源文件只 include 本文件，不再直接写死任何地址、令牌或 Wi-Fi 信息。
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#if defined(__has_include)
#  if __has_include("config.local.h")
#    include "config.local.h"
#  else
#    include "config.example.h"
#    warning "未找到 config.local.h，已回退到 config.example.h（全部为 CHANGE_ME），设备无法联网"
#  endif
#else
#  include "config.example.h"
#endif

/* ================= 派生 URL（不要在业务代码里重复拼接） ================= */
#define DEVICE_SENSOR_UPLOAD_URL  DEVICE_HEALTH_BASE_URL "/sensor/save"

#define DEVICE_CAMERA_UPLOAD_URL  DEVICE_CAMERA_BASE_URL "/api/cameras/" DEVICE_CAMERA_ID \
                                  "/frame?token=" DEVICE_STREAM_INGEST_TOKEN

#define DEVICE_SERVO_COMMAND_URL  DEVICE_CAMERA_BASE_URL "/api/cameras/" DEVICE_CAMERA_ID \
                                  "/servo/command?token=" DEVICE_STREAM_INGEST_TOKEN

#define DEVICE_SERVO_STATUS_URL   DEVICE_CAMERA_BASE_URL "/api/cameras/" DEVICE_CAMERA_ID \
                                  "/servo/status?token=" DEVICE_STREAM_INGEST_TOKEN

#define DEVICE_VOICE_CHAT_URL     DEVICE_VOICE_BASE_URL "/api/voice/chat?deviceId=" DEVICE_VOICE_ID \
                                  "&sessionId=" DEVICE_VOICE_SESSION_ID

#define DEVICE_VOICE_TEST_WAV_URL DEVICE_VOICE_BASE_URL "/api/voice/test-audio.wav"

#define DEVICE_VOICE_WS_URL       DEVICE_VOICE_WS_BASE_URL "?deviceId=" DEVICE_VOICE_ID

/* ================= 兼容旧宏名（原代码里的名字保持不变） ================= */
#define SENSOR_DEVICE_ID   DEVICE_SENSOR_ID
#define SENSOR_UPLOAD_URL  DEVICE_SENSOR_UPLOAD_URL
#define CAMERA_UPLOAD_URL  DEVICE_CAMERA_UPLOAD_URL

#endif /* DEVICE_CONFIG_H */
