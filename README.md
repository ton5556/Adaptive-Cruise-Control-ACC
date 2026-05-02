# Adaptive Cruise Control (ACC) - Version 2.1

ระบบควบคุมความเร็วอัตโนมัติสำหรับยานพาหนะ (Adaptive Cruise Control System)

## 📁 โครงสร้างโปรเจค

```
Adaptive-Cruise-Control-ACC/
│
├── pi5_dashboard.py                    # Dashboard บน Raspberry Pi 5 สำหรับแสดงผลและควบคุม
├── plot_analysis.py                    # Script สำหรับวิเคราะห์และ plot ข้อมูล
│
├── acc_send_val_to_Pi_Full/            # โฟลเดอร์สำหรับการส่งค่าไปยัง Pi
│   └── esp32_acc/
│       └── esp32_acc.ino               # โค้ด Arduino สำหรับ ESP32 ควบคุม ACC
│
└── lead_car_wifi/
    └── lead_car_wifi.ino               # โค้ด Arduino สำหรับรถยนต์นำหน้า (Lead car)
```

## 📋 รายละเอียดไฟล์

### Python Scripts
| ไฟล์ | คำอธิบาย |
|-----|---------|
| `pi5_dashboard.py` | Dashboard ที่ทำงานบน Raspberry Pi 5 เพื่อแสดงผลข้อมูล ACC และการควบคุม |
| `plot_analysis.py` | Script สำหรับวิเคราะห์และแสดงผลกราฟข้อมูลจากระบบ ACC |

### Arduino/ESP32 Code
| ไฟล์ | คำอธิบาย |
|-----|---------|
| `acc_send_val_to_Pi_Full/esp32_acc/esp32_acc.ino` | โค้ดหลัก ESP32 สำหรับควบคุมระบบ ACC และส่งข้อมูลไปยัง Raspberry Pi |
| `lead_car_wifi/lead_car_wifi.ino` | โค้ด Arduino/ESP32 สำหรับรถนำหน้า (Lead Car) ส่งสัญญาณทาง WiFi |

## 🔧 สำเร็จการติดตั้ง

### ความต้องการ
- Raspberry Pi 5
- ESP32 Microcontroller
- Arduino IDE
- Python 3.x

### ขั้นตอนการติดตั้ง
1. Clone repository
2. Upload `esp32_acc.ino` ไปยัง ESP32 บนรถ
3. Upload `lead_car_wifi.ino` ไปยัง Lead car
4. รัน `pi5_dashboard.py` บน Raspberry Pi 5

## 📊 หน้าที่ของแต่ละส่วน

- **ESP32 Main Unit**: อ่านข้อมูลเซนเซอร์ ควบคุมความเร็ว และส่งข้อมูลไปยัง Pi
- **Lead Car Unit**: ส่งตำแหน่งและข้อมูลรถนำหน้า
- **Raspberry Pi Dashboard**: รับข้อมูล แสดงผล และรับการควบคุมจากผู้ใช้
- **Analysis Script**: วิเคราะห์ข้อมูลประสิทธิภาพของระบบ

## � โค้ดตัวอย่าง

### 1️⃣ pi5_dashboard.py
Dashboard สำหรับรับข้อมูลจาก ACC Car และ Lead Car, แสดง Real-time graph

