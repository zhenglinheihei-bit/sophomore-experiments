# 🌡️ 实验一 · 小小气象站

基于 **ESP32 + DHT22** 的迷你气象站。设备接入家庭 WiFi 后在局域网里起一个 Web 服务，手机或电脑打开网页即可实时查看温湿度，还能顺手开关板载 LED。不依赖任何云服务，数据不出家门。

## 实验目的

- 掌握 ESP32 连接 WiFi（STA 模式）与断线自动重连
- 学会读取 DHT22 数字温湿度传感器并做定时采样
- 用 `WebServer` 搭建内置网页与 JSON API
- 了解 mDNS 局域网域名（`esp32-weather.local`）

## 器材与接线

| DHT22 引脚 | 接到 ESP32 |
| --- | --- |
| VCC | 3.3V |
| DATA | GPIO4 |
| GND | GND |

> 裸传感器需在 DATA 与 VCC 之间加 10kΩ 上拉电阻；三针模块版已内置。
> LED 使用板载 LED（GPIO2），无需额外接线。

## 使用方法

1. 复制 `include/secrets.example.h` 为 `include/secrets.h`，填入 WiFi 名称和密码
2. 烧录后打开串口监视器（115200）查看 IP，或直接访问 <http://esp32-weather.local>
3. 网页每 2 秒自动刷新温湿度；也可调用 API：

```bash
curl "http://esp32-weather.local/api/data"       # 读取温湿度等数据
curl "http://esp32-weather.local/api/led?state=toggle"  # 翻转板载 LED
```

`/api/data` 返回示例：

```json
{"temperature":26.4,"humidity":58.2,"led":false,"rssi":-52,"heap":233012,"uptime":"0d 01:23:45","ip":"192.168.1.42"}
```

## 代码要点

- `src/main.cpp`：传感器每 2.5s 采样一次（DHT22 最小间隔 2s），读数失败时**保留上次有效值**
- 网页以 `PROGMEM` 内嵌在固件里，前端 `fetch` 轮询 `/api/data`
- WiFi 断开后每 10s 自动重连

## 思考题

1. 为什么 DHT22 两次读取间隔不能小于 2 秒？
2. 传感器读数失败返回 `NaN` 时，固件是怎么处理的？这样做比直接显示 0 好在哪？
3. 如果想把数据存到电脑上画曲线，你会怎么扩展？（提示：`/api/data` + 定时采集脚本）
