# Hardware Specifications

Ground-truth reference for component choices, voltage levels, and interface details.

## Microcontroller

| Parameter | Value |
|---|---|
| Board | Waveshare ESP32-S3-Tiny |
| MCU | ESP32-S3 (dual-core Xtensa LX7, 240 MHz) |
| Flash | 4 MB (ESP32-S3FH4R2) |
| PSRAM | 2 MB (ESP32-S3FH4R2) |
| GPIO voltage | 3.3 V |
| ADC | 12-bit, ADC1 channels (ADC2 conflicts with Wi-Fi) |
| GPIO count | 18 exposed + TX/RX |
| USB | USB-C (native USB on ESP32-S3) — used for programming and debug serial |

### USB-CDC Bootloader Reboot Frozen Problem

The ESP32-S3 uses its **native USB peripheral** for Serial (no separate UART chip). The Arduino-ESP32 USB-CDC driver has a software callback that watches for DTR/RTS line-state transitions. When a serial monitor **closes the port**, and **if** the host sends DTR=0/RTS=0, the driver will interpret this as a request to reboot into **ROM download mode** (the same mechanism that enables PlatformIO one-click upload).

**What happens:**

1. Serial monitor disconnects → host drops DTR/RTS
2. USB-CDC driver calls `usb_persist_restart(RESTART_BOOTLOADER)`
3. A flag is set in RTC memory; the chip does a software reset
4. ROM code reads the flag → enters USB download mode instead of running user firmware
5. LEDC (PWM) is not initialized → motor control pin **floats** → BLDC driver may interpret this as full throttle
6. Reconnecting a serial monitor talks to the ROM bootloader, not user firmware → **commands are ignored**
7. Recovery requires a **physical press of the reset button**

**When this is a problem:**
- Debugging via USB serial (PlatformIO monitor, PuTTY, etc.) with DTR/RTS enabled (the default for most tools)
- Disconnecting and reconnecting the serial monitor during testing

**When this is NOT a problem:**
- Normal robotic operation via **RS-485** — the MAX3485 transceiver uses UART1, which has no DTR/RTS mechanism. Connecting/disconnecting the RS-485 bus cannot trigger a bootloader reboot.
- PlatformIO upload — this is the intentional use case for the DTR/RTS reset behavior.

**Workaround for debugging:**
- When debugging from serial monitor, **disable DTR and RTS** before connecting (most tools have a checkbox or config option for this).
- if using PlatformIO serial monitor, add `monitor_dtr = 0` and `monitor_rts = 0` to the relevant `[env]` in `platformio.ini`.
- In pyserial (Python scripts), open the port with `dsrdtr=False, rtscts=False` (the default), and avoid toggling `ser.dtr` or `ser.rts`.

### Pin Map

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 1 | Button 1 | Input (pull-up) | Momentary switch, active LOW |
| 2 | Button 2 | Input (pull-up) | Momentary switch, active LOW |
| 3 | Button 3 | Input (pull-up) | Momentary switch, active LOW |
| 4 | Button 4 | Input (pull-up) | Momentary switch, active LOW |
| 5 | Motor 1 DIR (gripper) | Output | Direction control |
| 6 | Motor 1 PWM (gripper) | Output | Speed control (LEDC) + 4.7kΩ pull-down |
| 7 | Motor 1 CAP (gripper) | Input | Encoder feedback via voltage divider (5V→3.3V) |
| 9 | Motor 2 DIR (tightening) | Output | Direction control |
| 10 | Motor 2 PWM (tightening) | Output | Speed control (LEDC) + 4.7kΩ pull-down |
| 11 | Motor 2 CAP (tightening) | Input | Encoder feedback via voltage divider (5V→3.3V) |
| 13 | INA240 current sense | Input (ADC) | Analog current measurement |
| 15 | I2C SDA | Bidir | OLED display |
| 16 | I2C SCL | Output | OLED display |
| 18 | RS-485 DE/RE | Output | MAX3485 direction control |
| 43 | RS-485 TX (board header) | Output | MAX3485 DI (driver input) |
| 44 | RS-485 RX (board header) | Input | MAX3485 RO (receiver output) |

## BLDC Motor — JGB37-3650-2480

| Parameter | Value |
|---|---|
| Rated voltage | 24 V |
| No-load current | 0.25 A |
| Rated load current | 1 A |
| Stall current | ~5 A |
| Feedback | 6 pulses per motor revolution (before gearbox) |
| Wires | 6: Red (+24V), Black (GND), Blue (PWM in), White (DIR), Yellow (CAP out), + emergency stop |
| PWM input | 0–5 V range; 3.3 V → full speed; GND → stop; dead band ~1.2 V |
| CAP output voltage | ~5.2 V peak → **needs voltage divider** to 3.3 V for ESP32 |
| Available gearbox ratios | 6.25, 10, 18.8, 30, 56, 70, 90, 131, 169, 210, 270, 394, 506, 630, 810 |
| Testing ratios | 4 ratios TBD |

