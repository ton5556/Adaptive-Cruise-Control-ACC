/*
 * Lead Car - WiFi TCP Server + MPU6050 Steering Correction
 * Format CSV: time_ms,phase,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB,gyroZ,heading
 */

#include <WiFi.h>
#include <WiFiServer.h>
#include <Wire.h>
#include <MPU6050_light.h>  // ติดตั้งผ่าน Library Manager: "MPU6050_light" by rfetick
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== WiFi CONFIG =====
const char* WIFI_SSID = "car01";
const char* WIFI_PASS = "123ton89";
const int TCP_PORT = 5001;

#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

WiFiServer server(TCP_PORT);
WiFiClient client;

// ===== MPU6050 =====
MPU6050 mpu(Wire);
float gyroZ = 0.0;    // yaw rate  (°/s)
float heading = 0.0;  // integrated heading (°)

// ===== YAW CORRECTION GAINS =====
// ปรับค่าเหล่านี้บน Pi5 หรือ Serial
float Kp_yaw = 0.8;                 // proportional ต่อ heading error  (°  → PWM)
float Kd_yaw = 0.3;                 // derivative  ต่อ yaw rate        (°/s → PWM)
const float YAW_CORR_LIMIT = 60.0;  // clamp correction ไม่ให้ใหญ่เกิน

// ===== ENCODER =====
#define ENCA 2
#define ENCB 3
#define ENCA_B 4
#define ENCB_B 5

volatile long countA = 0;
volatile long countB = 0;

void IRAM_ATTR isrA() {
  if (digitalRead(ENCA) == digitalRead(ENCB)) countA++;
  else countA--;
}
void IRAM_ATTR isrA_B() {
  if (digitalRead(ENCA_B) == digitalRead(ENCB_B)) countB++;
  else countB--;
}

// ===== MOTOR =====
#define PWMA 13
#define AIN1 12
#define AIN2 11
#define PWMB 10
#define BIN1 1
#define BIN2 6
#define STBY 7

// ===== PI SPEED CONTROL =====
float Kp = 0.15, Ki = 0.8;
const float I_LIMIT = 200.0;
const int PWM_MIN = 20, PWM_MAX = 200;

// ===== CONVERSION =====
const float CPR = 275.0;
const float WHEEL_DIAM = 65.0;
const float MM_PER_PULSE = (PI * WHEEL_DIAM) / CPR;

// ===== RAMP PATTERN =====
const float SPEED_SLOW = 0.0;
const float SPEED_MED = 200.0;
const float SPEED_FAST = 300.0;
const unsigned long HOLD_MS = 4000;
const float RAMP_RATE = 40.0;

float targetSpeed = SPEED_SLOW;
float rampTarget = SPEED_SLOW;
float integralA = 0, integralB = 0;
int lastPwmA = 0, lastPwmB = 0;

enum Phase { PH_HOLD_SLOW,
             PH_RAMP_UP_MED,
             PH_HOLD_MED,
             PH_RAMP_UP_FAST,
             PH_HOLD_FAST,
             PH_RAMP_DOWN_MED,
             PH_RAMP_DOWN_SLOW };
Phase currentPhase = PH_HOLD_SLOW;

const unsigned long LOOP_MS = 50;
unsigned long lastTime = 0;
unsigned long phaseStart = 0;

// ===== UTILITY =====
long readCount(volatile long& cnt) {
  noInterrupts();
  long v = cnt;
  interrupts();
  return v;
}

// ===== MOTOR DRIVE =====
void setMotorA(int pwm) {
  int out = constrain(abs(pwm), 0, PWM_MAX);
  if (out > 0 && out < PWM_MIN) out = PWM_MIN;
  if (pwm >= 0) {
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
  } else {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
  }
  ledcWrite(0, out);
  lastPwmA = (pwm >= 0) ? out : -out;
}

void setMotorB(int pwm) {
  int out = constrain(abs(pwm), 0, PWM_MAX);
  if (out > 0 && out < PWM_MIN) out = PWM_MIN;
  if (pwm >= 0) {
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, HIGH);
  } else {
    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);
  }
  ledcWrite(1, out);
  lastPwmB = (pwm >= 0) ? out : -out;
}

