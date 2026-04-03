# Hardware Specifications

Ground-truth reference for component choices, voltage levels, and interface details.

## Microcontroller

| Parameter | Value |
|---|---|
| Board | Waveshare ESP32-S3-Tiny |
| MCU | ESP32-S3 (dual-core Xtensa LX7, 240 MHz) |
| Flash | TBD (check module datasheet) |
| PSRAM | TBD (check module datasheet) |
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

TBD — to be populated once the Waveshare ESP32-S3-Tiny datasheet is reviewed.
18 GPIO pins + TX/RX are exposed. Pin-to-GPIO mapping and ADC channel assignments need verification.

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
| TX pin | D6 (GPIO43) → MAX3485 DI (RDX on Module PCB)|
| RX pin | D7 (GPIO44) → MAX3485 RO (TDX on Module PCB)|
| DE/RE pin | D0 (GPIO1) → MAX3485 DE + ~RE (tied together) |
| Baud rate | 115200 (default) |

> **Label confusion:** Many MAX3485 breakout modules label the pins "TXD" and "RXD" from the module's perspective. "TXD" on the module is the receiver output (RO) — connect it to the MCU's **RX** pin (D7). "RXD" on the module is the driver input (DI) — connect it to the MCU's **TX** pin (D6). In short: module TXD → MCU RX, module RXD → MCU TX.

## Current Sensor — INA240 (preferred)

| Parameter | Value |
|---|---|
| Type | High-side current-sense amplifier |
| Output | Analog voltage (read with ESP32 ADC) |
| Common-mode range | Up to 80 V (suitable for 24 V rail) |
| Gain variants | A1 (×20), A2 (×50), A3 (×100), A4 (×200) — TBD which variant |
| ADC pin | TBD |

Fallback: INA219 (I2C, shares bus with OLED) — use if pin budget is too tight.

## OLED Display

| Parameter | Value |
|---|---|
| Size | 0.66″ |
| Driver | SSD1306 |
| Interface | I2C (SDA/SCL) |
| I2C address | 0x3C (7-bit) / 0x78 (8-bit, module selector) |
| Supply voltage | 3.3 V (only) |
| Wiring | SDA → TBD, SCL → TBD (pin assignment pending new board pin map) |

### Internal buffer vs visible area

The SSD1306 always has a 128×64 pixel internal framebuffer, but the 0.66″ module only exposes a **64×48 pixel visible window** offset within that buffer. The Adafruit_SSD1306 driver must be initialised with the full 128×64 size, and all drawing coordinates must be shifted to land inside the visible window:

| Parameter | Value |
|---|---|
| Framebuffer size | 128 × 64 pixels |
| Visible window | 64 × 48 pixels |
| Horizontal offset | 32 pixels (VIS_X = 32) |
| Vertical offset | 16 pixels (VIS_Y = 16) |

All `setCursor()` calls must use `(VIS_X + col, VIS_Y + row)` to place text within the visible area. Content drawn at (0, 0) will be off-screen to the top-left.

### Text size reference (within 64×48 visible area)

| textSize | Char px | Chars/line | Lines | Legible? | Notes |
|---|---|---|---|---|---|
| 1 | 6 × 8 | 10 | 6 | Yes | Smallest usable size, still readable |
| 2 | 12 × 16 | 5 | 3 | Yes | Good for headers or large readouts |
| 3 | 18 × 24 | 3 | 2 | Too large | Fills screen, not practical |
| Mixed 2+1 | — | — | 1 header + 4 body | Yes | Best layout: size 2 title + size 1 details |

## Buttons

| Parameter | Value |
|---|---|
| Count | 4 momentary push buttons |
| Encoding | Resistor-ladder voltage divider on single ADC pin |
| Functions | Motor A tighten, Motor A loosen, Motor B tighten, Motor B loosen |

## Power

| Rail | Source | Consumers |
|---|---|---|
| 24 V | UR5e tool flange | BLDC motors, INA240 sense resistor |
| 3.3 V | Buck converter (24 V → 3.3 V, part TBD) | MCU, RS-485, OLED, INA240 supply, buttons |
| 5 V | Possibly not needed | To be confirmed during testing |
