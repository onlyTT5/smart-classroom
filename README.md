# Smart Classroom

基于 ESP-IDF 的智能课堂语音终端固件。项目以小智（Xiaozhi）AI 助手为基础，运行在 ESP32 系列开发板上，支持语音唤醒、语音对话、屏幕显示、音频播放、网络通信和 MCP 设备控制，并扩展了课堂信息播报与温湿度查询功能。

## 功能概览

- 语音唤醒、录音、语音识别和语音合成播放
- Opus 音频编解码与设备端/服务器端回声消除模式
- WebSocket 或 MQTT + UDP 通信
- LCD、OLED、触摸屏、摄像头、按键和 LED 等板级外设适配
- OTA 固件升级和 NVS 配置保存
- MCP（Model Context Protocol）设备控制
- DHT11 温湿度查询接口
- `MyInfo` 课堂信息存储与语音播报
- 支持大量 ESP32、ESP32-C3、ESP32-C6、ESP32-S3 和 ESP32-P4 开发板

> 注意：当前 `Dht11Sensor` 使用随机值模拟温湿度，不会读取真实 DHT11 GPIO。温度初始范围约为 20~30 °C，湿度初始范围约为 40%~80%。

## 项目结构

```text
main/
  audio/        音频服务、编解码器和音频处理
  boards/       各开发板的硬件适配与配置
  display/      LCD/OLED 显示实现
  led/          LED 控制实现
  protocols/    WebSocket、MQTT 等通信协议
  application.* 应用生命周期与主事件循环
  mcp_server.*  MCP 工具注册与 JSON-RPC 处理
  dht11_sensor.*虚拟温湿度模块
  myinfo.*      课堂信息与语音播报模块
docs/            通信协议和 MCP 文档
scripts/         编译、发布和资源转换脚本
```

## 环境要求

- Windows、Linux 或 macOS
- ESP-IDF `>= 5.4.0`
- Python 及 ESP-IDF 工具链
- 与所选板型匹配的 ESP32 开发板、麦克风、扬声器和 USB 数据线
- 可访问项目配置的 OTA/语音服务地址

建议先按照 Espressif 官方文档完成 ESP-IDF 安装，并在终端加载 ESP-IDF 环境。确认 `idf.py --version` 可以正常执行后再编译项目。

## 快速开始

### 1. 设置芯片目标

以默认的 ESP32-S3 面包板 WiFi 配置为例：

```bash
idf.py set-target esp32s3
```

### 2. 配置开发板和服务

```bash
idf.py menuconfig
```

在 `Xiaozhi Assistant` 菜单中选择：

- `Board Type`：选择实际使用的开发板
- `Default Language`：选择设备界面语言
- `Default OTA URL`：配置固件检查和服务器地址

项目默认板型为 `BOARD_TYPE_BREAD_COMPACT_WIFI`。不同开发板可能需要使用其 `main/boards/<board>/` 目录中的专用配置或说明文档。

### 3. 编译、烧录和查看日志

```bash
idf.py build
idf.py flash
idf.py monitor
```

也可以一次完成烧录并打开日志：

```bash
idf.py flash monitor
```

退出 monitor 使用 `Ctrl+]`。

## 发布固件

项目版本在根目录 `CMakeLists.txt` 中定义，目前为 `1.9.4`。发布脚本会根据 `main/boards` 下的 `config.json` 设置目标芯片、编译变体、合并固件，并生成 `releases/` 下的压缩包。

列出可发布的板型和变体：

```bash
python scripts/release.py --list-boards
```

编译指定板型的全部变体：

```bash
python scripts/release.py esp-hi
```

只编译指定变体：

```bash
python scripts/release.py esp-hi --name esp-hi
```

对当前已经编译的配置生成合并固件包：

```bash
python scripts/release.py
```

## MCP 工具

设备通过 WebSocket 或 MQTT 通道承载 MCP JSON-RPC 消息。后台可以按以下顺序调用：

1. `initialize` 初始化 MCP 会话
2. `tools/list` 获取当前板型可用工具
3. `tools/call` 调用具体工具

常用内置工具包括：

- `self.get_device_status`：获取设备状态
- `self.audio_speaker.set_volume`：设置音量，范围 0~100
- `self.screen.set_brightness`：设置屏幕亮度，范围 0~100（需要板型提供背光）
- `self.screen.set_theme`：切换屏幕主题
- `self.camera.take_photo`：拍照并请求视觉分析（需要摄像头）
- `self.led.led_on` / `self.led.led_off`：控制 LED
- `self.dht11.get_temperature`：查询模拟温度
- `self.dht11.get_humidity`：查询模拟湿度
- `self.dht11.get_temperature_and_humidity`：同时查询温湿度
- `self.myinfo.play_info` / `self.myinfo.play_all_info`：播报课堂信息

板级功能会在对应 `Board::InitializeTools` 中追加，因此最终工具列表取决于所选开发板。新增工具时应使用 `McpServer::AddTool` 注册，并通过 `tools/list` 验证参数定义。

## 课堂信息播报

`MyInfo` 支持通过语音命令或 MCP 触发信息播报，信息索引从 1 开始。支持的命令包括：

- `介绍温工`、`温工是谁`、`info1`
- `介绍粤嵌公司`、`粤嵌公司`、`info2`
- `介绍林院`、`林院是谁`、`info3`
- `边缘智能`、`info4`
- `所有信息`、`播报所有信息`、`allinfo`

## 通信文档

- [MCP 使用说明](docs/mcp-usage.md)
- [MCP 协议流程](docs/mcp-protocol.md)
- [WebSocket 通信协议](docs/websocket.md)
- [MQTT + UDP 通信协议](docs/mqtt-udp.md)

## 常见问题

### 编译时板型不匹配

确认 `idf.py set-target` 与开发板芯片一致，然后重新运行 `idf.py menuconfig` 选择板型。板级源码会根据 `CONFIG_BOARD_TYPE_*` 自动加入构建。

### 设备无法连接服务器

检查 Wi-Fi 配置、OTA/服务器地址、设备时间和串口日志。WebSocket 连接会使用 `Authorization`、`Protocol-Version`、`Device-Id` 和 `Client-Id` 等请求头；MQTT + UDP 模式还需要确认服务器下发的 UDP 地址和密钥有效。

### 温湿度数据与实际环境不符

这是当前实现的预期行为：`Dht11Sensor` 只生成虚拟数据。接入真实传感器时，需要替换 `main/dht11_sensor.*` 中的数据生成逻辑，并补充对应 GPIO、时序和错误处理。

## 许可证

本项目许可证见 [LICENSE](LICENSE)。