### No-load current by gearbox ratio (bench power supply)

Measured from the 24 V bench power supply with no external mechanical load attached.

| Gearbox ratio | No-load current |
|---|---|
| 1:56 | 0.17 A |
| 1:90 | 0.25 A |
| 1:131 | 0.25 A |
| 1:169 | 0.20 A |

Motor can become quite warm after 5 to 10 mins of continuous operation.
See `docs/motor_spec/` for full datasheet CSV and wiring diagrams.

### Motor PWM Observations (from DC bench test, 56:1 ratio)

| PWM input voltage | CAP frequency | Notes |
|---|---|---|
| 3.3 V (or open) | 830 Hz | Full speed |
| 2.5 V | 600 Hz | |
| 1.5 V | 140 Hz | Very slow |
| ≤ 1.2 V | 0 Hz | Motor stops, CAP held at 5.2 V |

## RS-485 Transceiver

| Parameter | Value |
|---|---|
| Chip | MAX3485 |
| Supply voltage | 3.3 V |
| Interface | UART TX/RX + DE/RE direction control |
| TX pin | GPIO43 (board TX header) → MAX3485 DI (RDX on Module PCB)|
| RX pin | GPIO44 (board RX header) → MAX3485 RO (TDX on Module PCB)|
| DE/RE pin | GPIO18 → MAX3485 DE + ~RE (tied together) |
| Baud rate | 115200 (default) |

> **Label confusion:** Many MAX3485 breakout modules label the pins "TXD" and "RXD" from the module's perspective. "TXD" on the module is the receiver output (RO) — connect it to the MCU's **RX** pin (GPIO44). "RXD" on the module is the driver input (DI) — connect it to the MCU's **TX** pin (GPIO43). In short: module TXD → MCU RX, module RXD → MCU TX.

## Current Sensor — INA240 (preferred)

| Parameter | Value |
|---|---|
| Type | High-side current-sense amplifier |
| Output | Analog voltage (read with ESP32 ADC) |
| Common-mode range | Up to 80 V (suitable for 24 V rail) |
| Gain variants | A1 (×20), A2 (×50), A3 (×100), A4 (×200) — TBD which variant |
| ADC pin | GPIO13 |

Fallback: INA219 (I2C, shares bus with OLED) — use if pin budget is too tight.

## OLED Display

| Parameter | Value |
|---|---|
| Size | 0.96″ |
| Driver | SSD1306 |
| Interface | I2C (SDA/SCL) |
| Resolution | 128 × 64 pixels (full framebuffer, no offset) |
| I2C address | 0x3C (7-bit) / 0x78 (8-bit, module selector) |
| Supply voltage | 3.3 V (only) |
| Wiring | SDA → GPIO15, SCL → GPIO16 |

### Display notes

The 0.96″ module uses the full 128×64 SSD1306 framebuffer. No cursor offset is needed — drawing starts at (0, 0). This replaces the previous 0.66″ module which had a 64×48 visible window at offset (32, 16).

### Text size reference (128×64 full screen)

| textSize | Char px | Chars/line | Lines | Notes |
|---|---|---|---|---|
| 1 | 6 × 8 | 21 | 8 | Smallest usable size, good for dense status display |
| 2 | 12 × 16 | 10 | 4 | Good for headers or large numeric readouts |
| 3 | 18 × 24 | 7 | 2 | Large values; leaves room for a size-1 footer line |
| Mixed 2+1 | — | — | 2 header + 6 body | Best layout: size 2 title + size 1 details |

## Buttons

| Parameter | Value |
|---|---|
| Count | 4 momentary push buttons |
| Encoding | Individual GPIO pins with internal pull-up resistors, active LOW |
| Pins | GPIO1, GPIO2, GPIO3, GPIO4 |
| Functions | Motor A tighten, Motor A loosen, Motor B tighten, Motor B loosen |

## Power

| Rail | Source | Consumers |
|---|---|---|
| 24 V | UR5e tool flange | BLDC motors, INA240 sense resistor |
| 3.3 V | Buck converter (24 V → 3.3 V, part TBD) | MCU, RS-485, OLED, INA240 supply, buttons |
| 5 V | Possibly not needed | To be confirmed during testing |
