# 第三阶段：实时双向语音

该服务独立监听 `8011`，不会替换第一、二阶段的 `8010` 服务。

## 硬件协议

连接：

```http
GET wss://api.example.com/api/voice/realtime?deviceId=esp32-room-1
Upgrade: websocket
X-Device-Token: 与服务器相同的设备令牌
```

连接成功后，服务端先发送 `session.ready`，其中包含实际音频参数。

硬件上行使用 WebSocket binary frame：

- PCM signed 16-bit little-endian
- 16000 Hz
- 单声道
- 建议每帧 3200 bytes，即 100 ms
- 连续发送，不再等待整段 WAV 录完

服务端下行的 binary frame 是可立即写入 I2S 环形缓冲区的 PCM：

- PCM signed 16-bit little-endian
- 16000 Hz
- 单声道

硬件检测到本轮说话停止后发送：

```json
{"type":"input.end"}
```

服务端会回复 `{"type":"input.end.accepted"}`，并代替硬件补充一小段静音，
让服务端 VAD 及时确认本轮结束。硬件发送停止事件后不需要继续上传静音 PCM。
如果该控制帧因网络或硬件逻辑漏发，网关会在最后一帧 PCM 后约 800 ms 自动结束本轮，
此时回复中的 `source` 为 `idle_timeout`。

服务端还会发送少量 JSON 控制帧：

- `session.ready`：可以开始发送音频
- `input.end.accepted`：已收到本轮输入结束事件
- `playback.clear`：检测到老人重新说话，立即清空扬声器缓冲区并停止当前播放
- `assistant.transcript.delta`：AI 回复文本增量，仅用于调试
- `user.transcript`：老人语音识别文本，仅用于调试
- `response.done`：一轮 AI 回复结束
- `error`：本轮或连接错误

硬件可以每 15 秒发送 `{"type":"ping"}`，服务端回复 `pong`。WebSocket 自身也启用了心跳。

## 硬件实现注意事项

- 麦克风采集和 WebSocket 发送使用独立任务；不要让摄像头 HTTP 上传阻塞该任务。
- 语音连接期间给音频任务更高优先级，并限制或暂停摄像头帧上传。
- 扬声器建议使用 24～64 KiB 环形缓冲，收到第一批 PCM 后立即播放。
- 收到 `playback.clear` 必须清空尚未播放的数据，否则无法实现打断。
- 如果硬件暂时不能同时录音和播放，可在播放期间暂停上行，但会失去打断能力。
- 音频下行不再是 WAV，不应等待文件头、Content-Length 或下载完成。

## 与前两阶段的关系

- 第一阶段：`POST :8010/api/voice/chat`，完整音频上传、完整文件返回。
- 第二阶段：`POST :8010/api/voice/chat-stream`，完整音频上传、PCM 分块返回。
- 第三阶段：`WS :8011/api/voice/realtime`，PCM 双向实时传输和语音打断。

第三阶段出现问题时，硬件可直接切回第二阶段，不需要改变服务器上的旧服务。
