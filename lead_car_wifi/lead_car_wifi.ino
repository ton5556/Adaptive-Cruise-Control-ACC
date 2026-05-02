/*
 * Lead Car - WiFi TCP Server
 * ส่งข้อมูลไป Pi5 ผ่าน TCP Socket
 * Format CSV: time_ms,phase,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB
 */

#include <WiFi.h>
#include <WiFiServer.h>

// ===== WiFi CONFIG =====
const char* WIFI_SSID = "car01";       
const char* WIFI_PASS = "123ton89";   
const int   TCP_PORT  = 5001;              

WiFiServer server(TCP_PORT);
WiFiClient client;

// ===== ENCODER =====
#define ENCA   2
#define ENCB   3
#define ENCA_B 4
#define ENCB_B 5

volatile long countA = 0;
volatile long countB = 0;

void IRAM_ATTR isrA()   { if (digitalRead(ENCA)   == digitalRead(ENCB))   countA++; else countA--; }
void IRAM_ATTR isrA_B() { if (digitalRead(ENCA_B) == digitalRead(ENCB_B)) countB++; else countB--; }

// ===== MOTOR =====
#define PWMA 13
#define AIN1 12
#define AIN2 11
#define PWMB 10
#define BIN1  1
#define BIN2  6
#define STBY  7

// ===== PI CONTROL =====
float Kp = 0.15, Ki = 0.8;
const float I_LIMIT = 200.0;
const int   PWM_MIN = 20, PWM_MAX = 200;

// ===== CONVERSION =====
const float CPR          = 275.0;
const float WHEEL_DIAM   = 65.0;
const float MM_PER_PULSE = (PI * WHEEL_DIAM) / CPR;

// ===== RAMP PATTERN =====
const float SPEED_SLOW = 100.0;
const float SPEED_MED  = 200.0;
const float SPEED_FAST = 300.0;
const unsigned long HOLD_MS = 4000;
const float RAMP_RATE  = 80.0;   // pps/s

float targetSpeed = SPEED_SLOW;
float rampTarget  = SPEED_SLOW;
float integralA = 0, integralB = 0;
int   lastPwmA  = 0, lastPwmB  = 0;

enum Phase { PH_HOLD_SLOW, PH_RAMP_UP_MED, PH_HOLD_MED,
             PH_RAMP_UP_FAST, PH_HOLD_FAST, PH_RAMP_DOWN_MED, PH_RAMP_DOWN_SLOW };
Phase currentPhase = PH_HOLD_SLOW;

const unsigned long LOOP_MS = 50;
unsigned long lastTime   = 0;
unsigned long phaseStart = 0;

// ===== UTILITY =====
long readCount(volatile long &cnt) { noInterrupts(); long v = cnt; interrupts(); return v; }

// ===== MOTOR DRIVE =====
void setMotorA(int pwm) {
  int out = constrain(abs(pwm), 0, PWM_MAX);
  if (out > 0 && out < PWM_MIN) out = PWM_MIN;
  if (pwm >= 0) { digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);  }
  else          { digitalWrite(AIN1, LOW);  digitalWrite(AIN2, HIGH); }
  ledcWrite(0, out);
  lastPwmA = (pwm >= 0) ? out : -out;
}

void setMotorB(int pwm) {
  int out = constrain(abs(pwm), 0, PWM_MAX);
  if (out > 0 && out < PWM_MIN) out = PWM_MIN;
  if (pwm >= 0) { digitalWrite(BIN1, LOW);  digitalWrite(BIN2, HIGH); }
  else          { digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);  }
  ledcWrite(1, out);
  lastPwmB = (pwm >= 0) ? out : -out;
}

