# ESP32-S3 摄像头上传示例

这个目录里的 `camera.c` 是给硬件同学参考的稳定版上传代码。

第一阶段建议使用：

```c
#define CAMERA_UPLOAD_INTERVAL_MS 500
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_FB_COUNT 1
```

确认服务器能稳定收到，并且网页/小程序能显示画面后，再逐步改成：

```c
#define CAMERA_UPLOAD_INTERVAL_MS 300
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_FB_COUNT 2
```

注意上传地址里的 `token` 是上传密钥：

```c
#define CAMERA_UPLOAD_URL "http://服务器IP:8090/api/cameras/room-1/frame?token=上传密钥"
```

网页或小程序观看时要填观看密钥，不要填上传密钥。
