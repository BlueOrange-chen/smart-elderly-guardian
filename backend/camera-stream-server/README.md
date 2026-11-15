# ESP32-S3 Camera Stream Server

这是一个给 ESP32-S3 摄像头使用的流媒体网关。硬件端仍然通过 HTTP POST 上传 JPEG 帧，服务器保存最新帧，并用 FFmpeg 转成小程序 `<video>` 可以播放的 HLS。

## 当前主链路

```text
ESP32-S3 Camera
  -> HTTP POST JPEG
  -> Spring Boot 保存 latestFrame
  -> FFmpeg 编码 H.264
  -> 生成 M3U8 + TS 分片
  -> 微信小程序 <video> 播放 HLS
```

小程序播放地址：

```text
https://api.example.com/api/live/room-1.m3u8?token=观看密钥
```

## 为什么改成 HLS

个人小程序通常不能使用 `live-player` / `live-pusher`，但 `<video>` 组件可以播放 HLS。HLS 的延迟会比 WebRTC 和 FLV 更高，通常约 3-6 秒，不过它更符合小程序权限边界，也更容易稳定演示。

## 接口

### 1. ESP32 上传 JPEG

```text
POST /api/cameras/{cameraId}/frame?token=上传密钥
Content-Type: image/jpeg
Body: JPEG 二进制
```

示例：

```text
https://api.example.com/api/cameras/room-1/frame?token=上传密钥
```

成功返回：

```http
HTTP/1.1 204 No Content
Connection: close
X-Frame-Sequence: 1
X-Frame-Bytes: 12345
```

### 2. HLS 播放列表

```text
GET /api/live/{cameraId}.m3u8?token=观看密钥
```

响应：

```text
Content-Type: application/vnd.apple.mpegurl
Cache-Control: no-cache, no-store, must-revalidate
```

服务会在小程序请求 m3u8 时启动对应摄像头的 FFmpeg 转码进程。m3u8 中的 TS 分片地址会被服务器动态改写为带摄像头编号的相对路径：

```text
room-1/seg_000001.ts?token=观看密钥
```

这是因为小程序 `<video>` 不能给 m3u8/ts 请求追加自定义请求头，而且 m3u8 查询参数不会自动传给相对分片地址。

### 3. HLS TS 分片

```text
GET /api/live/{cameraId}/seg_000001.ts?token=观看密钥
```

响应：

```text
Content-Type: video/mp2t
Cache-Control: private, max-age=5
```

服务器支持简单的 HTTP Range 请求。

### 4. 单帧快照

```text
GET /api/cameras/{cameraId}/snapshot.jpg?token=观看密钥
```

快照接口可用于调试，也可作为 HLS 不可用时的小程序兜底方案。

### 5. MJPEG 调试

```text
GET /api/cameras/{cameraId}/mjpeg?token=观看密钥
```

浏览器调试用，不建议小程序长期直接使用。

### 6. 摄像头状态

```text
GET /api/cameras/{cameraId}/status?token=观看密钥
```

返回示例：

```json
{
  "cameraId": "room-1",
  "online": true,
  "lastFrameAt": "2026-07-21T08:00:00Z",
  "frameSequence": 123,
  "hlsReady": true
}
```

## 小程序配置

前端当前可以直接使用：

```js
cameraHlsUrl: 'https://api.example.com/api/live/room-1.m3u8?token=观看密钥'
```

WXML 示例：

```xml
<video
  src="{{cameraHlsUrl}}"
  autoplay
  muted
  controls="{{false}}"
  object-fit="contain"
  binderror="onVideoError"
  style="width: 100%; height: 420rpx; background: #000;"
/>
```

正式环境要注意：微信真机/正式版通常要求备案 HTTPS 域名，并配置到小程序合法域名中。开发者工具可先关闭域名校验验证链路。

## HLS 参数

默认配置：

```properties
stream.ffmpeg-path=ffmpeg
stream.hls-root-dir=${java.io.tmpdir}/esp32-camera-hls
stream.hls-fps=10
stream.hls-segment-seconds=1
stream.hls-list-size=5
stream.hls-width=640
stream.hls-video-bitrate=650k
stream.hls-maxrate=850k
stream.hls-bufsize=1300k
stream.hls-startup-timeout-ms=8000
stream.hls-inactive-timeout-ms=30000
```

ECS 上建议：

```text
STREAM_FFMPEG_PATH=/usr/bin/ffmpeg
STREAM_HLS_ROOT_DIR=/var/lib/camera-hls
STREAM_HLS_FPS=10
STREAM_HLS_SEGMENT_SECONDS=1
STREAM_HLS_LIST_SIZE=5
```

## 本地运行

需要本机已安装 FFmpeg。

```powershell
cd esp32-camera-stream-server
mvn spring-boot:run
```

## 打包

```powershell
mvn -DskipTests package
```

生成的 jar：

```text
target/esp32-camera-stream-server-0.0.1-SNAPSHOT.jar
```

## ECS 部署

服务器当前使用 systemd 服务：

```text
esp32-camera-stream.service
```

常用命令：

```bash
sudo systemctl restart esp32-camera-stream
sudo systemctl status esp32-camera-stream
curl http://127.0.0.1:8090/actuator/health
```

## ESP32 建议参数

如果上传不稳定，先用保守配置跑通：

```c
#define CAMERA_UPLOAD_INTERVAL_MS 800
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY 15
```

稳定后再逐步提高到 VGA 或更短上传间隔。
