/**
 * 实验四 · 超声波测距仪 (Ultrasonic Ranger)
 * -------------------------------------------
 * HC-SR04 超声波测距，ESP32 自建热点，手机连上后打开 192.168.4.1
 * 实时查看距离（cm）；距离小于阈值时板载 LED 快闪报警——倒车雷达效果。
 * 采用滑动平均滤波，读数稳定不跳动。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#define TRIG_PIN      5
#define ECHO_PIN      18
#define LED_PIN       2
#define AP_SSID       "ESP32-Ranger"   // 手机连接这个热点（无密码）
#define ALARM_CM      20.0f            // 报警距离阈值（cm）
#define MAX_CM        400.0f           // HC-SR04 标称量程 2~400cm
#define SAMPLE_WINDOW 5                // 滑动平均窗口大小
#define MEASURE_MS    150UL            // 测量周期

WebServer server(80);

float window_[SAMPLE_WINDOW];          // window 与 Arduino 核心冲突，改名 window_
int   wIdx = 0, wCount = 0;
float g_distance = NAN;
bool  g_alarm = false;
unsigned long lastMeasureMs = 0, lastBlinkMs = 0, lastPrintMs = 0;
bool  ledOn = false;

// ---------- 内嵌网页 ----------
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>超声波测距仪</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;
background:#0f172a;color:#e2e8f0;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:28px 16px}
header{width:100%;max-width:640px;margin-bottom:18px}
h1{font-size:19px;font-weight:600;margin-bottom:4px}
.sub{color:#94a3b8;font-size:12px}.sub b{color:#38bdf8}
.card{width:100%;max-width:640px;background:#1e293b;border:1px solid #334155;border-radius:16px;
padding:36px 24px;text-align:center}
.value{font-size:88px;font-weight:700;line-height:1;font-variant-numeric:tabular-nums;
color:#38bdf8;transition:color .3s}
.unit{font-size:24px;color:#94a3b8;margin-left:8px}
.state{margin-top:18px;font-size:16px;color:#94a3b8;min-height:24px}
.warn{color:#f87171;font-size:16px;font-weight:600;margin-top:10px;display:none}
footer{margin-top:24px;font-size:12px;color:#64748b}
.g{color:#4ade80}.y{color:#fbbf24}.r{color:#f87171}.n{color:#94a3b8}
</style>
</head>
<body>
<header>
<h1>📏 超声波测距仪</h1>
<div class="sub">热点 <b>ESP32-Ranger</b> · 阈值 <span id="thr">--</span> cm 内报警 · 每 0.5 秒刷新</div>
</header>
<div class="card">
<div><span class="value" id="dist">--</span><span class="unit">cm</span></div>
<div class="state" id="state">等待数据…</div>
<div class="warn" id="warn">⚠ 距离过近！</div>
</div>
<footer>HC-SR04 · 滑动平均滤波 · 板载 LED 同步报警</footer>
<script>
const $=id=>document.getElementById(id);
function setLevel(lv){
  const s=$('state'), v=$('dist');
  v.className='value '+lv;
  if(lv==='r'){s.textContent='太近了！注意距离';s.className='state r';}
  else if(lv==='y'){s.textContent='距离偏近';s.className='state y';}
  else if(lv==='g'){s.textContent='距离安全';s.className='state g';}
  else{s.textContent='未检测到障碍物（或传感器未接）';s.className='state n';}
}
async function refresh(){
  try{
    const r=await fetch('/api/data',{cache:'no-store'});
    if(!r.ok)throw 0;
    const d=await r.json();
    $('thr').textContent=d.threshold;
    if(d.distance==null){$('dist').textContent='--';setLevel('n');}
    else{
      $('dist').textContent=d.distance.toFixed(1);
      setLevel(d.distance<d.threshold?'r':(d.distance<60?'y':'g'));
    }
    $('warn').style.display=d.alarm?'block':'none';
  }catch(e){setLevel('n');}
}
setInterval(refresh,500);
refresh();
</script>
</body>
</html>
)rawliteral";

// ---------- 测距与滤波 ----------
// 触发一次测距：返回 cm；无回波/超时/超量程返回 -1
float readOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long us = pulseIn(ECHO_PIN, HIGH, 30000UL);   // 30ms 超时 ≈ 最远 5m
  if (us == 0) return -1;
  float cm = us * 0.0343f / 2.0f;                        // 声速 343m/s，往返除 2
  if (cm < 2.0f || cm > MAX_CM) return -1;
  return cm;
}

// 有效样例压入滑动窗口，输出平均值并刷新报警状态
void pushSample(float cm) {
  window_[wIdx] = cm;
  wIdx = (wIdx + 1) % SAMPLE_WINDOW;
  if (wCount < SAMPLE_WINDOW) wCount++;
  float sum = 0;
  for (int i = 0; i < wCount; i++) sum += window_[i];
  g_distance = sum / wCount;
  g_alarm = (g_distance < ALARM_CM);
}

String buildJson() {
  String j = "{";
  j += "\"distance\":" + (isnan(g_distance) ? String("null") : String(g_distance, 1));
  j += ",\"alarm\":"    + String(g_alarm ? "true" : "false");
  j += ",\"threshold\":" + String((int)ALARM_CM);
  j += ",\"heap\":"     + String(ESP.getFreeHeap());
  j += "}";
  return j;
}

// ---------- HTTP 处理器 ----------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", buildJson());
}

// ---------- Arduino 入口 ----------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  Serial.print("[AP] 请用手机连接热点 \"");
  Serial.print(AP_SSID);
  Serial.println("\"（无密码）");
  Serial.print("[AP] 连接后浏览器打开 http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.onNotFound([]() { server.send(404, "text/plain", "404 Not Found"); });
  server.begin();
  Serial.println("[HTTP] Web 服务已启动，开始测距...");
}

void loop() {
  unsigned long now = millis();

  // 周期测量 + 滑动平均
  if (now - lastMeasureMs >= MEASURE_MS) {
    lastMeasureMs = now;
    float cm = readOnce();
    if (cm > 0) pushSample(cm);          // 无效样例不进窗口
  }

  // 报警：板载 LED 快闪
  if (g_alarm) {
    if (now - lastBlinkMs > 120) {
      lastBlinkMs = now;
      ledOn = !ledOn;
    }
  } else {
    ledOn = false;
  }
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);

  // 串口每秒打印一次，便于记录数据
  if (now - lastPrintMs > 1000) {
    lastPrintMs = now;
    if (!isnan(g_distance)) {
      Serial.printf("[测距] %.1f cm%s\n", g_distance, g_alarm ? "  ⚠ 距离过近" : "");
    }
  }

  server.handleClient();
}
