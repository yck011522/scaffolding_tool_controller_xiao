"""
Auto-sweep PWM duty cycle and log motor RPM + current draw.

Connects to the ESP32-S3 running test_ina_current firmware.
Sweeps PWM 0→100% in 5% steps, records RPM and INA240 current at each step.
Saves CSV and generates a dual-axis plot.

Usage:
    python sweep_current.py [COM_PORT] [GEAR_RATIO]
    python sweep_current.py COM5 56
"""

import sys
import time
import csv
import serial
import matplotlib.pyplot as plt

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
GEAR_RATIO = float(sys.argv[2]) if len(sys.argv) > 2 else 56.0
BAUD = 115200
TIMEOUT = 120


def send_cmd(ser, cmd):
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
    lines = []
    start = time.time()
    ser.timeout = 3.0
    step = 0
    while time.time() - start < TIMEOUT:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            continue
        lines.append(line)
        # Detect data lines (start with a step number)
        parts = line.split(",")
        if len(parts) == 6:
            try:
                int(parts[0])
                pwm = parts[1]
                rpm = parts[2]
                cur = parts[5]
                step += 1
                print(f"  Step {step:2d}/21 | PWM {pwm:>3s}% | RPM {rpm:>8s} | {cur:>7s} mA")
            except ValueError:
                pass
        if "Sweep complete" in line:
            print("Sweep complete!")
            break
    return lines


def parse_sweep(lines):
    """Extract (pwm%, motor_rpm, adc_raw, voltage_mv, current_ma) from sweep output."""
    data = []
    for line in lines:
        parts = line.split(",")
        # Format: step,PWM%,MotorRPM,ADC_raw,voltage_mV,current_mA
        if len(parts) == 6:
            try:
                int(parts[0])  # step number — validates this is a data line
                pwm = int(parts[1])
                motor_rpm = float(parts[2])
                adc_raw = int(parts[3])
                voltage_mv = float(parts[4])
                current_ma = float(parts[5])
                data.append((pwm, motor_rpm, adc_raw, voltage_mv, current_ma))
            except ValueError:
                continue
    return data


def main():
    print(f"Connecting to {PORT} at {BAUD} baud...")
    ser = serial.Serial(PORT, BAUD)
    time.sleep(2)
    ser.reset_input_buffer()

    # Set gear ratio
    send_cmd(ser, f"RATIO {GEAR_RATIO}")
    time.sleep(0.5)

    # Stop motor and ensure clean state
    send_cmd(ser, "STOP")
    time.sleep(1)

    # Run sweep
    print("Running PWM sweep 0→100% (5% steps, 2s each)...")
    ser.reset_input_buffer()
    ser.write(b"SWEEP\r\n")
    lines = wait_for_sweep(ser)

    data = parse_sweep(lines)
    send_cmd(ser, "STOP")
    ser.close()

    if not data:
        print("ERROR: No data parsed!")
        for line in lines:
            print(f"  RAW: {line}")
        return

    print(f"Got {len(data)} data points")

    pwms = [d[0] for d in data]
    motor_rpms = [d[1] for d in data]
    currents = [d[4] for d in data]

    # Print table
    print(f"\n{'PWM%':>5} {'MotorRPM':>9} {'mA':>8}")
    for i, d in enumerate(data):
        print(f"{pwms[i]:5d} {motor_rpms[i]:9.1f} {currents[i]:8.1f}")

    # Save CSV
    csv_path = f"sweep_current_ratio{int(GEAR_RATIO)}.csv"
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["pwm_percent", "motor_rpm", "current_ma", "adc_raw", "voltage_mv"])
        for i, d in enumerate(data):
            writer.writerow([pwms[i], motor_rpms[i], currents[i], d[2], d[3]])
    print(f"\nCSV saved: {csv_path}")

    # Plot: dual y-axis
    fig, ax1 = plt.subplots(figsize=(10, 6))
    ax2 = ax1.twinx()

    color_rpm = "#2196F3"
    color_current = "#F44336"

    ax1.plot(pwms, motor_rpms, "o-", color=color_rpm, markersize=5, label="Motor RPM")
    ax2.plot(pwms, currents, "s-", color=color_current, markersize=5, label="Current (mA)")

    ax1.set_xlabel("PWM Duty Cycle (%)")
    ax1.set_ylabel("Motor RPM", color=color_rpm)
    ax2.set_ylabel("Current Draw (mA)", color=color_current)

    ax1.tick_params(axis="y", labelcolor=color_rpm)
    ax2.tick_params(axis="y", labelcolor=color_current)

    ax1.set_xlim(0, 100)
    ax1.grid(True, alpha=0.3)

    # Combined legend
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper left")

    plt.title(f"Motor Speed & Current vs PWM — Gear Ratio {int(GEAR_RATIO)}:1")
    fig.tight_layout()

    img_path = f"sweep_current_ratio{int(GEAR_RATIO)}.png"
    fig.savefig(img_path, dpi=150)
    print(f"Plot saved: {img_path}")
    plt.show()


if __name__ == "__main__":
    main()
