# test_bldc_pwm — Results 2026-04-01

## Motor Under Test

- Model: JGB37-3650-2480
- Gearbox ratio: 56:1 (142 RPM rated)
- Rated voltage: 24 V

## Setup

- MCU: XIAO ESP32-S3 Sense, powered via USB-C
- PWM output: D10 (GPIO6), direction: D9 (GPIO5)
- Motor powered from bench 24 V DC supply
- Common GND between ESP32 and 24 V supply
- 4.7 kΩ pull-down resistor on D10 (PWM pin to GND)

> **S3-Tiny note (2026-04-09):** This test was not re-run separately on the Waveshare ESP32-S3-Tiny. PWM drive is exercised and validated by the BLDC feedback test (`test_bldc_feedback`) and current test (`test_ina_current`), both of which passed on the S3-Tiny with identical results.

## Findings

### PWM Frequency

| Frequency | Starts from standstill at partial duty? |
|---|---|
| 1 kHz | No — requires 100% duty to start |
| 5 kHz | Yes |
| 10 kHz | Yes |
| 20 kHz | Yes |
| 25 kHz | Yes |

**Root cause:** The integrated driver expects an analog-like DC voltage (0–5 V) on the blue wire input. It has an internal RC low-pass filter. At 1 kHz, the ripple is too large for the driver's startup detection. At ≥5 kHz, the filter smooths the PWM into a clean enough DC level.

**Decision:** Use **5 kHz** as the default PWM frequency.

### Dead Band

- Motor starts spinning at **~40% duty** (from standstill, ramp up)
- Motor stops at **~37% duty** (from running, ramp down)
- Hysteresis: ~3%

### Direction Control

- **DIR 0 (LOW):** CW (viewed from output shaft toward motor body)
- **DIR 1 (HIGH):** CCW

### 3.3 V Logic Compatibility

- Both PWM (blue wire) and DIR (white wire) work correctly with 3.3 V logic from the ESP32.
- No level shifter needed for these inputs.

### CAP Output (not yet connected)

- Measured with oscilloscope: 5.2 V peak
- Requires voltage divider before connecting to ESP32 GPIO (tested in Step 2)
