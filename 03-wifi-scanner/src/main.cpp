/**
 * 实验三 · WiFi 信号扫描仪 (WiFi Scanner)
 * -------------------------------------------
 * ESP32 自建开放热点 ESP32-Scanner，手机连上后访问 192.168.4.1，
 * 即可看到周围 WiFi 列表、信号强度条、信道与加密情况，每 10 秒自动重扫。
 * 串口同步打印扫描结果，方便截图写实验报告。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#define AP_SSID      "ESP32-Scanner"   // 手机连接这个热点（无密码）
#define MAX_AP       30                // 最多展示的网络数
#define AUTO_SCAN_MS 10000UL

WebServer server(80);

bool scanning = false;
String jsonCache = "[]";
unsigned long lastAutoMs = 0;

// ---------- 内嵌网页 ----------
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>WiFi 信号扫描仪</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;
background:#0f172a;color:#e2e8f0;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:24px 14px}
header{width:100%;max-width:680px;display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}
h1{font-size:19px;font-weight:600}
.sub{width:100%;max-width:680px;color:#94a3b8;font-size:12px;margin-bottom:14px}
.sub b{color:#38bdf8}
button{background:#334155;color:#e2e8f0;border:1px solid #475569;border-radius:10px;
padding:9px 16px;font-size:14px;cursor:pointer}
button:hover{background:#475569}
table{width:100%;max-width:680px;border-collapse:collapse;font-size:14px}
th{color:#94a3b8;font-weight:400;text-align:left;padding:8px 10px;border-bottom:1px solid #334155}
td{padding:9px 10px;border-bottom:1px solid #1e293b;font-variant-numeric:tabular-nums}
td.num{color:#94a3b8;white-space:nowrap}
td.ssid{word-break:break-all;max-width:280px}
.bar{width:120px;height:8px;background:#1e293b;border-radius:4px;overflow:hidden}
.bar i{display:block;height:100%;border-radius:4px}
.g{background:#4ade80}.y{background:#fbbf24}.r{background:#f87171}
.meta{width:100%;max-width:680px;display:flex;justify-content:space-between;color:#64748b;
font-size:12px;margin-top:14px;flex-wrap:wrap;gap:8px}
.empty{width:100%;max-width:680px;text-align:center;color:#64748b;padding:40px 0}
</style>
</head>
<body>
<header><h1>📶 WiFi 信号扫描仪</h1><button onclick="rescan()">立即扫描</button></header>
<div class="sub">由 ESP32 提供 · 热点 <b>ESP32-Scanner</b> · 页面每 8 秒自动刷新</div>
<table>
<thead><tr><th>信号强度</th><th>网络名称</th><th>RSSI</th><th>信道</th></tr></thead>
<tbody id="tbody"><tr><td colspan="4" class="empty">正在扫描，请稍候…</td></tr></tbody>
</table>
<div class="meta"><span id="count">--</span><span id="time">--</span></div>
<script>
const $=id=>document.getElementById(id);
function barColor(p){return p>60?'g':(p>30?'y':'r');}
function render(list){
  const tb=$('tbody');
  if(!list.length){tb.innerHTML='<tr><td colspan="4" class="empty">没有扫描到网络</td></tr>';}
  else{
    tb.innerHTML=list.map(a=>{
      const p=Math.max(5,Math.min(100,2*(a.rssi+100)));
      const esc=(''+a.ssid).replace(/&/g,'&amp;').replace(/</g,'&lt;');
      return `<tr><td><div class="bar"><i class="${barColor(p)}" style="width:${p}%"></i></div></td>`+
        `<td class="ssid">${esc}${a.secure?' 🔒':''}</td>`+
        `<td class="num">${a.rssi} dBm</td><td class="num">CH ${a.channel}</td></tr>`;
    }).join('');
  }
  $('count').textContent='共扫描到 '+list.length+' 个网络';
}
async function refresh(){
  try{
    const r=await fetch('/api/networks',{cache:'no-store'});
    render(await r.json());
    $('time').textContent='更新于 '+new Date().toLocaleTimeString();
  }catch(e){}
}
async function rescan(){
  try{await fetch('/api/scan');}catch(e){}
  setTimeout(refresh,3000);
}
setInterval(refresh,8000);
refresh();
</script>
</body>
</html>
)rawliteral";

// ---------- 工具与扫描 ----------
// JSON 字符串转义：SSID 里可能含有引号、反斜杠等字符
String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { o += ' '; }
    else { o += c; }
  }
  return o;
}

// 把一次扫描结果整理成按 RSSI 从强到弱排序的 JSON，并打印到串口
void buildJson(int n) {
  int m = n > MAX_AP ? MAX_AP : n;
  int order[MAX_AP];
  for (int i = 0; i < m; i++) order[i] = i;
  for (int i = 0; i < m - 1; i++)            // 按信号强度排序索引
    for (int j = 0; j < m - 1 - i; j++)
      if (WiFi.RSSI(order[j + 1]) > WiFi.RSSI(order[j])) {
        int t = order[j]; order[j] = order[j + 1]; order[j + 1] = t;
      }

  jsonCache = "[";
  for (int i = 0; i < m; i++) {
    int id = order[i];
    String ssid = WiFi.SSID(id);
    if (!ssid.length()) ssid = "<隐藏网络>";
    if (i) jsonCache += ",";
    jsonCache += "{\"ssid\":\"" + jsonEscape(ssid) + "\"";
    jsonCache += ",\"rssi\":" + String(WiFi.RSSI(id));
    jsonCache += ",\"channel\":" + String(WiFi.channel(id));
    jsonCache += ",\"secure\":" + String(WiFi.encryptionType(id) != WIFI_AUTH_OPEN ? "true" : "false");
    jsonCache += "}";
  }
  jsonCache += "]";

  Serial.println("-------- 扫描到 " + String(m) + " 个网络 --------");
  for (int i = 0; i < m; i++) {
    int id = order[i];
    String ssid = WiFi.SSID(id);
    if (!ssid.length()) ssid = "<隐藏网络>";
    Serial.printf("%2d. %4d dBm  CH%2d  %s  %s\n", i + 1, WiFi.RSSI(id),
                  WiFi.channel(id),
                  WiFi.encryptionType(id) != WIFI_AUTH_OPEN ? "[加密]" : "[开放]",
                  ssid.c_str());
  }
}

void startScan() {
  if (!scanning) {
    WiFi.scanNetworks(true);   // true = 异步扫描，不阻塞 Web 响应
    scanning = true;
  }
}

// ---------- HTTP 处理器 ----------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiNetworks() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", jsonCache);
}

// ---------- Arduino 入口 ----------
void setup() {
  Serial.begin(115200);
  delay(300);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);        // 开放热点，无需密码
  Serial.print("[AP] 请用手机连接热点 \"");
  Serial.print(AP_SSID);
  Serial.println("\"（无密码）");
  Serial.print("[AP] 连接后浏览器打开 http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/networks", HTTP_GET, handleApiNetworks);
  server.on("/api/scan", HTTP_GET, []() {
    startScan();
    server.send(200, "text/plain", "ok");
  });
  server.onNotFound([]() { server.send(404, "text/plain", "404 Not Found"); });
  server.begin();
  Serial.println("[HTTP] Web 服务已启动");

  startScan();
}

void loop() {
  if (scanning) {
    int n = WiFi.scanComplete();          // -1=进行中, -2=失败, >=0=结果数
    if (n >= 0) {
      buildJson(n);
      scanning = false;
      WiFi.scanDelete();                  // 释放扫描结果内存
    } else if (n == -2) {
      scanning = false;                   // 本轮失败，等下一轮自动重试
    }
  } else if (millis() - lastAutoMs > AUTO_SCAN_MS) {
    lastAutoMs = millis();
    startScan();
  }

  server.handleClient();
}
