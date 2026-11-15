# 硬件对接协议：WebSocket + Opus 双向流式语音

## 1. 连接

```text
wss://api.example.com/api/voice/stream?deviceId={设备唯一编号}
```

WebSocket 握手请求头：

```http
X-Device-Token: 与当前 HTTP 语音接口相同的设备令牌
```

握手成功的 HTTP 状态码必须是 `101 Switching Protocols`。

## 2. 首个消息

WebSocket 建立后，设备必须在 5 秒内发送首个 JSON 文本帧：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration_ms": 20
  }
}
```

服务端接受后返回：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "session_id": "服务端生成的会话ID",
  "device_id": "设备编号",
  "input_audio": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration_ms": 20
  },
  "output_audio": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration_ms": 20,
    "bitrate": 20000
  }
}
```

## 3. 麦克风上行

一轮录音开始时发送 JSON 文本帧：

```json
{"type":"listen","state":"start"}
```

收到下面确认后开始发送音频：

```json
{"type":"listen","state":"started"}
```

随后每 20 ms 发送一个 WebSocket binary frame。每个 binary frame 必须是一个完整、独立的
Opus packet，不添加长度头、序号、时间戳或自定义二进制头。

编码参数：

```text
输入 PCM：16000 Hz / signed 16-bit little-endian / mono
Opus application：VOIP
Opus frame duration：20 ms
每帧 PCM samples：320
每帧原始 PCM bytes：640
推荐 bitrate：20000 bit/s
VBR：开启
FEC：第一版关闭
DTX：第一版关闭
```

VAD 判断本轮结束后发送：

```json
{"type":"listen","state":"stop"}
```

兼容的结束消息：

```json
{"type":"input.end"}
```

二者只发送一个。服务端返回：

```json
{"type":"listen","state":"stopped","source":"device","inputBytes":本轮解码后的PCM字节数}
```

## 4. AI 语音下行

服务端准备下发 AI 语音时先发送：

```json
{
  "type": "tts",
  "state": "start",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration_ms": 20
  }
}
```

接下来的 WebSocket binary frame 均为完整的 Opus packet。设备每收到一帧就放入解码队列，
解码为 16 kHz / 16-bit / mono PCM 后写入 I2S 播放环形缓冲区，不等待全部响应完成。

服务端发送完毕后返回：

```json
{"type":"tts","state":"stop"}
```

随后返回：

```json
{"type":"response.done"}
```

## 5. 其他控制消息

设备心跳：

```json
{"type":"ping"}
```

服务端返回：

```json
{"type":"pong","timestamp":毫秒时间戳}
```

设备收到下面消息时，应立即停止当前播放并清空 Opus 接收队列和 PCM/I2S 环形缓冲区：

```json
{"type":"playback.clear"}
```

调试消息：

```json
{"type":"stt","text":"识别到的老人语音","final":true}
{"type":"tts.text.delta","text":"AI回复文字增量"}
```

错误消息：

```json
{"type":"error","code":"错误码","message":"可选说明"}
```

收到 `error` 后不得把 JSON 当音频播放。

## 6. 硬件队列要求

- 按 WebSocket opcode 区分 Text JSON 和 Binary Opus。
- 建议 Opus 接收队列容纳 20～40 帧，即 400～800 ms。
- 收到首个下行 Opus packet 后即可启动播放，不等待 `tts.stop`。
- 播放任务、麦克风采集任务和 WebSocket 收发任务相互独立。
- 语音会话期间降低摄像头上传优先级，避免摄像头 HTTP 请求阻塞语音帧。
- WebSocket 断开后采用 1、2、4、8、15 秒退避重连，重连后必须重新发送 `hello`。
- 当前不要求全双工回声消除；AI 播放期间可以暂停麦克风上行。

## 7. HTTP 回退

原第二阶段接口继续保留：

```text
POST https://api.example.com/api/voice/chat-stream
```

WebSocket/Opus 联调失败时，固件可以临时切回该接口。
