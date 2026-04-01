"""
Auto-sweep PWM frequency and duty cycle for BLDC feedback test.

Connects to the XIAO ESP32-S3 running test_bldc_feedback firmware,
sweeps PWM frequencies from 5 kHz to 25 kHz, and at each frequency
runs a full duty cycle sweep (0-100%). Saves results to CSV and
generates a response plot.

Usage:
    .venv\Scripts\activate   
    cd test\test_bldc_feedback

    python sweep_freq.py [COM_PORT]
    python sweep_freq.py COM5
    python sweep_freq.py COM5 56
"""

import sys
import time
import csv
import serial
import matplotlib.pyplot as plt

# ── Settings ─────────────────────────────────────────────────────────
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = 115200
FREQUENCIES = [5000, 10000, 15000, 20000, 25000]
GEAR_RATIO = sys.argv[2] if len(sys.argv) > 2 else "56"
TIMEOUT = 120  # seconds max per sweep

def send_cmd(ser, cmd):
    """Send a command and return all lines until no more data for 0.5s."""
    ser.reset_input_buffer()
    ser.write(f"{cmd}\r\n".encode())
    time.sleep(0.3)
    lines = []
    ser.timeout = 0.5
    while True:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            break
        lines.append(line)
    return lines

def wait_for_sweep(ser):
    """Read lines until 'Sweep complete' or timeout."""
    lines = []
    start = time.time()
    ser.timeout = 5.0  # each step takes ~2s, give margin
    while time.time() - start < TIMEOUT:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            continue
        lines.append(line)
        print(f"    > {line}")
        if "Sweep complete" in line:
            break
    return lines

def parse_sweep(lines):
    """Extract (pwm_percent, motor_rpm, shaft_rpm) from sweep output."""
    data = []
    for line in lines:
        parts = line.split(",")
        # Format: step,PWM%,MotorRPM,ShaftRPM
        if len(parts) == 4:
            try:
                step = int(parts[0])
                pwm = int(parts[1])
                motor_rpm = float(parts[2])
                shaft_rpm = float(parts[3])
                data.append((pwm, motor_rpm, shaft_rpm))
            except ValueError:
                continue
    return data

def main():
    print(f"Connecting to {PORT} at {BAUD} baud...")
    # Open without resetting the board (disable DTR/RTS)
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.dtr = False
    ser.rts = False
    ser.open()
    time.sleep(1)
    ser.reset_input_buffer()

    # Verify connection with a STATUS command
    print("Verifying connection...")
    resp = send_cmd(ser, "STATUS")
    for line in resp:
        print(f"  {line}")
    if not resp:
        print("WARNING: No response from board. Check connection.")

    # Set gear ratio
    send_cmd(ser, f"RATIO {GEAR_RATIO}")
    time.sleep(0.5)

    all_results = {}

    for freq in FREQUENCIES:
        print(f"\n── Frequency: {freq} Hz ──")

        # Stop motor, change frequency
        send_cmd(ser, "STOP")
        time.sleep(1)
        send_cmd(ser, f"FREQ {freq}")
        time.sleep(0.5)

        # Run sweep
        ser.reset_input_buffer()
        ser.write(b"SWEEP\r\n")
        lines = wait_for_sweep(ser)

        data = parse_sweep(lines)
        all_results[freq] = data

        if data:
            print(f"  Got {len(data)} data points")
            for pwm, mrpm, srpm in data:
                print(f"    PWM {pwm:3d}% → Motor {mrpm:7.1f} RPM, Shaft {srpm:6.1f} RPM")
        else:
            print("  WARNING: No data parsed!")
            for line in lines:
                print(f"    RAW: {line}")

        # Stop motor between sweeps
        send_cmd(ser, "STOP")
        time.sleep(2)

    ser.close()

    # ── Save CSV ─────────────────────────────────────────────────────
    csv_path = f"sweep_freq_ratio{GEAR_RATIO}.csv"
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["freq_hz", "pwm_percent", "motor_rpm", "shaft_rpm"])
        for freq, data in all_results.items():
            for pwm, mrpm, srpm in data:
                writer.writerow([freq, pwm, mrpm, srpm])
    print(f"\nCSV saved: {csv_path}")

    # ── Plot ─────────────────────────────────────────────────────────
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    for freq, data in all_results.items():
        if not data:
            continue
        pwms = [d[0] for d in data]
        motor_rpms = [d[1] for d in data]
        shaft_rpms = [d[2] for d in data]
        ax1.plot(pwms, motor_rpms, "o-", label=f"{freq} Hz", markersize=4)
        ax2.plot(pwms, shaft_rpms, "o-", label=f"{freq} Hz", markersize=4)

    ax1.set_xlabel("PWM Duty Cycle (%)")
    ax1.set_ylabel("Motor RPM (before gearbox)")
    ax1.set_title(f"Motor RPM vs PWM — Gear Ratio {GEAR_RATIO}:1")
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xlim(0, 100)

    ax2.set_xlabel("PWM Duty Cycle (%)")
    ax2.set_ylabel("Output Shaft RPM")
    ax2.set_title(f"Shaft RPM vs PWM — Gear Ratio {GEAR_RATIO}:1")
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.set_xlim(0, 100)

    fig.tight_layout()
    img_path = f"sweep_freq_ratio{GEAR_RATIO}.png"
    fig.savefig(img_path, dpi=150)
    print(f"Plot saved: {img_path}")
    plt.show()

if __name__ == "__main__":
    main()
