"""Live MJPEG stream viewer for XIAO ESP32S3 Sense camera.

Connects to the ESP32 camera's /stream endpoint over HTTP and
displays frames in an OpenCV window. Press 'q' to quit.

Usage:
    python camera_viewer.py

Requires:
    pip install opencv-python numpy
"""
import cv2
import urllib.request
import numpy as np

STREAM_URL = "http://10.210.38.139/stream"

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
