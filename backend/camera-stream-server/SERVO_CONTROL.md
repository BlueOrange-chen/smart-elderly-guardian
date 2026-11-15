# 摄像头舵机控制接入说明

## 数据流

```text
小程序 --设置目标角度--> 摄像头转发服务 --ESP32 定时拉取--> 舵机
小程序 <--查询实际状态-- 摄像头转发服务 <--ESP32 执行后回报-- 舵机
```

服务按 `cameraId` 分别保存每个摄像头的最新目标角度和最近一次硬件回报。当前数据保存在内存中，服务重启后目标角度恢复为 `0`，状态恢复为空。

## 接口

以下示例假设服务地址为 `https://api.example.com`，摄像头编号为 `room-1`。

### 1. 小程序设置目标角度

```http
POST /api/cameras/room-1/servo/command
X-Stream-Token: 小程序控制密钥
Content-Type: application/json

{"target_angle":45}
```

角度范围为 `-90` 到 `90`。响应示例：

```json
{"target_angle":45,"command_id":1,"updated_at":"2026-07-15T08:00:00Z"}
```

小程序调用示例：

```javascript
wx.request({
  url: `${baseUrl}/api/cameras/${cameraId}/servo/command`,
  method: 'POST',
  header: {
    'Content-Type': 'application/json',
    'X-Stream-Token': controlToken
  },
  data: { target_angle: angle }
})
```

拖动滑块时不要每一个像素都发请求，建议做 150～300ms 防抖，或者只在松手时发送。

### 2. ESP32 拉取目标角度

```http
GET /api/cameras/room-1/servo/command?token=硬件上传密钥
```

硬件现有代码只读取 `target_angle`，响应中的其他字段会被忽略，不影响兼容：

```json
{"target_angle":45,"command_id":1,"updated_at":"2026-07-15T08:00:00Z"}
```

### 3. ESP32 回报执行状态

```http
POST /api/cameras/room-1/servo/status?token=硬件上传密钥
Content-Type: application/json

{"angle":45,"reason":"moved"}
```

成功返回 `204 No Content`。`reason` 可使用 `moved`、`limit_reached` 或 `status`。

### 4. 小程序查询实际状态

```http
GET /api/cameras/room-1/servo/status
X-Stream-Token: 小程序控制密钥
```

响应示例：

```json
{"angle":45,"reason":"moved","reported_at":"2026-07-15T08:00:01Z"}
```

硬件还未回报过状态时返回 `404`，小程序应显示“等待设备上线”，不要当成舵机故障。

## 服务端配置

生产环境增加一个专门用于小程序控制的密钥：

```text
STREAM_CONTROL_TOKEN=请替换为随机长密钥
```

如果未设置，当前配置会临时沿用 `STREAM_VIEWER_TOKEN`。正式部署建议务必单独设置，硬件继续使用 `STREAM_INGEST_TOKEN`。

## 硬件代码需要调整的地方

1. **必须解决 LEDC 冲突。** 项目里的摄像头 XCLK 已使用 `LEDC_TIMER_0 + LEDC_CHANNEL_0`，而舵机代码也使用了同一组资源。舵机应至少改为：

   ```c
   #define SERVO_LEDC_TIMER LEDC_TIMER_1
   #define SERVO_LEDC_CHANNEL LEDC_CHANNEL_1
   ```

   否则舵机配置的 50Hz 会覆盖摄像头需要的 20MHz 时钟，摄像头或舵机将无法正常工作。

2. **确认 GPIO19 是否可用。** ESP32-S3 的 GPIO19 通常也是原生 USB D-。如果开发板使用原生 USB 下载或输出日志，舵机信号应换到板卡上另一个空闲、可输出 PWM 的 GPIO。最终以开发板原理图和项目全部引脚分配为准。
3. `esp_http_client_perform()` 已经执行完整 HTTP 事务，响应正文通常应通过事件回调接收。当前代码在 `perform()` 之后再调用 `esp_http_client_read_response()`，有概率读到空正文。建议改成事件回调累积响应，或使用 `open -> fetch_headers -> read -> close` 流程。
4. 拉取和回报后都应检查 `esp_http_client_get_status_code(client)`；只有 `2xx` 才解析 JSON 或认定回报成功，否则 `401/404/500` 会被误判。
5. 当前轮询间隔是 5000ms，操作会有 0～5 秒延迟。建议先改为 500ms；如果与摄像头上传抢网络资源，再调到 800～1000ms。
6. `servo_command_task` 启动后可将两个 URL 复制到任务自己的局部缓冲区并释放 `args`，避免任务参数长期占用堆内存。
7. 使用 `heap_caps_malloc/heap_caps_free` 时应显式包含 `esp_heap_caps.h`。
8. 舵机最好独立 5V 供电并与 ESP32 共地。不要直接依赖开发板 5V 引脚承受舵机启动/堵转电流，否则摄像头工作时可能导致 ESP32 复位或画面异常。

初始化调用示例：

```c
ESP_ERROR_CHECK(sg90_init());
ESP_ERROR_CHECK(sg90_start_command_task(
    "https://api.example.com/api/cameras/room-1/servo/command?token=YOUR_INGEST_TOKEN",
    "https://api.example.com/api/cameras/room-1/servo/status?token=YOUR_INGEST_TOKEN"
));
```

这段调用应放在 Wi-Fi 已成功连接之后。
