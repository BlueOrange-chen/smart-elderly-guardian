# 团队协作与提交操作指南

本文可以直接发给前端和硬件同学。三方共用一个 GitHub 仓库，通过分支和 Pull Request 协作，不要直接向 `main` 分支覆盖文件。

仓库地址：<https://github.com/BlueOrange-chen/smart-elderly-guardian>

## 第一次准备

### 1. 安装与确认

安装 Git 后，在终端执行：

```bash
git --version
git config --global user.name "你的姓名或 GitHub 用户名"
git config --global user.email "你的 GitHub 邮箱"
```

在 GitHub 网页上登录自己的账号。仓库负责人需要将同学加入仓库 Collaborators，或者让同学 Fork 后提 Pull Request。

### 2. 克隆仓库

```bash
git clone https://github.com/BlueOrange-chen/smart-elderly-guardian.git
cd smart-elderly-guardian
```

如果 HTTPS 推送要求登录，按 GitHub 提示使用浏览器授权或 Personal Access Token，不要把 Token 写进代码或文档。

## 每次开始工作

先取得主分支最新代码，再创建自己的分支：

```bash
git switch main
git pull origin main
git switch -c feature/你的功能名称
```

分支示例：

```text
feature/wechat-health-dashboard
feature/esp32-sensor-firmware
feature/camera-servo-control
fix/wechat-camera-playback
docs/hardware-pin-map
```

一个分支尽量只解决一个问题。

## 前端同学操作步骤

1. 将微信开发者工具项目放入：

   ```text
   frontend/wechat-miniprogram/
   ```

2. 推荐保留：

   ```text
   frontend/wechat-miniprogram/
   ├── miniprogram/
   ├── typings/
   ├── package.json
   ├── tsconfig.json
   ├── project.config.example.json
   └── README.md
   ```

3. 不要提交：

   ```text
   node_modules/
   miniprogram_npm/
   project.private.config.json
   微信 AppSecret
   生产 Token
   个人服务器密码
   ```

4. 如果 `project.config.json` 含个人 AppID，将 AppID 改成占位值后保存为 `project.config.example.json`；个人真实配置保留在被忽略的 `project.private.config.json`。

5. 在 `frontend/wechat-miniprogram/README.md` 写清楚：微信开发者工具版本、如何导入、页面列表、后端基础地址如何配置、当前已完成和未完成的功能。

6. 提交前运行微信开发者工具的编译和真机预览，确认 `app.json` 中声明的每个页面真实存在。

## 硬件同学操作步骤

1. 不同固件分别建立目录，不要把所有代码混在一个工程：

   ```text
   hardware/firmware/
   ├── sensor-device/
   ├── camera-upload/
   └── voice-device/
   ```

   如果三项能力本来就在同一份固件中，可以使用：

   ```text
   hardware/firmware/smart-terminal/
   ```

2. 每个固件目录至少提交：

   ```text
   README.md              # 板卡、编译、烧录、使用方法
   platformio.ini         # 使用 PlatformIO 时
   或主 .ino / CMakeLists.txt
   src/                   # 源码
   include/               # 头文件
   config.example.h       # 无密钥的配置模板
   ```

3. 不要提交：

   ```text
   .pio/
   build/
   *.bin
   *.elf
   Wi-Fi SSID/密码
   DEVICE_API_TOKEN
   生产服务器密钥
   真实用户采集数据
   ```

4. 将真实配置放在例如 `config.local.h` 中，并确保它进入 `.gitignore`；仓库只提交 `config.example.h`：

   ```c
   #define WIFI_SSID "CHANGE_ME"
   #define WIFI_PASSWORD "CHANGE_ME"
   #define SERVER_BASE_URL "https://example.com"
   #define DEVICE_API_TOKEN "CHANGE_ME"
   ```

5. 在 `hardware/protocols/` 中记录设备与后端的契约：URL、请求头、JSON 字段、音频参数、正常响应、错误响应和重试策略。协议变化要和对应后端 Pull Request 一起提交。

6. 在固件 README 记录板卡型号、传感器型号、引脚表、依赖库及版本、Arduino/ESP-IDF/PlatformIO 版本、烧录命令和最小联调步骤。别人必须能够按文档重新编译，而不是只能拿到一个 `.bin` 文件。

## 提交和推送

先查看文件，尤其确认没有密钥和大文件：

```bash
git status
git diff
git add frontend/wechat-miniprogram
# 硬件同学改为：git add hardware
git diff --cached
```

提交：

```bash
git commit -m "feat(frontend): add health monitoring pages"
# 或：git commit -m "feat(hardware): add sensor device firmware"
git push -u origin feature/你的功能名称
```

推荐提交前搜索敏感信息：

```bash
git grep -n -I -E "API_KEY|PASSWORD|TOKEN|SECRET|BEGIN.*PRIVATE KEY"
```

搜索结果包含变量名和 `CHANGE_ME` 示例是正常的；如果包含真实值，先停止提交并移到本地配置。

## 创建 Pull Request

1. 打开仓库网页；
2. 点击 `Compare & pull request`；
3. Base 选择 `main`，Compare 选择自己的分支；
4. 标题说明模块和改动，例如 `feat(frontend): 提交微信小程序健康首页`；
5. 描述中填写完成内容、测试方法、接口变化和截图；
6. 指定至少一名其他模块同学审核；
7. 根据审核意见继续向同一分支提交，Pull Request 会自动更新；
8. 审核通过、测试完成后再合并。

## 避免冲突

- 前端主要修改 `frontend/`，硬件主要修改 `hardware/`，后端主要修改 `backend/`；
- 根 README、通用协议和接口文档属于公共文件，修改前先在群里说明；
- 不要在微信或网盘反复传“最终版 2”“最新最终版”；以 GitHub `main` 和 Pull Request 为准；
- 不要使用 `git push --force` 覆盖公共分支；
- 遇到冲突时先保存自己的工作，再拉取和合并，不要直接删除别人的代码。

## 日常同步

自己的功能分支开发期间，可定期同步主分支：

```bash
git switch main
git pull origin main
git switch feature/你的功能名称
git merge main
```

解决冲突并测试后再提交。对 Git 尚不熟悉时，发生冲突先联系仓库负责人，不要强制推送。
