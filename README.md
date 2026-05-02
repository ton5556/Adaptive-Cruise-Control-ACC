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

## 📝 License
อยากให้เพิ่มข้อมูลเพิ่มเติมสำหรับลอง

## 👤 ผู้พัฒนา
- ton5556

---

**Version**: 2.1  
**Last Updated**: May 2, 2026
