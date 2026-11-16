# 固件

本项目只有一个固件工程：`smart-terminal/`。它把体征采集、摄像头推流、云台随动、双向语音、
阿里云物模型上报全部跑在一块 ESP32-S3 上。

`CONTRIBUTING.md` 允许两种组织方式，本项目属于第二种：

> 如果三项能力本来就在同一份固件中，可以使用 `hardware/firmware/smart-terminal/`。

## 目录

```text
firmware/
└── smart-terminal/
    ├── CMakeLists.txt        # 工程入口（idf.py 用）
    ├── sdkconfig.defaults    # PSRAM/Flash/摄像头/网络 的默认配置
    ├── main/
    │   ├── CMakeLists.txt
    │   ├── idf_component.yml # 托管组件版本
    │   ├── config.example.h  # 配置模板（入库）
    │   ├── config.local.h    # 真实配置（不入库）
    │   ├── device_config.h   # 配置总入口 + URL 拼装
    │   └── *.c / *.h         # 各功能模块
    └── README.md             # 板卡、依赖、引脚、编译、烧录、联调
```

## 环境要求

| 项目 | 版本 | 说明 |
|---|---|---|
| ESP-IDF | **v5.2.2** | 实测版本，v5.1+ 应该也可以，未验证 |
| Python | 3.8+ | IDF 自带环境，无需额外安装 |
| CMake | 3.16+ | IDF 要求 |
| 驱动 | CP210x 或 CH343 | 视开发板 USB 转串口芯片而定 |

Windows 下建议用 **ESP-IDF PowerShell/CMD 环境**（开始菜单里的 "ESP-IDF 5.2 CMD"），
不要用系统自带 PowerShell 直接跑 `idf.py`，否则环境变量没导入。

## 编译与烧录

```bash
cd hardware/firmware/smart-terminal

# 1. 准备本机配置（只做一次）
copy main\config.example.h main\config.local.h
#   然后编辑 config.local.h：Wi-Fi、服务器地址、三个 Token、阿里云凭据

# 2. 选目标芯片（只做一次，或改 sdkconfig 后重做）
idf.py set-target esp32s3

# 3. 编译
idf.py build

# 4. 烧录 + 串口日志（把 COM3 换成你的端口）
idf.py -p COM3 flash monitor
```

常见操作：

```bash
idf.py fullclean          # 编译产物异常时清干净
idf.py menuconfig         # 改单项配置（改完记得同步 sdkconfig.defaults）
idf.py -p COM3 erase-flash  # 换开发板/分区表变化时先擦除
Ctrl + ]                  # 退出 monitor
```

## 组件策略（重要）

工程依赖 4 个托管组件：`esp_audio_codec`、`esp_websocket_client`、`esp_jpeg`、`esp32-camera`，
由 `main/idf_component.yml` 声明，首次 `idf.py build` 时自动下载到 `managed_components/`。

**原工程把 `espressif/esp32-camera` 整个仓库拷进了 `components/esp32-camera/`（约 1 MB、
50+ 文件）。** 这种做法有两个问题：

1. 团队仓库里混进第三方源码，评审时无法区分"我们改了什么"；
2. 与托管组件同名，两者同时存在会冲突。

因此本仓库改为托管方式：删除 `components/esp32-camera/`，依赖写在 `idf_component.yml`。

> 若你的本地那份 `components/esp32-camera` **被改过**（比如打了补丁），不要直接删。
> 先 `git diff` 或与原版比对，把改动以 patch 形式记在 PR 描述里，再决定是否保留 vendored 方式。
> 离线环境无法访问组件仓库时，也可以保留 vendored 副本，但必须在 README 里写明来源和版本。

## 三合一还是拆三块板

本工程是"一块板跑全部"。如果后续要拆成传感器板、摄像头板、语音板三个独立设备，
不要直接复制这个工程，而是按 `CONTRIBUTING.md` 的目录结构各建一个工程，并且：

- 每个工程只保留自己需要的 `main/*.c`；
- 三个工程共用同一份 `config.example.h` 字段命名（便于现场换板只改配置）；
- 拆板后 `deviceId` / `cameraId` 必须区分，否则后端会把多台设备的数据混在一起。

## 烧录后第一轮检查

串口日志里依次应看到：

```text
esp32 connected to ap!
esp32 get ip address
aliot mqtt connected            （DEVICE_MQTT_ENABLED=1 时）
Camera init success
camera task create ret=1
SG90 set angle +0
sensor upload status=200 body=...  或 status=201
```

任何一步没有出现，先跳到 `smart-terminal/README.md` 的"故障排查"。
