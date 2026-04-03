# Copilot Instructions — Scaffolding Tool Controller

## Project overview

Embedded firmware for a Waveshare ESP32-S3-Tiny controlling a
scaffolding assembly end-effector on a UR5e robot. The codebase contains
one main firmware image and many isolated component tests, each in its own
PlatformIO environment.

## Repository layout

```
platformio.ini          # All build environments
src/main.cpp            # Integrated firmware (not yet complete)
test/test_<name>/       # Isolated component tests (each is a standalone firmware)
  main.cpp              # Entry point
  *.cpp / *.h           # Supporting files
  results_*.md          # Test results log
scripts/                # Python helper scripts for debugging and verification
docs/                   # Hardware specs, protocol docs, motor datasheets
```

## Build system — PlatformIO

- The PlatformIO CLI lives at `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`.
  Always use the full path because `pio` is not on `$PATH`.
- Build:    `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e <env>`
- Upload:   `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e <env> --target upload --upload-port <COMn>`
- Monitor:  `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port <COMn> --baud 115200`
- Always `cd` to the project root first (where `platformio.ini` lives).
- The upload port changes depending on which USB port is used. Check
  available ports if the last known port fails.

## Autonomous debug loop

This is the core workflow for iterating on firmware without user
intervention at every step.  **Follow this loop if applicable when you modify
firmware and need to verify the result.**

### 1. Edit firmware

Make changes to the `.cpp` / `.h` files in `test/test_<name>/` or `src/`.

### 2. Build and upload

```powershell
cd <project_root>
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e <env> --target upload --upload-port <COMn>
```

A successful upload ends with `[SUCCESS]` and resets the board via RTS.

### 3. Wait for boot

The board needs a few seconds after upload to boot, connect to WiFi (if
applicable), and start printing serial output. Use `Start-Sleep -Seconds 8`
or similar before probing.

### 4. Verify with a Python script

Write a Python script to a file in `scripts/` with a descriptive name
(e.g. `scripts/verify_oled.py`, `scripts/rs485_roundtrip.py`) and run it
with `python scripts/<name>.py`.  **Do not use inline `python -c "..."`.**

The script should:

- **Resets the board** via serial RTS toggle if needed:
  ```python
  ser = serial.Serial(port, 115200, timeout=1)
  ser.dtr = False; ser.rts = False
  time.sleep(0.1)
  ser.rts = True; time.sleep(0.1); ser.rts = False
  ```
- **Captures serial output** in a background thread, looking for known
  markers (e.g. `"WiFi connected"`, `"Test complete"`).
- **Exercises the firmware** — HTTP requests, serial commands, ADC reads,
  whatever the test requires.
- **Prints structured results** prefixed with tags like `[SERIAL]`,
  `[CAPTURE]`, `[STREAM]`, `[OK]`, `[FAIL]` so the output is easy to parse.

Keep verification scripts in `scripts/` with descriptive names
(e.g. `verify_oled.py`, `rs485_roundtrip.py`).

### 5. Interpret and iterate

Read the script output. If the result is wrong:
- Check serial output for error messages or unexpected states.
- Modify the firmware, rebuild, upload, and re-verify.
- Repeat until the behaviour matches expectations.

**Do not ask the user to verify unless the issue requires physical
interaction** (plugging cables, pressing buttons, checking LEDs).

## mDNS for network devices

ESP32 firmware that uses WiFi should advertise an mDNS hostname
(e.g. `camera-one.local`) so Python scripts don't need hard-coded IP
addresses. Use `<ESPmDNS.h>` on the ESP32 side and plain
`socket.getaddrinfo("name.local", 80)` on the Python side.

## Hardware assumptions (Waveshare ESP32-S3-Tiny)

- ESP32-S3 dual-core LX7, 240 MHz.
- 18 GPIO pins exposed + TX/RX.
- No on-board camera — camera is a separate XIAO ESP32S3 Sense module.
- WiFi connects to the network configured in `main.cpp`.
- The board is the only hardware variant — remove any multi-board
  conditional compilation from stock examples.

## Coding style

- Keep firmware code minimal and direct. Remove dead code paths, unused
  ifdefs, and stock example boilerplate for other boards.
- Add brief comments on tunable parameters (JPEG quality, frame size, etc.)
  but don't over-comment obvious code.
- Python scripts should have a module docstring with description, usage, and
  pip requirements.
- Use `Serial.println("[TAG] message")` in firmware for structured debug
  output that scripts can pattern-match against.
- Save test results as `results_YYYY-MM-DD.md` in the test folder.

## Phone hotspot caveat

When using a phone hotspot as the WiFi network, non-standard ports (e.g.
81, 8080) may be silently blocked. Always serve HTTP on port 80.

## Python environment

A `.venv` virtualenv exists at the project root. Activate it before running
scripts. Common packages: `pyserial`, `opencv-python`, `numpy`, `matplotlib`.
