"""Live MJPEG stream viewer for XIAO ESP32S3 Sense camera.

Connects to the ESP32 camera's /stream endpoint over HTTP and
displays frames in an OpenCV window. Press 'q' to quit.

The camera is discovered via mDNS hostname (camera-one.local).
If mDNS resolution fails within 5 seconds, the script exits.

Usage:
    python camera_viewer.py

Requires:
    pip install opencv-python numpy
""" 
import cv2
import urllib.request
import numpy as np
import socket
import sys

CAMERA_HOST = "camera-one.local"
STREAM_URL = f"http://{CAMERA_HOST}/stream"

print(f"Resolving {CAMERA_HOST}...")
try:
    ip = socket.getaddrinfo(CAMERA_HOST, 80, socket.AF_INET, socket.SOCK_STREAM, 0, 0)
    print(f"Resolved to {ip[0][4][0]}")
except socket.gaierror:
    print(f"ERROR: Could not resolve {CAMERA_HOST} — is the camera on this network?")
    sys.exit(1)

print("Connecting to stream...")
stream = urllib.request.urlopen(STREAM_URL, timeout=10)
print(f"Connected! Status: {stream.status}")

buf = b''
while True:
    buf += stream.read(4096)
    # Find JPEG start and end markers
    start = buf.find(b'\xff\xd8')
    end = buf.find(b'\xff\xd9')
    if start != -1 and end != -1 and end > start:
        jpg = buf[start:end+2]
        buf = buf[end+2:]
        frame = cv2.imdecode(np.frombuffer(jpg, dtype=np.uint8), cv2.IMREAD_COLOR)
        if frame is not None:
            cv2.imshow("XIAO Camera Stream", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

stream.close()
cv2.destroyAllWindows()