// ===== RAMP STATE MACHINE =====
void updatePattern(float dt) {
  unsigned long elapsed = millis() - phaseStart;
  switch (currentPhase) {
    case PH_HOLD_SLOW:     rampTarget = SPEED_SLOW; if (elapsed >= HOLD_MS) { currentPhase = PH_RAMP_UP_MED;    phaseStart = millis(); } break;
    case PH_RAMP_UP_MED:   rampTarget = SPEED_MED;  if (targetSpeed >= SPEED_MED  - 2) { currentPhase = PH_HOLD_MED;       phaseStart = millis(); } break;
    case PH_HOLD_MED:      rampTarget = SPEED_MED;  if (elapsed >= HOLD_MS) { currentPhase = PH_RAMP_UP_FAST;   phaseStart = millis(); } break;
    case PH_RAMP_UP_FAST:  rampTarget = SPEED_FAST; if (targetSpeed >= SPEED_FAST - 2) { currentPhase = PH_HOLD_FAST;      phaseStart = millis(); } break;
    case PH_HOLD_FAST:     rampTarget = SPEED_FAST; if (elapsed >= HOLD_MS) { currentPhase = PH_RAMP_DOWN_MED;  phaseStart = millis(); } break;
    case PH_RAMP_DOWN_MED: rampTarget = SPEED_MED;  if (targetSpeed <= SPEED_MED  + 2) { currentPhase = PH_RAMP_DOWN_SLOW; phaseStart = millis(); } break;
    case PH_RAMP_DOWN_SLOW:rampTarget = SPEED_SLOW; if (targetSpeed <= SPEED_SLOW + 2) { currentPhase = PH_HOLD_SLOW;      phaseStart = millis(); } break;
  }
  float maxStep = RAMP_RATE * dt;
  if      (targetSpeed < rampTarget - maxStep) targetSpeed += maxStep;
  else if (targetSpeed > rampTarget + maxStep) targetSpeed -= maxStep;
  else                                         targetSpeed  = rampTarget;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nWiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());
  server.begin();
  Serial.printf("TCP server on port %d\n", TCP_PORT);

  // Encoder
  pinMode(ENCA,   INPUT_PULLUP); pinMode(ENCB,   INPUT_PULLUP);
  pinMode(ENCA_B, INPUT_PULLUP); pinMode(ENCB_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCA),   isrA,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCA_B), isrA_B, CHANGE);

  // Motor
  pinMode(AIN1,OUTPUT); pinMode(AIN2,OUTPUT);
  pinMode(BIN1,OUTPUT); pinMode(BIN2,OUTPUT);
  pinMode(STBY,OUTPUT); digitalWrite(STBY, HIGH);
  ledcSetup(0,20000,8); ledcAttachPin(PWMA,0);
  ledcSetup(1,20000,8); ledcAttachPin(PWMB,1);

  lastTime = phaseStart = millis();
}

// ===== LOOP =====
void loop() {
  // รับ client ใหม่
  if (!client || !client.connected()) {
    client = server.available();
    if (client) Serial.println("Pi5 connected!");
  }

  unsigned long now = millis();
  if (now - lastTime < LOOP_MS) return;
  float dt = (now - lastTime) / 1000.0;
  lastTime = now;

  // Pattern
  updatePattern(dt);

  // Encoder
  static long lastA = 0, lastB = 0;
  long snapA = readCount(countA), snapB = readCount(countB);
  float speedA = (snapA - lastA) / dt;
  float speedB = (snapB - lastB) / dt;
  lastA = snapA; lastB = snapB;

  // PI
  float eA = targetSpeed - speedA, eB = targetSpeed - speedB;
  bool satA = (Kp*eA + Ki*integralA) > PWM_MAX || (Kp*eA + Ki*integralA) < -PWM_MAX;
  bool satB = (Kp*eB + Ki*integralB) > PWM_MAX || (Kp*eB + Ki*integralB) < -PWM_MAX;
  if (!satA || eA*integralA < 0) integralA = constrain(integralA + eA*dt, -I_LIMIT, I_LIMIT);
  if (!satB || eB*integralB < 0) integralB = constrain(integralB + eB*dt, -I_LIMIT, I_LIMIT);
  setMotorA((int)(Kp*eA + Ki*integralA));
  setMotorB((int)(Kp*eB + Ki*integralB));

  // ส่งข้อมูล TCP → Pi5
  if (client && client.connected()) {
    char buf[80];
    snprintf(buf, sizeof(buf), "%lu,%d,%.1f,%.1f,%.1f,%d,%d\n",
      now, (int)currentPhase,
      targetSpeed * MM_PER_PULSE,
      speedA      * MM_PER_PULSE,
      speedB      * MM_PER_PULSE,
      lastPwmA, lastPwmB);
    client.print(buf);
  }
}
