"""Camera debug script for XIAO ESP32S3 Sense.

Resets the board via serial RTS toggle, captures boot messages,
then tests both /capture (single JPEG) and /stream (MJPEG) endpoints
on port 80. Prints all serial output for diagnostics.

Usage:
    python camera_debug.py

Requires:
    pip install pyserial
"""
import serial
import time
import threading
import urllib.request

PORT = "COM20"
BAUD = 115200
STREAM_URL = "http://10.210.38.139/stream"
CAPTURE_URL = "http://10.210.38.139/capture"

lines = []
stop_event = threading.Event()

def read_serial(ser):
    while not stop_event.is_set():
        try:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='replace').rstrip()
                print(f"[SERIAL] {line}")
                lines.append(line)
            else:
                time.sleep(0.01)
        except Exception as e:
            print(f"[READ_ERR] {e}")
            break

# Open serial and toggle RTS to reset
ser = serial.Serial(PORT, BAUD, timeout=1)
ser.dtr = False
ser.rts = False
time.sleep(0.1)
ser.rts = True
time.sleep(0.1)
ser.rts = False
print("[INFO] Reset pulse sent, waiting for boot...")

# Start reader thread
reader = threading.Thread(target=read_serial, args=(ser,), daemon=True)
reader.start()

# Wait for WiFi to connect (up to 30s)
deadline = time.time() + 30
while time.time() < deadline:
    if any("Camera Ready" in l for l in lines):
        break
    time.sleep(0.5)

print(f"\n[INFO] Collected {len(lines)} boot lines")
print("[INFO] Now trying to fetch /capture ...")

try:
    req = urllib.request.urlopen(CAPTURE_URL, timeout=10)
    print(f"[CAPTURE] Status: {req.status}, Content-Type: {req.headers.get('Content-Type')}, Length: {len(req.read())}")
except Exception as e:
    print(f"[CAPTURE] Error: {e}")

time.sleep(1)
# Check serial for any new messages after capture
print(f"\n[INFO] Lines after capture:")
for l in lines[-5:]:
    print(f"  {l}")

print(f"\n[INFO] Now trying to fetch :81/stream (5s timeout)...")
try:
    req = urllib.request.urlopen(STREAM_URL, timeout=5)
    data = req.read(4096)
    print(f"[STREAM] Status: {req.status}, Got {len(data)} bytes")
    print(f"[STREAM] First 200 bytes: {data[:200]}")
except Exception as e:
    print(f"[STREAM] Error: {e}")

time.sleep(2)
print(f"\n[INFO] All serial output after stream attempt:")
for l in lines:
    print(f"  {l}")

stop_event.set()
ser.close()
print("\n[DONE]")