```python
#!/usr/bin/env python3
"""
Pi5 Data Server + Real-time Dashboard
รับข้อมูล:
  - ACC Car  → USB Serial  (/dev/ttyUSB0)
  - Lead Car → TCP Socket  (port 5001)
แสดงผล: Real-time matplotlib graph (VNC)
บันทึก: CSV log ทั้ง 2 คัน
"""

import serial
import socket
import threading
import time
import csv
import os
from datetime import datetime
from collections import deque

import matplotlib
matplotlib.use('TkAgg')          # ใช้งานผ่าน VNC ได้
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ===== CONFIG =====
SERIAL_PORT  = "/dev/ttyUSB0"   # ← เปลี่ยนถ้า port ต่างกัน
SERIAL_BAUD  = 115200
TCP_HOST     = "0.0.0.0"        # รับทุก interface
TCP_PORT     = 5001
BUFFER_LEN   = 300              # จำนวนจุดที่แสดงในกราฟ

# ===== THREAD: ACC Car (USB Serial) =====
def thread_acc():
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=1)
            print(f"[ACC] Serial connected: {SERIAL_PORT}")
            for line in ser:
                try:
                    line = line.decode('utf-8', errors='ignore').strip()
                    parts = line.split(',')
                    if len(parts) < 7:
                        continue
                    # Format: time_ms,dist_mm,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB
                    t_ms, d, tgt, spA, spB, pA, pB = (float(x) for x in parts[:7])
                    spd_avg = (spA + spB) / 2.0
                except Exception:
                    pass
        except serial.SerialException as e:
            print(f"[ACC] Serial error: {e} — retry in 3s")
            time.sleep(3)
```

### 2️⃣ plot_analysis.py
Script วิเคราะห์และ plot กราฟจากข้อมูล CSV

```python
#!/usr/bin/env python3
"""
Post-run Analysis — plot กราฟจาก CSV log หลังวิ่งเสร็จ
Usage:
  python3 plot_analysis.py                        # ใช้ไฟล์ล่าสุดใน ~/acc_logs
  python3 plot_analysis.py acc_X.csv lead_X.csv  # ระบุไฟล์เอง
"""

import sys
import os
import glob
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

LOG_DIR = os.path.expanduser("~/acc_logs")

# ===== หาไฟล์ล่าสุด =====
acc_files  = sorted(glob.glob(os.path.join(LOG_DIR, "acc_*.csv")))
lead_files = sorted(glob.glob(os.path.join(LOG_DIR, "lead_*.csv")))

if not acc_files or not lead_files:
    print(f"[ERROR] ไม่พบ log ใน {LOG_DIR}")
    sys.exit(1)

acc_file  = acc_files[-1]
lead_file = lead_files[-1]

# ===== LOAD & PROCESS =====
df_acc  = pd.read_csv(acc_file)
df_lead = pd.read_csv(lead_file)

# แปลงเวลาให้เริ่มที่ 0
df_acc['t']  = (df_acc['time_ms']  - df_acc['time_ms'].iloc[0])  / 1000.0
df_lead['t'] = (df_lead['time_ms'] - df_lead['time_ms'].iloc[0]) / 1000.0

# speed average
df_acc['speed_avg']  = (df_acc['speedA_mmps']  + df_acc['speedB_mmps'])  / 2
df_lead['speed_avg'] = (df_lead['speedA_mmps'] + df_lead['speedB_mmps']) / 2
```

### 3️⃣ esp32_acc.ino
โค้ดหลัก ESP32 สำหรับควบคุมระบบ ACC พร้อม VL53L0X ToF sensor

```cpp
/*
 * Mobile Robot - PI Speed Control + Simple ACC
 * ESP32-S3 Zero | TB6612FNG | TT Motor + Encoder | VL53L0X
 * Serial Output: CSV → Raspberry Pi 5
 * Format: time_ms,dist_mm,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB
 */

#include <Wire.h>
#include "Adafruit_VL53L0X.h"

// ===== VL53L0X ToF Sensor =====
Adafruit_VL53L0X lox;
float measuredDist = 0;
bool targetPresent = false;

// ===== ACC PARAMETERS =====
const float DIST_TARGET = 200.0;   // mm - ระยะที่ต้องการรักษา
const float DIST_MAX    = 600.0;   // mm - ถ้าไกลกว่านี้ถือว่าไม่มี target
const float DIST_STOP   = 80.0;    // mm - หยุดถ้าใกล้กว่านี้
const float Kgap        = 1.5;     // gain ของ gap controller
const float CRUISE_SPEED = 150.0;  // pulse/s = ~185 mm/s
const float SPEED_MAX   = 500.0;   // pulse/s

// ===== ENCODER CONVERSION =====
const float CPR        = 275.0;    // pulse ต่อรอบ
const float WHEEL_DIAM = 65.0;     // mm
const float MM_PER_PULSE = (PI * WHEEL_DIAM) / CPR;  // ~0.742 mm/pulse

// ===== MOTOR CONTROL (PI) =====
float Kp = 0.15;
float Ki = 0.8;
const float I_LIMIT = 200.0;
const int   PWM_MIN  = 20;
const int   PWM_MAX  = 200;

// ===== ENCODER ISR =====
#define ENCA   2
#define ENCB   3
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

// ===== MOTOR PINS =====
#define PWMA 13
#define AIN1 12
#define AIN2 11
#define PWMB 10
#define BIN1  1
#define BIN2  6
#define STBY  7
```

