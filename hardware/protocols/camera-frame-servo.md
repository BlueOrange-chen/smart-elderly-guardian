# 摄像头帧上传与云台控制协议

设备 ↔ 摄像头服务（`backend/camera-stream-server`）。

固件实现：`camera.c`（帧上传）、`servo.c`（云台）。后端实现：`CameraStreamController.java`、
`ServoControlController.java`，设计说明见 `backend/camera-stream-server/SERVO_CONTROL.md`。

## 1. 数据流

```text
ESP32-S3 ──POST JPEG──> 摄像头服务（保存 latestFrame）
                              │
                              ├─FFmpeg──> M3U8 + TS ──> 小程序 <video> 播放 HLS
                              │
小程序 ──POST servo/command──> 服务（保存 target_angle）
ESP32  ──GET  servo/command──> 服务（每 5 s 轮询）
ESP32  ──POST servo/status───> 服务（回报实际角度）
小程序 ──GET  servo/status───> 服务（显示"等待设备上线"或实际角度）
```

## 2. 帧上传

```http
POST {CAMERA_BASE_URL}/api/cameras/{cameraId}/frame?token={STREAM_INGEST_TOKEN}
Content-Type: image/jpeg
Connection: close

<JPEG 二进制>
```

令牌也可以放头部：`X-Stream-Token: {STREAM_INGEST_TOKEN}`。**固件当前用 query 参数。**

成功响应 `204 No Content`：

```http
HTTP/1.1 204 No Content
Connection: close
X-Frame-Sequence: 1287
X-Frame-Bytes: 41233
```

| 响应 | 含义 | 设备动作 |
|---|---|---|
| `204` | 服务端已收下这一帧 | 继续下一帧 |
| `400` | `cameraId` 非法，或请求体不是完整 JPEG | 检查 JPEG 首尾标记 `FFD8 ... FFD9` |
| `401` / `403` | 上传令牌错误 | **停止重试**，核对 `DEVICE_STREAM_INGEST_TOKEN` |
| 无响应 | 网络中断 | 退避 2 s 后重试 |

设备侧要求（当前实现已满足）：

- 发送前校验 `fb->buf[0]==0xFF && fb->buf[1]==0xD8` 且结尾 `0xFF 0xD9`，否则丢弃该帧；
- `Connection: close`，不复用长连接（避免服务端半关闭导致假成功）；
- 失败后等待 2 s，不连续重传同一帧。

## 3. 画质与帧率

| 场景 | 分辨率 | JPEG 质量 | 间隔 | 单帧大小 | 带宽 |
|---|---|---|---|---|---|
| 初次联调 | QVGA (320×240) | 18 | 500 ms | 10–20 KB | 20–40 KB/s |
| 演示（当前） | VGA (640×480) | 12 | 500 ms | 30–60 KB | 60–120 KB/s |
| 弱网 | QQVGA (160×120) | 22 | 500 ms | 5–8 KB | 10–16 KB/s |

HLS 转码端默认 640×480 / 10 fps / 650 kbps，所以**上传端超过约 8 fps 没有意义**，只会浪费带宽。

## 4. 云台：拉取目标角度

```http
GET {CAMERA_BASE_URL}/api/cameras/{cameraId}/servo/command?token={STREAM_INGEST_TOKEN}
```

响应：

```json
{"target_angle":45,"command_id":1,"updated_at":"2026-07-15T08:00:00Z"}
```

设备只需要 `target_angle`（整数，后端允许 −90…+90）。固件另兼容 `targetAngle`、`angle` 以及嵌套在
`data` 里的写法，属于防御性解析，不是协议要求。

## 5. 云台：回报执行状态

```http
POST {CAMERA_BASE_URL}/api/cameras/{cameraId}/servo/status?token={STREAM_INGEST_TOKEN}
Content-Type: application/json

{"angle":45,"reason":"moved"}
```

成功 `204 No Content`。

| `reason` | 何时发送 |
|---|---|
| `moved` | 角度变化并执行成功 |
| `limit_reached` | 目标超出机械/软件限位，被夹到边界 |
| `status` | 心跳式回报（当前固件未使用） |

## 6. 云台限位不一致（重要）

| 侧 | 允许范围 |
|---|---|
| 后端接口 | −90 … +90 |
| 固件机械限位 | **−40 … +40**（`servo.c` 的 `s_min_angle` / `s_max_angle`） |

所以小程序拖到 80° 时，后端记录 `target_angle=80`，设备会停在 40° 并回报 `limit_reached`。
这是**预期行为**，不是 bug。若要放宽：

1. 确认支架机械行程确实支持 ±90°；
2. 改 `servo.c` 里的两个限值；
3. 同步更新本节与 `PIN_MAP.md`；
4. 真机验证堵转电流不超过供电能力（SG90 堵转约 700 mA）。

## 7. 轮询节奏

当前 `servo_command_task` 每 **5000 ms** 拉取一次，因此小程序操作到实际转动有 0–5 s 延迟。

后端 `SERVO_CONTROL.md` 建议改为 **500 ms**（若与摄像头上传抢带宽，再退到 800–1000 ms）。

改动位置：`servo.c` 末尾 `vTaskDelay(pdMS_TO_TICKS(5000))`。改小后注意：

- 每次请求约 200 B，500 ms 一次约 400 B/s，可忽略；
- 但 TLS 握手开销大，若改用 HTTPS 建议保持 ≥ 1 s，或复用连接。

## 8. 观看端接口（不属于设备侧，供联调）

| 接口 | 用途 |
|---|---|
| `GET /api/cameras/{id}/snapshot.jpg?token={观看令牌}` | 单帧快照，最适合验证"上传是否通" |
| `GET /api/cameras/{id}/status?token={观看令牌}` | `online`、`frameSequence`、`hlsReady` |
| `GET /api/live/{id}.m3u8?token={观看令牌}` | 小程序播放地址 |
| `GET /api/cameras/{id}/mjpeg?token={观看令牌}` | 浏览器调试用 MJPEG |

观看令牌与上传令牌**不是同一个**，填错会 401。

## 9. 联调命令

```bash
# 上传一帧测试图（服务端会做 JPEG 校验，随便一个文件会被 400 拒绝）
curl -i -X POST "http://127.0.0.1:8090/api/cameras/room-1/frame?token=CHANGE_ME" \
  -H "Content-Type: image/jpeg" --data-binary @test.jpg

# 看状态
curl "http://127.0.0.1:8090/api/cameras/room-1/status?token=CHANGE_ME"

# 模拟小程序设置 30°
curl -i -X POST "http://127.0.0.1:8090/api/cameras/room-1/servo/command" \
  -H "Content-Type: application/json" -H "X-Stream-Token: CHANGE_ME" \
  -d '{"target_angle":30}'

# 设备侧会看到的响应
curl "http://127.0.0.1:8090/api/cameras/room-1/servo/command?token=CHANGE_ME"
```
