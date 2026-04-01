"""
RS-485 round-trip latency measurement.

Sends payloads of increasing size to the ESP32 (echo mode enabled)
via the USB-RS485 adapter and measures the round-trip time.

Prerequisites:
  - ESP32 running test_rs485 firmware with ECHO mode ON
  - USB-RS485 adapter connected to the same bus

Usage:
    python rs485_roundtrip.py [RS485_COM_PORT]
    python rs485_roundtrip.py COM3
"""

import sys
import time
import csv
import serial
import matplotlib.pyplot as plt

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
BAUD = 115200
TRIALS = 50           # repeats per payload size
TIMEOUT_S = 2.0       # max wait for echo reply
PAYLOAD_SIZES = [1, 2, 4, 8, 16, 32, 64, 100, 128, 200, 256]


def measure_roundtrip(ser, payload_size):
    """Send a payload and measure time until the full echo is received."""
    # Build a deterministic payload (printable ASCII, no CR/LF)
    payload = ("A" * payload_size)
    msg = payload + "\r\n"

    ser.reset_input_buffer()
    start = time.perf_counter()
    ser.write(msg.encode())

    # Read until we get back the full payload + CR/LF, or timeout
    received = b""
    while time.perf_counter() - start < TIMEOUT_S:
        if ser.in_waiting:
            received += ser.read(ser.in_waiting)
            # Check if we got at least the payload length back
            # Echo comes back as: payload + \r\n
            if len(received) >= payload_size:
                break
        else:
            time.sleep(0.0005)  # 0.5 ms poll interval

    elapsed = time.perf_counter() - start

    # Verify content
    text = received.decode(errors="replace").strip()
    ok = text.startswith(payload[:min(len(text), payload_size)])

    return elapsed * 1000.0, len(received), ok  # ms


def main():
    print(f"Connecting to USB-RS485 adapter on {PORT} at {BAUD} baud...")
    print(f"Make sure ESP32 echo mode is ON.\n")

    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(0.5)
    ser.reset_input_buffer()
    time.sleep(0.5)

    results = []

    for size in PAYLOAD_SIZES:
        times = []
        errors = 0
        print(f"  Payload {size:>4d} bytes: ", end="", flush=True)

        for trial in range(TRIALS):
            rtt_ms, rx_len, ok = measure_roundtrip(ser, size)
            if ok:
                times.append(rtt_ms)
                print(".", end="", flush=True)
            else:
                errors += 1
                print("x", end="", flush=True)
            # Small gap between trials to let buffers drain
            time.sleep(0.05)

        if times:
            avg = sum(times) / len(times)
            mn = min(times)
            mx = max(times)
            print(f"  avg={avg:.1f} ms  min={mn:.1f}  max={mx:.1f}  errors={errors}")
            results.append((size, avg, mn, mx, errors))
        else:
            print(f"  ALL FAILED ({errors} errors)")
            results.append((size, 0, 0, 0, errors))

    ser.close()

    if not results:
        print("No data collected!")
        return

    # Print summary table
    print(f"\n{'Size':>6} {'Avg ms':>8} {'Min ms':>8} {'Max ms':>8} {'Errors':>7}")
    for r in results:
        print(f"{r[0]:6d} {r[1]:8.1f} {r[2]:8.1f} {r[3]:8.1f} {r[4]:7d}")

    # Theoretical minimum: byte_time = 10 bits / baud, round trip = 2× payload
    byte_time_ms = 10.0 / BAUD * 1000.0
    print(f"\nTheoretical byte time at {BAUD} baud: {byte_time_ms:.4f} ms")
    print(f"Theoretical min RTT for N bytes: 2 × N × {byte_time_ms:.4f} ms (wire time only)")

    # Save CSV
    csv_path = "rs485_roundtrip.csv"
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["payload_bytes", "avg_ms", "min_ms", "max_ms", "errors"])
        for r in results:
            writer.writerow(r)
    print(f"\nCSV saved: {csv_path}")

    # Plot
    sizes = [r[0] for r in results if r[1] > 0]
    avgs = [r[1] for r in results if r[1] > 0]
    mins = [r[2] for r in results if r[1] > 0]
    maxs = [r[3] for r in results if r[1] > 0]
    theoretical = [2 * s * byte_time_ms for s in sizes]

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(sizes, avgs, "o-", color="#2196F3", markersize=6, label="Avg RTT")
    ax.fill_between(sizes, mins, maxs, alpha=0.2, color="#2196F3", label="Min–Max range")
    ax.plot(sizes, theoretical, "--", color="#999999", label=f"Theoretical wire time ({BAUD} baud)")

    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("Round-Trip Time (ms)")
    ax.set_title("RS-485 Round-Trip Latency vs Payload Size")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()

    img_path = "rs485_roundtrip.png"
    fig.savefig(img_path, dpi=150)
    print(f"Plot saved: {img_path}")
    plt.show()


if __name__ == "__main__":
    main()
