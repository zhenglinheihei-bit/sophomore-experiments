/**
 * ESP32 小小气象站 (Mini Weather Station)
 * --------------------------------------
 * 采集 DHT22 温湿度，在内置 Web 仪表盘实时展示，
 * 并提供 JSON API 与板载 LED 远程控制，纯局域网运行，无需云服务。
 *
 * 硬件: ESP32 DevKit + DHT22/AM2302（兼容 DHT11，见 DHTTYPE）
 * 框架: Arduino（通过 PlatformIO 构建）
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DHT.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #warning "未找到 include/secrets.h，正在使用占位 WiFi 配置；请复制 include/secrets.example.h 为 secrets.h 并填入真实信息"
  #define WIFI_SSID "YOUR_WIFI_SSID"
  #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

// ---------------- 引脚与常量配置 ----------------
#ifndef LED_BUILTIN
#define LED_BUILTIN 2            // 大多数 ESP32 DevKit 板载 LED 在 GPIO2
#endif
#define DHTPIN  4                // DHT 数据脚接 GPIO4
#define DHTTYPE DHT22            // 使用 DHT11 时改为 DHT11

#define HOSTNAME          "esp32-weather"   // mDNS 域名: http://esp32-weather.local
#define SENSOR_INTERVAL_MS 2500UL           // DHT22 两次采样至少间隔 2s
#define WIFI_TIMEOUT_MS    15000UL
#define WIFI_RETRY_MS      10000UL

// ---------------- 全局对象与状态 ----------------
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

static float g_temperature = NAN;   // 保留最近一次有效读数
static float g_humidity    = NAN;
static unsigned long g_lastSensorMs = 0;
static unsigned long g_lastWifiMs   = 0;
static bool g_ledState = false;

// ---------------- 内嵌网页 ----------------
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 小小气象站</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;
background:#0f172a;color:#e2e8f0;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:28px 16px}
header{width:100%;max-width:640px;display:flex;justify-content:space-between;align-items:center;margin-bottom:24px}
h1{font-size:20px;font-weight:600}
.status{display:flex;align-items:center;gap:6px;font-size:13px;color:#94a3b8}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot.ok{background:#4ade80;box-shadow:0 0 6px #4ade80}
.dot.bad{background:#f87171;box-shadow:0 0 6px #f87171}
main{display:flex;gap:16px;width:100%;max-width:640px;flex-wrap:wrap}
.card{flex:1;min-width:220px;background:#1e293b;border:1px solid #334155;border-radius:16px;padding:24px;text-align:center}
.card .label{font-size:14px;color:#94a3b8;letter-spacing:4px;margin-bottom:12px}
.card .value{font-size:56px;font-weight:700;color:#38bdf8;font-variant-numeric:tabular-nums}
.card .unit{font-size:20px;color:#94a3b8;margin-left:4px}
.panel{width:100%;max-width:640px;margin-top:16px;background:#1e293b;border:1px solid #334155;
border-radius:16px;padding:16px 20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px}
button{background:#334155;color:#e2e8f0;border:1px solid #475569;border-radius:10px;padding:10px 18px;
font-size:14px;cursor:pointer;transition:all .2s}
button:hover{background:#475569}
button.on{background:#38bdf8;border-color:#38bdf8;color:#0f172a}
.meta{display:flex;gap:16px;font-size:13px;color:#94a3b8;flex-wrap:wrap}
footer{margin-top:24px;font-size:12px;color:#64748b}
</style>
</head>
<body>
<header>
<h1>🌡️ ESP32 小小气象站</h1>
<div class="status"><span id="dot" class="dot bad"></span><span id="statusText">连接中…</span></div>
</header>
<main>
<div class="card"><div class="label">温度</div><div class="value"><span id="temp">--</span><span class="unit">°C</span></div></div>
<div class="card"><div class="label">湿度</div><div class="value"><span id="hum">--</span><span class="unit">%</span></div></div>
</main>
<section class="panel">
<button id="ledBtn">💡 LED：关</button>
<div class="meta">
<span>📶 <span id="rssi">--</span> dBm</span>
<span>⏱ <span id="uptime">--</span></span>
<span>💾 <span id="heap">--</span> KB</span>
</div>
</section>
<footer>ESP32 + DHT22 · 每 2 秒自动刷新 · 局域网直连</footer>
<script>
const $=id=>document.getElementById(id);
function setStatus(ok){$('dot').className='dot '+(ok?'ok':'bad');$('statusText').textContent=ok?'在线':'连接失败';}
function updateLed(on){const b=$('ledBtn');b.textContent='💡 LED：'+(on?'开':'关');b.classList.toggle('on',on);}
async function refresh(){
  try{
    const r=await fetch('/api/data',{cache:'no-store'});
    if(!r.ok)throw 0;
    const d=await r.json();
    $('temp').textContent=d.temperature==null?'--':d.temperature.toFixed(1);
    $('hum').textContent=d.humidity==null?'--':d.humidity.toFixed(1);
    $('rssi').textContent=d.rssi;
    $('uptime').textContent=d.uptime;
    $('heap').textContent=(d.heap/1024).toFixed(0);
    updateLed(d.led);
    setStatus(true);
  }catch(e){setStatus(false);}
}
$('ledBtn').onclick=async()=>{try{await fetch('/api/led?state=toggle');}catch(e){}refresh();};
setInterval(refresh,2000);
refresh();
</script>
</body>
</html>
)rawliteral";

// ---------------- 工具函数 ----------------
String uptimeString() {
  unsigned long s = millis() / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
           s / 86400UL, (s / 3600UL) % 24UL, (s / 60UL) % 60UL, s % 60UL);
  return String(buf);
}

void readSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) g_temperature = t;   // 读取失败时保留上次有效值
  if (!isnan(h)) g_humidity = h;
}

String buildJson() {
  String j = "{";
  j += "\"temperature\":" + (isnan(g_temperature) ? String("null") : String(g_temperature, 1));
  j += ",\"humidity\":"   + (isnan(g_humidity)    ? String("null") : String(g_humidity, 1));
  j += ",\"led\":"        + String(g_ledState ? "true" : "false");
  j += ",\"rssi\":"       + String(WiFi.RSSI());
  j += ",\"heap\":"       + String(ESP.getFreeHeap());
  j += ",\"uptime\":\""   + uptimeString() + "\"";
  j += ",\"ip\":\""       + WiFi.localIP().toString() + "\"";
  j += "}";
  return j;
}

void sendJson() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", buildJson());
}

// ---------------- HTTP 处理器 ----------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleLed() {
  String state = server.hasArg("state") ? server.arg("state") : "toggle";
  if (state == "on")       g_ledState = true;
  else if (state == "off") g_ledState = false;
  else                     g_ledState = !g_ledState;  // toggle
  digitalWrite(LED_BUILTIN, g_ledState ? HIGH : LOW);
  sendJson();
}

// ---------------- WiFi ----------------
void connectWiFi() {
  Serial.print("[WiFi] 正在连接 \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n[WiFi] 已连接，IP 地址: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] 连接超时，将在后台自动重试");
  }
}

// ---------------- Arduino 入口 ----------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 小小气象站启动 ===");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  dht.begin();

  connectWiFi();

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] 可访问 http://" HOSTNAME ".local");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, sendJson);
  server.on("/api/led", HTTP_GET, handleLed);
  server.onNotFound([]() { server.send(404, "text/plain", "404 Not Found"); });
  server.begin();
  Serial.println("[HTTP] Web 服务已启动（端口 80）");
}

void loop() {
  // 周期性采样传感器
  if (millis() - g_lastSensorMs >= SENSOR_INTERVAL_MS) {
    g_lastSensorMs = millis();
    readSensor();
  }

  // WiFi 断线自动重连
  if (millis() - g_lastWifiMs >= WIFI_RETRY_MS) {
    g_lastWifiMs = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] 连接断开，正在重连...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }

  server.handleClient();
}