// ===== RAMP STATE MACHINE =====
void updatePattern(float dt) {
  unsigned long elapsed = millis() - phaseStart;
  switch (currentPhase) {
    case PH_HOLD_SLOW:
      rampTarget = SPEED_SLOW;
      if (elapsed >= HOLD_MS) {
        currentPhase = PH_RAMP_UP_MED;
        phaseStart = millis();
      }
      break;
    case PH_RAMP_UP_MED:
      rampTarget = SPEED_MED;
      if (targetSpeed >= SPEED_MED - 2) {
        currentPhase = PH_HOLD_MED;
        phaseStart = millis();
      }
      break;
    case PH_HOLD_MED:
      rampTarget = SPEED_MED;
      if (elapsed >= HOLD_MS) {
        currentPhase = PH_RAMP_UP_FAST;
        phaseStart = millis();
      }
      break;
    case PH_RAMP_UP_FAST:
      rampTarget = SPEED_FAST;
      if (targetSpeed >= SPEED_FAST - 2) {
        currentPhase = PH_HOLD_FAST;
        phaseStart = millis();
      }
      break;
    case PH_HOLD_FAST:
      rampTarget = SPEED_FAST;
      if (elapsed >= HOLD_MS) {
        currentPhase = PH_RAMP_DOWN_MED;
        phaseStart = millis();
      }
      break;
    case PH_RAMP_DOWN_MED:
      rampTarget = SPEED_MED;
      if (targetSpeed <= SPEED_MED + 2) {
        currentPhase = PH_RAMP_DOWN_SLOW;
        phaseStart = millis();
      }
      break;
    case PH_RAMP_DOWN_SLOW:
      rampTarget = SPEED_SLOW;
      if (targetSpeed <= SPEED_SLOW + 2) {
        currentPhase = PH_HOLD_SLOW;
        phaseStart = millis();
      }
      break;
  }
  float maxStep = RAMP_RATE * dt;
  if (targetSpeed < rampTarget - maxStep) targetSpeed += maxStep;
  else if (targetSpeed > rampTarget + maxStep) targetSpeed -= maxStep;
  else targetSpeed = rampTarget;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  // I2C + MPU6050
  Wire.begin(8, 9);
  byte status = mpu.begin();
  Serial.printf("MPU6050 status: %d\n", status);  // 0 = OK
  delay(1000);
  mpu.calcOffsets();  // calibrate gyro offset (วางรถนิ่งๆ 1–2 วิ)
  Serial.println("MPU6050 calibrated");

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());
  server.begin();
  Serial.printf("TCP server on port %d\n", TCP_PORT);

  // Encoder
  pinMode(ENCA, INPUT_PULLUP);
  pinMode(ENCB, INPUT_PULLUP);
  pinMode(ENCA_B, INPUT_PULLUP);
  pinMode(ENCB_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCA), isrA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCA_B), isrA_B, CHANGE);

  // Motor
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, HIGH);
  ledcSetup(0, 20000, 8);
  ledcAttachPin(PWMA, 0);
  ledcSetup(1, 20000, 8);
  ledcAttachPin(PWMB, 1);

  lastTime = phaseStart = millis();
}

// ===== ฟังก์ชัน draw OLED =====
// วางก่อน loop()

void drawOLED_align() {
  display.clearDisplay();
  float abs_h = fabs(heading);

  // --- Zone 1: header ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ALIGN MODE");

  const char* status;
  if (abs_h < 2.0) status = "READY";
  else if (abs_h < 5.0) status = "ADJUST";
  else status = "TILT!";

  int sw = strlen(status) * 6;
  display.setCursor(128 - sw, 0);
  display.print(status);

  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // --- Zone 2: bubble level bar ---
  const int BAR_Y = 13, BAR_H = 34, BAR_W = 128;
  display.drawRect(0, BAR_Y, BAR_W, BAR_H, SSD1306_WHITE);

  // เส้นกริด ทุก 10 px
  for (int x = 10; x < BAR_W; x += 10) {
    display.drawFastVLine(x, BAR_Y + 2, BAR_H - 4, SSD1306_WHITE);
  }

  // เส้นกลาง (ตำแหน่ง 0°)
  display.drawFastVLine(64, BAR_Y, BAR_H, SSD1306_WHITE);

  // triangle indicator อยู่ใต้ center
  display.fillTriangle(62, BAR_Y + BAR_H - 1,
                       66, BAR_Y + BAR_H - 1,
                       64, BAR_Y + BAR_H - 5,
                       SSD1306_WHITE);

  // bubble position: max drift ±15° → ±50px จากกลาง
  const float MAX_DEG = 15.0;
  int offset = (int)((heading / MAX_DEG) * 50.0);
  offset = constrain(offset, -54, 54);
  int bx = 64 + offset;

  // วาด bubble (กว่างขึ้นถ้าใกล้ 0°)
  int bw = (abs_h < 2.0) ? 9 : 7;
  display.fillRect(bx - bw / 2, BAR_Y + 3, bw, BAR_H - 6, SSD1306_WHITE);

  // ถ้าอยู่กลาง: กระพริบ (invert ช่วง ok)
  if (abs_h < 2.0) {
    static bool blink = false;
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 400) {
      blink = !blink;
      lastBlink = millis();
    }
    if (blink) display.fillRect(bx - bw / 2, BAR_Y + 3, bw, BAR_H - 6, SSD1306_BLACK);
  }

  // --- Zone 3: ตัวเลข ---
  display.drawFastHLine(0, 49, 128, SSD1306_WHITE);
  display.setTextSize(1);

  // heading
  display.setCursor(0, 54);
  if (heading >= 0) display.print("+");
  display.print(heading, 1);
  display.print("deg");

  // yaw rate (ขวา)
  char rateBuf[12];
  snprintf(rateBuf, sizeof(rateBuf), "%+.0f/s", gyroZ);
  int rw2 = strlen(rateBuf) * 6;
  display.setCursor(128 - rw2, 54);
  display.print(rateBuf);

  display.display();
}

