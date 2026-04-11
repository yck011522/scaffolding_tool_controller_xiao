# Communication Protocol

RS-485 serial protocol between the upper-level controller (ROS node on UR5e)
and the scaffolding tool microcontroller.

> **Status:** Draft — ready for review.

## Design Principles

- **Master–slave.** The robot side (ROS node) initiates every exchange.
  The tool controller never sends unsolicited data.
- **Request → response.** Every command gets exactly one response line.
  The robot can parse it immediately without buffering or timeouts.
- **ASCII framing.** Human-readable for debugging, simple to parse in
  both Python (ROS) and C++ (firmware). One command per line, `\n`
  terminated.
- **No streaming.** High-frequency logging (LOG, FAST) is USB-only for
  bench tuning. The RS-485 bus stays quiet between exchanges.

## Transport

| Parameter | Value |
|---|---|
| Physical | RS-485 half-duplex (MAX3485) |
| Baud rate | 115200 |
| Data format | 8N1 |
| Line terminator | `\n` (0x0A) |
| Max command length | 64 bytes |
| Max response length | 128 bytes |
| Response timeout | 100 ms (robot side should retry or fault after this) |

## Message Format

**Command (robot → tool):**
```
<VERB> [<args...>]\n
```

**Response (tool → robot):**
```
OK [<data...>]\n        — success
ERR <code> <message>\n   — failure
```

Every response begins with `OK` or `ERR` so the parser only needs to check
the first token.

## Command Reference

### Motor Control

| Command | Description | Response |
|---|---|---|
| `RUN M1 T` | Run motor 1, tighten | `OK M1 T` |
| `RUN M1 L` | Run motor 1, loosen | `OK M1 L` |
| `RUN M2 T` | Run motor 2, tighten | `OK M2 T` |
| `RUN M2 L` | Run motor 2, loosen | `OK M2 L` |
| `STOP` | Stop all motors, reset stall flag | `OK STOP` |
| `STOP M1` | Stop motor 1 only | `OK STOP M1` |
| `STOP M2` | Stop motor 2 only | `OK STOP M2` |

Starting a motor while another is already running stops the first one
automatically. Starting a stalled motor returns `ERR 2 M1 STALLED` — send
`STOP` first to clear the stall flag.

### Status Query

| Command | Response |
|---|---|
| `STATUS` | `OK <M1_state> <M1_action> <M2_state> <M2_action> <current_mA> <active_pwm_pct>` |
| `PING` | `OK PONG` |
| `VERSION` | `OK <firmware_version_string>` |

**STATUS fields:**

| Field | Type | Values |
|---|---|---|
| M1_state | string | `IDLE`, `RUN`, `STALL` |
| M1_action | string | `T`, `L`, `-` (none) |
| M2_state | string | `IDLE`, `RUN`, `STALL` |
| M2_action | string | `T`, `L`, `-` (none) |
| current_mA | integer | 0–825 (sensor ceiling) |
| active_pwm_pct | integer | 0–100 |

Example: `OK RUN T IDLE - 312 45` — motor 1 tightening at 312 mA / 45% PWM, motor 2 idle.

### Configuration

| Command | Description | Response |
|---|---|---|
| `SET LIMIT M1 T <mA>` | Set motor 1 tighten current limit | `OK LIMIT M1 T <mA>` |
| `SET LIMIT M1 L <mA>` | Set motor 1 loosen current limit | `OK LIMIT M1 L <mA>` |
| `SET LIMIT M2 T <mA>` | Set motor 2 tighten current limit | `OK LIMIT M2 T <mA>` |
| `SET LIMIT M2 L <mA>` | Set motor 2 loosen current limit | `OK LIMIT M2 L <mA>` |
| `GET LIMIT` | Query all current limits | `OK LIMIT M1 <T_mA> <L_mA> M2 <T_mA> <L_mA>` |

Configuration changes take effect immediately. They are **not** persisted
across power cycles — the robot should set them during initialization.

## Error Codes

| Code | Meaning |
|---|---|
| 1 | Unknown command |
| 2 | Motor is stalled (send STOP to clear) |
| 3 | Invalid parameter (out of range or wrong type) |
| 4 | Motor index invalid |

## Typical Exchange Sequences

### Initialize and set limits
```
Robot:  PING
Tool:   OK PONG
Robot:  VERSION
Tool:   OK v1.0.0
Robot:  SET LIMIT M1 T 330
Tool:   OK LIMIT M1 T 330
Robot:  SET LIMIT M2 L 900
Tool:   OK LIMIT M2 L 900
```

### Tighten a scaffolding tube
```
Robot:  RUN M2 T
Tool:   OK M2 T
Robot:  STATUS            (poll periodically)
Tool:   OK IDLE - RUN T 285 44
Robot:  STATUS
Tool:   OK IDLE - STALL T 330 45
Robot:  STOP
Tool:   OK STOP
```

### Handle stall during operation
```
Robot:  RUN M1 T
Tool:   OK M1 T
  ... (motor stalls) ...
Robot:  STATUS
Tool:   OK STALL T IDLE - 330 45
Robot:  STOP
Tool:   OK STOP
Robot:  RUN M1 T          (retry if desired)
Tool:   OK M1 T
```

## USB-Only Commands (bench debugging)

These commands are accepted only on the USB serial interface and are
**not** available over RS-485. They exist for tuning and diagnostics:

| Command | Purpose |
|---|---|
| `LOG` | Toggle 200 ms periodic logging |
| `FAST` | Toggle per-control-cycle logging |
| `KP <val>` | Set proportional gain |
| `KI <val>` | Set integral gain |
| `SLEW T\|L <val>` | Set slew rate |
| `RAMP <ms>` | Set ramp duration |
| `RATIO <n>` | Set gearbox ratio for RPM display |

## Future Considerations

- **Checksums.** If RS-485 noise proves to be an issue, add an optional
  CRC-8 suffix: `RUN M1 T *A3\n`. The controller accepts lines with or
  without the checksum during transition.
- **Multi-tool addressing.** If multiple tools share the RS-485 bus, add a
  1-byte address prefix: `@1 RUN M1 T\n`. Address 0 = broadcast.
- **Binary mode.** If ASCII parsing latency matters (unlikely at <10 Hz
  command rate), define a compact binary framing with the same semantics.
