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
BUFFER_LEN   = 300              # จำนวนจุดที่แสดงในกราฟ (300 × 50ms = 15 วิ)
LOG_DIR      = os.path.expanduser("~/acc_logs")

# ===== SHARED DATA (thread-safe deque) =====
t_acc, dist_acc, spd_acc, tgt_acc           = deque(maxlen=BUFFER_LEN), deque(maxlen=BUFFER_LEN), deque(maxlen=BUFFER_LEN), deque(maxlen=BUFFER_LEN)
t_lead, spd_lead, tgt_lead, phase_lead      = deque(maxlen=BUFFER_LEN), deque(maxlen=BUFFER_LEN), deque(maxlen=BUFFER_LEN), deque(maxlen=BUFFER_LEN)
lock = threading.Lock()

# ===== LOG FILES =====
os.makedirs(LOG_DIR, exist_ok=True)
ts = datetime.now().strftime("%Y%m%d_%H%M%S")
log_acc_path  = os.path.join(LOG_DIR, f"acc_{ts}.csv")
log_lead_path = os.path.join(LOG_DIR, f"lead_{ts}.csv")

log_acc  = open(log_acc_path,  'w', newline='')
log_lead = open(log_lead_path, 'w', newline='')

writer_acc  = csv.writer(log_acc)
writer_lead = csv.writer(log_lead)
writer_acc.writerow(["time_ms","dist_mm","target_mmps","speedA_mmps","speedB_mmps","pwmA","pwmB"])
writer_lead.writerow(["time_ms","phase","target_mmps","speedA_mmps","speedB_mmps","pwmA","pwmB"])

print(f"[LOG] ACC  → {log_acc_path}")
print(f"[LOG] Lead → {log_lead_path}")

# ===== THREAD: ACC Car (USB Serial) =====
def thread_acc():
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=1)
            print(f"[ACC] Serial connected: {SERIAL_PORT}")
            for line in ser:
                try:
                    line = line.decode('utf-8', errors='ignore').strip()
                    if line.startswith("HEADER") or line.startswith("ERROR"):
                        print(f"[ACC] {line}")
                        continue
                    parts = line.split(',')
                    if len(parts) < 7:
                        continue
                    t_ms, d, tgt, spA, spB, pA, pB = (float(x) for x in parts[:7])
                    spd_avg = (spA + spB) / 2.0
                    with lock:
                        t_acc.append(t_ms / 1000.0)
                        dist_acc.append(d)
                        tgt_acc.append(tgt)
                        spd_acc.append(spd_avg)
                    writer_acc.writerow(parts[:7])
                    log_acc.flush()
                except Exception:
                    pass
        except serial.SerialException as e:
            print(f"[ACC] Serial error: {e} — retry in 3s")
            time.sleep(3)

# ===== THREAD: Lead Car (TCP) =====
def thread_lead():
    while True:
        try:
            srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind((TCP_HOST, TCP_PORT))
            srv.listen(1)
            print(f"[Lead] TCP waiting on port {TCP_PORT}...")
            conn, addr = srv.accept()
            print(f"[Lead] Connected from {addr}")
            buf = ""
            while True:
                data = conn.recv(256).decode('utf-8', errors='ignore')
                if not data:
                    break
                buf += data
                while '\n' in buf:
                    line, buf = buf.split('\n', 1)
                    line = line.strip()
                    if not line or line.startswith("HEADER"):
                        continue
                    try:
                        parts = line.split(',')
                        if len(parts) < 7:
                            continue
                        t_ms, ph, tgt, spA, spB, pA, pB = (float(x) for x in parts[:7])
                        spd_avg = (spA + spB) / 2.0
                        with lock:
                            t_lead.append(t_ms / 1000.0)
                            spd_lead.append(spd_avg)
                            tgt_lead.append(tgt)
                            phase_lead.append(ph)
                        writer_lead.writerow(parts[:7])
                        log_lead.flush()
                    except Exception:
                        pass
            conn.close()
            srv.close()
        except Exception as e:
            print(f"[Lead] TCP error: {e} — retry in 3s")
            time.sleep(3)

# ===== REAL-TIME PLOT =====
fig, axes = plt.subplots(3, 1, figsize=(12, 9))
fig.patch.set_facecolor('#0d0d0d')
fig.suptitle("ACC System Monitor — Real-time", color='#e0e0e0', fontsize=14, fontweight='bold')

for ax in axes:
    ax.set_facecolor('#1a1a2e')
    ax.tick_params(colors='#aaaaaa')
    ax.spines['bottom'].set_color('#444')
    ax.spines['left'].set_color('#444')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.yaxis.label.set_color('#cccccc')
    ax.xaxis.label.set_color('#cccccc')

