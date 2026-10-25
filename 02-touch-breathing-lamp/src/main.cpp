/**
 * 实验二 · 触摸呼吸灯 (Touch Breathing Lamp)
 * -------------------------------------------
 * 板载 LED 由 PWM 驱动呼吸；触摸 T0 (GPIO4) 切换显示模式：
 *   慢呼吸 → 快呼吸 → 心跳 → 熄灭 → 慢呼吸 ...
 * 不需要任何外部硬件，插一根杜邦线在 GPIO4 上可增强触摸效果。
 */

#include <Arduino.h>

#define LED_PIN      2
#define PWM_CHANNEL  0
#define PWM_FREQ     5000    // 5kHz PWM，8 位分辨率（0~255）

// 显示模式
enum LampMode { MODE_BREATH_SLOW, MODE_BREATH_FAST, MODE_HEARTBEAT, MODE_OFF, MODE_COUNT };
const char* const MODE_NAMES[MODE_COUNT] = {"慢呼吸", "快呼吸", "心跳", "熄灭"};

LampMode mode = MODE_BREATH_SLOW;
bool wasTouched = false;
unsigned long lastTapMs = 0;
int touchBaseline = 0;    // 开机时采集的「未触摸」基准值

// 电容触摸特性：被触摸时 touchRead 读数明显下降。
// 低于基准值的 72% 判定为触摸，自动校准可适应不同开发板。
int touchThreshold() {
  return touchBaseline * 72 / 100;
}

// 正弦呼吸 + 伽马校正（人眼对暗部变化更敏感，校正后视觉上亮度均匀渐变）
uint8_t breathDuty(unsigned long t, unsigned long periodMs) {
  float phase = (t % periodMs) / (float)periodMs;
  float s = 0.5f - 0.5f * cosf(2.0f * PI * phase);   // 0 → 1 → 0 的平滑曲线
  return (uint8_t)(powf(s, 1.6f) * 255.0f + 0.5f);
}

// 心跳波形：两次快速脉冲 + 停顿，周期 1400ms
uint8_t heartbeatDuty(unsigned long t) {
  unsigned long p = t % 1400;
  const int centers[2] = {120, 420};
  int v = 0;
  for (int i = 0; i < 2; i++) {
    long d = (long)p - centers[i];
    if (d < 0) d = -d;
    if (d < 160) v += 160 - (int)d;   // 三角脉冲，峰值 160
  }
  return v > 255 ? 255 : v;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, 8);
  ledcAttachPin(LED_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  // 开机 2 秒自动校准触摸基准（期间不要碰 T0/GPIO4）
  Serial.println("[校准] 采集触摸基准值，请不要触摸 T0 (GPIO4) ...");
  long sum = 0;
  int n = 0;
  unsigned long start = millis();
  while (millis() - start < 2000) {
    sum += touchRead(T0);
    n++;
    delay(20);
  }
  touchBaseline = sum / n;
  Serial.printf("[校准] 基准=%d，阈值=%d（读数低于阈值即视为触摸）\n",
                touchBaseline, touchThreshold());
  Serial.println("[就绪] 触摸 T0 (GPIO4) 切换显示模式");
}

void loop() {
  unsigned long now = millis();

  // 触摸「按下」瞬间切换模式：下降沿检测 + 300ms 消抖
  bool touched = touchRead(T0) < touchThreshold();
  if (touched && !wasTouched && now - lastTapMs > 300) {
    lastTapMs = now;
    mode = (LampMode)((mode + 1) % MODE_COUNT);
    Serial.printf("[模式] 切换为：%s\n", MODE_NAMES[mode]);
  }
  wasTouched = touched;

  uint8_t duty = 0;
  switch (mode) {
    case MODE_BREATH_SLOW: duty = breathDuty(now, 4000); break;
    case MODE_BREATH_FAST: duty = breathDuty(now, 1000); break;
    case MODE_HEARTBEAT:   duty = heartbeatDuty(now);    break;
    default:               duty = 0;                     break;
  }
  ledcWrite(PWM_CHANNEL, duty);

  delay(10);   // 约 100Hz 刷新，肉眼完全流畅
}
