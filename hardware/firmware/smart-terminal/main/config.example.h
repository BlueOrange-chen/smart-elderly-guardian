/**
 * @file config.example.h
 * @brief 设备配置模板 —— 仓库里只有这个文件，真实值写入 config.local.h（不入库）。
 *
 * 用法：
 *   1. cp main/config.example.h main/config.local.h
 *   2. 把 CHANGE_ME 换成真实值
 *   3. 不要提交 config.local.h（hardware/.gitignore 已忽略）
 *
 * 注意：
 *   - 任何以 http:// 开头的地址仅用于局域网联调。生产必须用 HTTPS/WSS，
 *     并让服务器证书能在设备端的 CA bundle 中校验通过。
 *   - Token 一旦进入过 Git 历史，删除文件是不够的，必须去服务端废止并重发。
 */

#ifndef DEVICE_CONFIG_EXAMPLE_H
#define DEVICE_CONFIG_EXAMPLE_H

/* ==================================================================
 * 1. Wi-Fi
 * ================================================================== */
#define DEVICE_WIFI_SSID        "CHANGE_ME"
#define DEVICE_WIFI_PASSWORD    "CHANGE_ME"

/* ==================================================================
 * 2. 设备身份
 *
 * 后端三套服务用字符串匹配设备，命名必须与部署时一致：
 *   - 健康平台：deviceId（写进 sensor_data.device_id）
 *   - 摄像头服务：cameraId（出现在 URL 路径里：/api/cameras/{cameraId}/...）
 *   - 语音服务：deviceId（WebSocket 与 HTTP 都要用）
 * 建议全部使用同一个短标识，例如 room-1，便于排障。
 * ================================================================== */
#define DEVICE_SENSOR_ID        "CHANGE_ME"
#define DEVICE_CAMERA_ID        "CHANGE_ME"
#define DEVICE_VOICE_ID         "CHANGE_ME"
/* 语音会话编号：同一 deviceId 的不同会话要换 ID，后端按 sessionId 保存上下文 */
#define DEVICE_VOICE_SESSION_ID "CHANGE_ME"

/* ==================================================================
 * 3. 服务器地址（只写协议 + 主机 + 端口，不要写路径）
 * ================================================================== */
/* 健康平台：POST /sensor/save（旧接口）或 /api/upload-data */
#define DEVICE_HEALTH_BASE_URL  "http://CHANGE_ME:8000"
/* 摄像头 / 云台服务：8090 */
#define DEVICE_CAMERA_BASE_URL  "http://CHANGE_ME:8090"
/* 语音服务 HTTP：8010 */
#define DEVICE_VOICE_BASE_URL   "http://CHANGE_ME:8010"
/* 语音 WebSocket 基址：8011（opus_gateway 或 realtime_gateway） */
#define DEVICE_VOICE_WS_BASE_URL "ws://CHANGE_ME:8011/api/voice/stream"

/* ==================================================================
 * 4. 设备令牌
 *
 * 三个位置各自独立，不要复用同一个值：
 *   - STREAM_INGEST：摄像头帧上传 + 云台指令拉取/状态回报（后端 STREAM_INGEST_TOKEN）
 *   - VOICE_DEVICE ：语音 HTTP 与 WebSocket 握手（后端 DEVICE_API_TOKEN）
 *   - HEALTH       ：健康平台若开启 DEVICE_API_TOKEN，作为 X-Device-Token 发送
 * ================================================================== */
#define DEVICE_STREAM_INGEST_TOKEN "CHANGE_ME"
#define DEVICE_VOICE_TOKEN         "CHANGE_ME"
/* 留空字符串表示后端未开启健康接口鉴权 */
#define DEVICE_HEALTH_TOKEN        ""

/* ==================================================================
 * 5. 阿里云 IoT 物模型（可选，置 0 则完全不连 MQTT）
 *
 * 关闭后设备只走自建后端（健康平台/摄像头/语音），功能不受影响。
 * ================================================================== */
#define DEVICE_MQTT_ENABLED        1
#define DEVICE_MQTT_PRODUCT_KEY    "CHANGE_ME"
#define DEVICE_MQTT_DEVICE_NAME    "CHANGE_ME"
#define DEVICE_MQTT_DEVICE_SECRET  "CHANGE_ME"
#define DEVICE_MQTT_REGION         "cn-shanghai"

/* ==================================================================
 * 6. 功能开关
 * ================================================================== */
/* 语音自动监听（VAD 触发） */
#define DEVICE_VOICE_AUTO_LISTEN   1
/* 整段 WAV 上传到 /api/voice/chat */
#define DEVICE_VOICE_HTTP_UPLOAD   1
/* Opus WebSocket 流式链路；0 = 只走 HTTP 回退（当前默认，稳定优先） */
#define DEVICE_VOICE_STREAM        0

/* ==================================================================
 * 7. 摄像头参数
 *
 * 联调顺序：QVGA/500ms → 稳定后 → VGA/300~500ms。
 * VGA + quality 12 单帧约 30~60 KB，500ms 一帧约 60~120 KB/s。
 * ================================================================== */
#define DEVICE_CAMERA_INTERVAL_MS  500
#define DEVICE_CAMERA_JPEG_QUALITY 12
#define DEVICE_CAMERA_FB_COUNT     2

#endif /* DEVICE_CONFIG_EXAMPLE_H */
