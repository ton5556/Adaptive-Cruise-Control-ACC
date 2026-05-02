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
from matplotlib.patches import FancyArrowPatch

LOG_DIR = os.path.expanduser("~/acc_logs")

# ===== หาไฟล์ล่าสุด หรือรับ argument =====
if len(sys.argv) == 3:
    acc_file  = sys.argv[1]
    lead_file = sys.argv[2]
else:
    acc_files  = sorted(glob.glob(os.path.join(LOG_DIR, "acc_*.csv")))
    lead_files = sorted(glob.glob(os.path.join(LOG_DIR, "lead_*.csv")))
    if not acc_files or not lead_files:
        print(f"[ERROR] ไม่พบ log ใน {LOG_DIR}")
        sys.exit(1)
    acc_file  = acc_files[-1]
    lead_file = lead_files[-1]

print(f"[PLOT] ACC : {acc_file}")
print(f"[PLOT] Lead: {lead_file}")

# ===== LOAD =====
df_acc  = pd.read_csv(acc_file)
df_lead = pd.read_csv(lead_file)

# แปลงเวลาให้เริ่มที่ 0
df_acc['t']  = (df_acc['time_ms']  - df_acc['time_ms'].iloc[0])  / 1000.0
df_lead['t'] = (df_lead['time_ms'] - df_lead['time_ms'].iloc[0]) / 1000.0

# speed avg
df_acc['speed_avg']  = (df_acc['speedA_mmps']  + df_acc['speedB_mmps'])  / 2
df_lead['speed_avg'] = (df_lead['speedA_mmps'] + df_lead['speedB_mmps']) / 2

# ===== STATS =====
dist_mean = df_acc['dist_mm'].mean()
dist_std  = df_acc['dist_mm'].std()
dist_err  = (df_acc['dist_mm'] - 200).abs().mean()
print(f"\n===== ACC Performance =====")
print(f"  Distance mean  : {dist_mean:.1f} mm  (target 200 mm)")
print(f"  Distance std   : {dist_std:.1f} mm")
print(f"  Mean abs error : {dist_err:.1f} mm")
print(f"  Speed range    : {df_acc['speed_avg'].min():.0f} – {df_acc['speed_avg'].max():.0f} mm/s")

# ===== PLOT =====
plt.style.use('dark_background')
fig = plt.figure(figsize=(14, 10))
fig.patch.set_facecolor('#0d0d0d')
gs  = gridspec.GridSpec(3, 2, figure=fig, hspace=0.45, wspace=0.35)

COLOR_LEAD = '#f4c542'
COLOR_ACC  = '#4dff91'
COLOR_DIST = '#00d4ff'
COLOR_ERR  = '#ff6b35'
COLOR_PH   = '#c084fc'

def style_ax(ax, title):
    ax.set_facecolor('#1a1a2e')
    ax.set_title(title, color='#cccccc', fontsize=10, pad=6)
    ax.tick_params(colors='#aaaaaa', labelsize=8)
    for spine in ['top','right']: ax.spines[spine].set_visible(False)
    for spine in ['bottom','left']: ax.spines[spine].set_color('#444')

# --- 1. Distance ---
ax1 = fig.add_subplot(gs[0, :])
ax1.plot(df_acc['t'], df_acc['dist_mm'], color=COLOR_DIST, lw=1.2, label='Measured dist')
ax1.axhline(200, color=COLOR_ERR, lw=1, ls='--', label='Target 200 mm')
ax1.fill_between(df_acc['t'], 180, 220, alpha=0.1, color=COLOR_ERR, label='±20 mm band')
ax1.set_ylabel("Distance (mm)", color='#ccc')
ax1.legend(loc='upper right', facecolor='#1a1a2e', edgecolor='#444', labelcolor='#ccc', fontsize=8)
style_ax(ax1, "ACC Gap Distance")

# --- 2. Speed comparison ---
ax2 = fig.add_subplot(gs[1, :])
ax2.plot(df_lead['t'], df_lead['speed_avg'], color=COLOR_LEAD, lw=1.2, label='Lead speed')
ax2.plot(df_acc['t'],  df_acc['speed_avg'],  color=COLOR_ACC,  lw=1.2, label='ACC speed')
ax2.plot(df_acc['t'],  df_acc['target_mmps'], color=COLOR_ACC, lw=0.8, ls=':', alpha=0.5, label='ACC target')
ax2.set_ylabel("Speed (mm/s)", color='#ccc')
ax2.legend(loc='upper right', facecolor='#1a1a2e', edgecolor='#444', labelcolor='#ccc', fontsize=8)
style_ax(ax2, "Speed: Lead vs ACC")

# --- 3. Lead Phase ---
ax3 = fig.add_subplot(gs[2, 0])
phase_labels = ['HOLD_S','RMP↑M','HOLD_M','RMP↑F','HOLD_F','RMP↓M','RMP↓S']
ax3.step(df_lead['t'], df_lead['phase'], color=COLOR_PH, lw=1.2, where='post')
ax3.set_yticks(range(7))
ax3.set_yticklabels(phase_labels, fontsize=7)
ax3.set_xlabel("Time (s)", color='#ccc')
ax3.set_ylabel("Phase", color='#ccc')
style_ax(ax3, "Lead Car Pattern Phase")

# --- 4. Distance Error Histogram ---
ax4 = fig.add_subplot(gs[2, 1])
err = df_acc['dist_mm'] - 200
ax4.hist(err, bins=40, color=COLOR_DIST, alpha=0.8, edgecolor='none')
ax4.axvline(0, color=COLOR_ERR, lw=1.5, ls='--')
ax4.set_xlabel("Distance Error (mm)", color='#ccc')
ax4.set_ylabel("Count", color='#ccc')
ax4.text(0.98, 0.95, f"μ={err.mean():.1f}\nσ={err.std():.1f}",
         transform=ax4.transAxes, ha='right', va='top',
         color='#fff', fontsize=9,
         bbox=dict(boxstyle='round,pad=0.3', facecolor='#333', edgecolor='#555'))
style_ax(ax4, "Gap Error Distribution")

fig.suptitle("ACC System — Post-run Analysis", color='#e0e0e0', fontsize=13, fontweight='bold', y=0.98)

# Save
out_path = acc_file.replace("acc_", "analysis_").replace(".csv", ".png")
plt.savefig(out_path, dpi=150, bbox_inches='tight', facecolor=fig.get_facecolor())
print(f"[PLOT] Saved → {out_path}")
plt.show()
