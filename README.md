# 🧪 大二实验 —— ESP32 嵌入式实验集

[![CI](https://github.com/zhenglinheihei-bit/sophomore-experiments/actions/workflows/ci.yml/badge.svg)](https://github.com/zhenglinheihei-bit/sophomore-experiments/actions/workflows/ci.yml)

一套基于 **ESP32 (Arduino 框架)** 的嵌入式小实验。每个子文件夹是一个**独立可烧录**的 PlatformIO 工程，由浅入深：从点灯、触摸，到 WiFi、Web 服务、传感器。

每次推送到 GitHub，CI 会把**所有实验全部在线编译一遍**，任何实验代码写坏了都会立刻被发现。

## 📋 实验一览

| 编号 | 实验 | 额外硬件 | 你会学到 | 难度 |
| --- | --- | --- | --- | --- |
| 实验一 | [小小气象站](01-weather-station/) | DHT22 温湿度传感器 | WiFi STA 联网、WebServer、传感器定时采样、mDNS、JSON API | ⭐⭐ |
| 实验二 | [触摸呼吸灯](02-touch-breathing-lamp/) | 无（杜邦线可选） | LEDC PWM 调光、电容触摸、自动校准、非阻塞状态机 | ⭐ |
| 实验三 | [WiFi 信号扫描仪](03-wifi-scanner/) | 无 | SoftAP 热点模式、异步扫描、RSSI 信号强度、动态网页 | ⭐⭐ |
| 实验四 | [超声波测距仪](04-ultrasonic-ranger/) | HC-SR04 超声波模块 | pulseIn 测脉宽、滑动平均滤波、阈值报警逻辑 | ⭐⭐ |

> 实验二、三 **不需要任何外部硬件**，一块 ESP32 开发板就能跑。
> 实验三、四是「手机直连 ESP32 热点」模式，**无需 WiFi 账号密码**，随时随地演示。

## 📁 目录结构

```text
sophomore-experiments/
├── 01-weather-station/       # 实验一：小小气象站
├── 02-touch-breathing-lamp/  # 实验二：触摸呼吸灯
├── 03-wifi-scanner/          # 实验三：WiFi 信号扫描仪
├── 04-ultrasonic-ranger/     # 实验四：超声波测距仪
├── .github/workflows/ci.yml  # CI：矩阵编译全部实验
├── .gitignore
└── README.md
```

## 🚀 环境准备与烧录

1. 安装 [VS Code](https://code.visualstudio.com/) 和 [PlatformIO IDE 插件](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
2. 用 VS Code **打开某个实验的子文件夹**（如 `02-touch-breathing-lamp`）
3. 开发板插上 USB，点底部状态栏 **→ (Upload)** 烧录，再点插头图标看串口输出（115200）

命令行方式（在仓库根目录）：

```bash
# 编译某个实验
pio run -d 02-touch-breathing-lamp
# 编译并烧录某个实验
pio run -d 02-touch-breathing-lamp -t upload
# 看串口
pio device monitor -d 02-touch-breathing-lamp
```

> GitHub 仓库名不支持中文，故仓库名用拼音含义的 `sophomore-experiments`，本地文件夹同名；「大二实验」见本 README 标题与仓库简介。本地操作 PlatformIO 时请避免把工程放在中文路径下，个别工具链版本会报编码错误。

## ✍️ 写实验报告的小建议

- 每个实验的 README 末尾都有 **思考题**，可以直接作为报告的分析题
- 串口窗口（115200）和手机网页的现象都**截图**存进报告
- 实验二的触摸阈值、实验三的 RSSI、实验四的测距数值都可以改参数做**对比数据表**

## 🔭 进阶方向（待扩展）

- 📟 接 OLED 屏显示（I2C）
- 🔋 深度睡眠 + 电池供电（电流测量）
- 📡 BLE 蓝牙广播 / 手机 App 控制
- ☁️ MQTT 接入 Home Assistant 家庭自动化
