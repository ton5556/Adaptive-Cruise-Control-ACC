/*
 * Mobile Robot - PI Speed Control + Simple ACC
 * mini project subj. control systems , head: Adaptive Cruise Control ACC
 * ESP32-S3 Zero | TB6612FNG | TT Motor + Encoder | VL53L0X
 * Serial Output: CSV → Raspberry Pi 5
 * Format: time_ms,dist_mm,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB
 */

#include <Wire.h>
#include "Adafruit_VL53L0X.h"

// ===== VL53L0X =====
Adafruit_VL53L0X lox;
float measuredDist = 0;
bool targetPresent = false;

// ===== ACC PARAM =====
const float DIST_TARGET = 200.0;  // mm
const float DIST_MAX = 600.0;     // mm
const float DIST_STOP = 80.0;     // mm
const float Kgap = 1.5;
const float CRUISE_SPEED = 150.0;      // pulse/s (>60cm)
const float BASE_FOLLOW_SPEED = 80.0;  // pulse/s - ความเร็วพื้นฐานตอนตาม target
const float SPEED_MAX = 500.0;

// ===== ENCODER CONVERSION =====
const float CPR = 275.0;                             // pulse ต่อรอบ (X1)
const float WHEEL_DIAM = 65.0;                       // mm
const float MM_PER_PULSE = (PI * WHEEL_DIAM) / CPR;  // ~0.742 mm/pulse

// ===== TARGET SPEED =====
float targetSpeed = 0;  // หน่วย pulse/s

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

float Kp = 0.15;
float Ki = 0.8;
const float I_LIMIT = 200.0;
const int PWM_MIN = 20;
const int PWM_MAX = 200;

// ===== TIMING =====
const unsigned long LOOP_MS = 50;
unsigned long lastTime = 0;

// ===== PI STATE =====
float integralA = 0, integralB = 0;
int lastPwmA = 0, lastPwmB = 0;  // เก็บไว้ส่ง debug

// ===== UTILITY =====
long readCount(volatile long &cnt) {
  noInterrupts();
  long val = cnt;
  interrupts();
  return val;
}

// ===== SENSOR =====
void updateDistance() {
  if (!lox.isRangeComplete()) return;

  uint16_t raw = lox.readRangeResult();

  if (raw >= 8190) {
    targetPresent = false;
    measuredDist = DIST_MAX + 1;
    return;
  }

  measuredDist = (float)raw;
  targetPresent = (measuredDist < DIST_MAX);
}

// ===== GAP CONTROLLER =====
float gapController() {
  if (!targetPresent) {
    return CRUISE_SPEED;  // ไม่มี target → cruise
  }

  if (measuredDist < DIST_STOP) {
    integralA = integralB = 0;
    return 0;  // หยุด
  }

  // BASE_FOLLOW + proportional gap
  float speed = BASE_FOLLOW_SPEED + Kgap * (measuredDist - DIST_TARGET);
  return constrain(speed, 0, SPEED_MAX);
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  // Header สำหรับ Python อ่าน
  delay(500);
  Serial.println("HEADER:time_ms,dist_mm,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB");

  // ===== I2C =====
  Wire.begin();

  if (!lox.begin()) {
    Serial.println("ERROR:VL53L0X not found!");
    while (1)
      ;
  }
  lox.startRangeContinuous();

  // ===== ENCODER =====
  pinMode(ENCA, INPUT_PULLUP);
  pinMode(ENCB, INPUT_PULLUP);
  pinMode(ENCA_B, INPUT_PULLUP);
  pinMode(ENCB_B, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENCA), isrA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCA_B), isrA_B, CHANGE);

  // ===== MOTOR =====
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

  lastTime = millis();
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

// ===== MAIN LOOP =====
void loop() {
  unsigned long now = millis();
  if (now - lastTime < LOOP_MS) return;
  float dt = (now - lastTime) / 1000.0;
  lastTime = now;

  // ===== 1. SENSOR =====
  updateDistance();

  // ===== 2. TARGET SPEED =====
  targetSpeed = gapController();

  // ===== 3. ENCODER =====
  static long lastA = 0, lastB = 0;
  long snapA = readCount(countA);
  long snapB = readCount(countB);

  float speedA = (snapA - lastA) / dt;  // pulse/s
  float speedB = (snapB - lastB) / dt;

  lastA = snapA;
  lastB = snapB;

  // ===== 4. PI =====
  float errorA = targetSpeed - speedA;
  float errorB = targetSpeed - speedB;

  bool satA = ((Kp * errorA + Ki * integralA) > PWM_MAX || (Kp * errorA + Ki * integralA) < -PWM_MAX);
  bool satB = ((Kp * errorB + Ki * integralB) > PWM_MAX || (Kp * errorB + Ki * integralB) < -PWM_MAX);

  if (!satA || (errorA * integralA < 0))
    integralA = constrain(integralA + errorA * dt, -I_LIMIT, I_LIMIT);
  if (!satB || (errorB * integralB < 0))
    integralB = constrain(integralB + errorB * dt, -I_LIMIT, I_LIMIT);

  int pwmA = (int)(Kp * errorA + Ki * integralA);
  int pwmB = (int)(Kp * errorB + Ki * integralB);

  setMotorA(pwmA);
  setMotorB(pwmB);

  // ===== 5. SERIAL OUTPUT (CSV) → Pi5 =====
  // หน่วย: mm/s สำหรับ speed (แปลงจาก pulse/s × mm/pulse)
  Serial.printf("%lu,%.1f,%.1f,%.1f,%.1f,%d,%d\n",
                now,
                measuredDist,
                targetSpeed * MM_PER_PULSE,
                speedA * MM_PER_PULSE,
                speedB * MM_PER_PULSE,
                lastPwmA,
                lastPwmB);
}