ax1, ax2, ax3 = axes

# กราฟ 1: Distance (ACC ← Lead)
ax1.set_ylabel("Distance (mm)")
ax1.set_title("ACC ↔ Lead Distance", color='#cccccc', fontsize=10)
line_dist,     = ax1.plot([], [], color='#00d4ff', lw=1.5, label='Measured dist')
line_dist_tgt  = ax1.axhline(200, color='#ff6b35', lw=1, ls='--', label='Target 200mm')
ax1.legend(loc='upper right', facecolor='#1a1a2e', edgecolor='#444', labelcolor='#ccc', fontsize=8)

# กราฟ 2: Speed comparison
ax2.set_ylabel("Speed (mm/s)")
ax2.set_title("Speed: Lead vs ACC", color='#cccccc', fontsize=10)
line_spd_lead, = ax2.plot([], [], color='#f4c542', lw=1.5, label='Lead speed')
line_spd_acc,  = ax2.plot([], [], color='#4dff91', lw=1.5, label='ACC speed')
line_tgt_acc,  = ax2.plot([], [], color='#4dff91', lw=0.8, ls=':', alpha=0.6, label='ACC target')
line_tgt_lead, = ax2.plot([], [], color='#f4c542', lw=0.8, ls=':', alpha=0.6, label='Lead target')
ax2.legend(loc='upper right', facecolor='#1a1a2e', edgecolor='#444', labelcolor='#ccc', fontsize=8)

# กราฟ 3: Lead Car Phase
ax3.set_ylabel("Lead Phase (0-6)")
ax3.set_xlabel("Time (s)")
ax3.set_title("Lead Car Pattern Phase", color='#cccccc', fontsize=10)
line_phase,    = ax3.plot([], [], color='#c084fc', lw=1.5)
ax3.set_ylim(-0.5, 6.5)
phase_labels = {0:'HOLD_SLOW', 1:'RAMP↑MED', 2:'HOLD_MED', 3:'RAMP↑FAST', 4:'HOLD_FAST', 5:'RAMP↓MED', 6:'RAMP↓SLOW'}
ax3.set_yticks(range(7))
ax3.set_yticklabels([phase_labels[i] for i in range(7)], fontsize=7)

plt.tight_layout(rect=[0, 0, 1, 0.96])

def animate(_):
    with lock:
        # ===== Graph 1: Distance =====
        if len(t_acc) > 1:
            ta = list(t_acc)
            t0 = ta[0]
            ta = [x - t0 for x in ta]
            line_dist.set_data(ta, list(dist_acc))
            ax1.set_xlim(max(0, ta[-1] - 15), ta[-1] + 0.5)
            dvals = list(dist_acc)
            if dvals:
                ax1.set_ylim(max(0, min(dvals) - 50), max(dvals) + 50)

        # ===== Graph 2: Speed =====
        has_lead = len(t_lead) > 1
        has_acc  = len(t_acc)  > 1
        t_ref = 0
        if has_lead:
            tl = list(t_lead)
            t_ref = tl[0]
            tl = [x - t_ref for x in tl]
            line_spd_lead.set_data(tl, list(spd_lead))
            line_tgt_lead.set_data(tl, list(tgt_lead))
        if has_acc:
            ta = list(t_acc)
            ta = [x - ta[0] for x in ta]
            line_spd_acc.set_data(ta, list(spd_acc))
            line_tgt_acc.set_data(ta, list(tgt_acc))
        if has_lead or has_acc:
            all_t = (tl if has_lead else []) + (ta if has_acc else [])
            all_s = list(spd_lead) + list(spd_acc)
            if all_t:
                ax2.set_xlim(max(0, max(all_t) - 15), max(all_t) + 0.5)
            if all_s:
                ax2.set_ylim(max(0, min(all_s) - 20), max(all_s) + 20)

        # ===== Graph 3: Phase =====
        if len(t_lead) > 1:
            tl = list(t_lead)
            tl = [x - tl[0] for x in tl]
            line_phase.set_data(tl, list(phase_lead))
            ax3.set_xlim(max(0, tl[-1] - 15), tl[-1] + 0.5)

    return line_dist, line_spd_lead, line_spd_acc, line_tgt_acc, line_tgt_lead, line_phase

# ===== START =====
if __name__ == "__main__":
    t1 = threading.Thread(target=thread_acc,  daemon=True)
    t2 = threading.Thread(target=thread_lead, daemon=True)
    t1.start()
    t2.start()

    ani = animation.FuncAnimation(fig, animate, interval=200, blit=True, cache_frame_data=False)

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        log_acc.close()
        log_lead.close()
        print(f"\n[LOG] Saved:\n  {log_acc_path}\n  {log_lead_path}")