### 4️⃣ lead_car_wifi.ino
โค้ด WiFi TCP Server สำหรับรถนำหน้าส่งข้อมูลไปยัง Pi5

```cpp
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

// ===== ENCODER PINS =====
#define ENCA   2
#define ENCB   3
#define ENCA_B 4
#define ENCB_B 5

volatile long countA = 0;
volatile long countB = 0;

void IRAM_ATTR isrA()   { 
  if (digitalRead(ENCA) == digitalRead(ENCB)) countA++; 
  else countA--; 
}

void IRAM_ATTR isrA_B() { 
  if (digitalRead(ENCA_B) == digitalRead(ENCB_B)) countB++; 
  else countB--; 
}

// ===== MOTOR CONTROL =====
#define PWMA 13
#define AIN1 12
#define AIN2 11
#define PWMB 10
#define BIN1  1
#define BIN2  6
#define STBY  7

// ===== PI CONTROL PARAMETERS =====
float Kp = 0.15, Ki = 0.8;
const float I_LIMIT = 200.0;
const int   PWM_MIN = 20, PWM_MAX = 200;

// ===== SPEED RAMP PATTERN =====
const float SPEED_SLOW = 100.0;  // pps
const float SPEED_MED  = 200.0;  // pps
const float SPEED_FAST = 300.0;  // pps
const unsigned long HOLD_MS = 4000;
const float RAMP_RATE  = 80.0;   // pps/s
```

## 📡 Data Format (CSV)

### ACC Car Output
```
time_ms,dist_mm,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB
1050,245,150,142,148,127,130
1100,242,150,145,152,128,131
1150,240,150,148,150,129,130
```

### Lead Car Output
```
time_ms,phase,target_mmps,speedA_mmps,speedB_mmps,pwmA,pwmB
2050,0,100,98,102,80,85
2100,0,100,101,99,82,81
2150,1,200,198,202,160,165
```

## 🛠️ Hardware Setup

### ACC Car
- **Microcontroller**: ESP32-S3 Zero
- **Motor Driver**: TB6612FNG (dual channel)
- **Distance Sensor**: VL53L0X (ToF)
- **Motors**: 2× TT Motor with Encoder
- **Communication**: USB Serial → Raspberry Pi

### Lead Car
- **Microcontroller**: ESP32 or Arduino
- **Motor Driver**: TB6612FNG
- **Motors**: 2× TT Motor with Encoder
- **Communication**: WiFi TCP → Raspberry Pi

### Raspberry Pi
- **Device**: Raspberry Pi 5
- **Connections**: USB (ACC Car) + WiFi (Lead Car)
- **Display**: HDMI + VNC for remote access

## 📝 License
MIT License

## 👤 ผู้พัฒนา
- **ton5556** (GitHub: [@ton5556](https://github.com/ton5556))

---

**Version**: 2.1  
**Last Updated**: May 2, 2026  
**Repository**: [ton5556/Adaptive-Cruise-Control-ACC](https://github.com/ton5556/Adaptive-Cruise-Control-ACC)