// ===== LOOP =====
void loop() {
  if (!client || !client.connected()) {
    client = server.available();
    if (client) Serial.println("Pi5 connected!");
  }

  unsigned long now = millis();
  if (now - lastTime < LOOP_MS) return;
  float dt = (now - lastTime) / 1000.0;
  lastTime = now;

  // ----- อ่าน MPU6050 -----
  mpu.update();
  gyroZ = mpu.getGyroZ();  // °/s  (yaw rate)
  heading += gyroZ * dt;   // integrate → heading °
  // heading = 0 คือทิศตั้งต้น  บวก = เลี้ยวขวา  ลบ = เลี้ยวซ้าย

  // ----- คำนวณ steering correction -----
  // correction > 0 → รถเลี้ยวขวา → เพิ่ม A ลด B
  // correction < 0 → รถเลี้ยวซ้าย → ลด A เพิ่ม B
  float correction = constrain(
    Kp_yaw * heading + Kd_yaw * gyroZ,
    -YAW_CORR_LIMIT, YAW_CORR_LIMIT);

  // ----- Pattern -----
  updatePattern(dt);

  // ใน loop() หลัง updatePattern()
  if (currentPhase == PH_HOLD_SLOW && (!client || !client.connected())) {
    drawOLED_align();
    return;  // ไม่หมุนมอเตอร์จนกว่า Pi5 จะ connect
  }

  // ----- Encoder -----
  static long lastA = 0, lastB = 0;
  long snapA = readCount(countA), snapB = readCount(countB);
  float speedA = (snapA - lastA) / dt;
  float speedB = (snapB - lastB) / dt;
  lastA = snapA;
  lastB = snapB;

  // ----- PI speed control -----
  float eA = targetSpeed - speedA, eB = targetSpeed - speedB;
  bool satA = (Kp * eA + Ki * integralA) > PWM_MAX || (Kp * eA + Ki * integralA) < -PWM_MAX;
  bool satB = (Kp * eB + Ki * integralB) > PWM_MAX || (Kp * eB + Ki * integralB) < -PWM_MAX;
  if (!satA || eA * integralA < 0) integralA = constrain(integralA + eA * dt, -I_LIMIT, I_LIMIT);
  if (!satB || eB * integralB < 0) integralB = constrain(integralB + eB * dt, -I_LIMIT, I_LIMIT);

  int pwmA_out = (int)(Kp * eA + Ki * integralA + correction);  // +correction
  int pwmB_out = (int)(Kp * eB + Ki * integralB - correction);  // -correction
  setMotorA(pwmA_out);
  setMotorB(pwmB_out);

  // ----- ส่ง TCP -----
  if (client && client.connected()) {
    char buf[100];
    snprintf(buf, sizeof(buf), "%lu,%d,%.1f,%.1f,%.1f,%d,%d,%.2f,%.2f\n",
             now, (int)currentPhase,
             targetSpeed * MM_PER_PULSE,
             speedA * MM_PER_PULSE,
             speedB * MM_PER_PULSE,
             lastPwmA, lastPwmB,
             gyroZ, heading);
    client.print(buf);
  }
}
