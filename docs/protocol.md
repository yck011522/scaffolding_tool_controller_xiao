# Communication Protocol

Serial protocol between the upper-level controller (ROS node on UR5e) and
the scaffolding tool microcontroller. The same command set is accepted on
both transports:

- **RS-485** (production link to the robot)
- **USB-CDC** (bench/tuning, plus a small set of USB-only debug verbs)

## Design Principles

- **Master–slave.** The host (ROS node or tuning script) initiates every
  exchange. The tool controller never sends unsolicited data on RS-485.
- **No addressing.** Exactly one tool controller per RS-485 segment.
- **Request → response.** Every command produces exactly one response
  line. The host can parse it immediately without buffering or timeouts.
- **ASCII framing.** Human-readable, simple to parse from Python and C++.
  One command per line, `\n`-terminated.
- **Single command grammar.** USB and RS-485 share one parser, so any
  command that works on one transport works on the other. USB has a few
  extra debug-only verbs (`LOG`, `FAST`, `STATUSV`, `HELP`).
- **Persistent configuration.** All `SET` commands write to NVS so the
  tuned parameters survive power cycles.

## Transport

| Parameter            | Value                                              |
| -------------------- | -------------------------------------------------- |
| Physical (RS-485)    | RS-485 half-duplex (MAX3485 on Serial1, GPIO43/44) |
| Direction control    | GPIO18 (DE+RE tied)                                |
| Baud rate            | 115200                                             |
| Data format          | 8N1                                                |
| Line terminator      | LF `\n` (0x0A); CR is also tolerated               |
| Max command length   | 96 bytes                                           |
| Max response length  | 160 bytes                                          |
| Response timeout     | 100 ms (host-side retry/fault after this)          |

## Message Format

**Command (host → tool):**

```
<VERB> [<args...>]\n
```

Commands are case-insensitive. Tokens are whitespace-separated.

**Response (tool → host):**

```
OK [<data...>]\n        — success
ERR <code> <message>\n  — failure
```

Every response begins with `OK` or `ERR` so the host parser only needs to
check the first token.

## Command Reference

### Motor Control

| Command      | Description                                  | Response        |
| ------------ | -------------------------------------------- | --------------- |
| `TIGHTEN M1` | Run motor 1 in the tightening direction      | `OK TIGHTEN M1` |
| `LOOSEN M1`  | Run motor 1 in the loosening direction       | `OK LOOSEN M1`  |
| `TIGHTEN M2` | Run motor 2 in the tightening direction      | `OK TIGHTEN M2` |
| `LOOSEN M2`  | Run motor 2 in the loosening direction       | `OK LOOSEN M2`  |
| `STOP`       | Stop all motors and clear any stall flag     | `OK STOP`       |

Notes:

- Starting a motor while another is already running automatically stops
  the first one.
- Starting a motor that is currently `STALLED` returns `ERR 2 STALLED`.
  Send `STOP` first to clear the stall flag.

### Status Query

| Command   | Response                                              |
| --------- | ----------------------------------------------------- |
| `STATUS`  | `OK <M1_state> <M2_state> <current_mA> <pwm_pct>`     |
| `PING`    | `OK PONG`                                             |
| `VERSION` | `OK v<major>.<minor>.<patch>`                         |

`STATUS` collapses each motor's state and current action into a single
token (since there are only two possible actions, the action is folded
into the state):

| Field        | Type    | Values                                              |
| ------------ | ------- | --------------------------------------------------- |
| `M1_state`   | string  | `IDLE`, `TIGHTENING`, `LOOSENING`, `STALLED`        |
| `M2_state`   | string  | `IDLE`, `TIGHTENING`, `LOOSENING`, `STALLED`        |
| `current_mA` | integer | 0–1500 (rail current)                               |
| `pwm_pct`    | integer | 0–100 (PWM duty of the active motor; 0 if neither)  |

Example: `OK IDLE TIGHTENING 285 56`

### Configuration

All configuration values are per motor. Where a parameter has a separate
value for tightening vs loosening, an action token (`T` or `L`) is also
required. Every successful `SET` is **immediately written to NVS** and
takes effect on the next control cycle (or on the next motor start, in
the case of fields snapshotted at start-up like `LIMIT`).

| Command                              | Range          | Description                                    |
| ------------------------------------ | -------------- | ---------------------------------------------- |
| `SET LIMIT <M1\|M2> <T\|L> <mA>`     | 50 – 1500 mA   | PI current setpoint                            |
| `SET KP <M1\|M2> <val>`              | 0 – 100        | Velocity-form PI proportional gain             |
| `SET KI <M1\|M2> <val>`              | 0 – 100        | Velocity-form PI integral gain                 |
| `SET SLEW <M1\|M2> <T\|L> <val>`     | 1 – 255        | Max PWM change per control cycle               |
| `SET RAMP <M1\|M2> <ms>`             | 0 – 5000 ms    | Soft-start ramp duration                       |
| `SET PWMMIN <M1\|M2> <val>`          | 0 – 255        | Dead-band floor                                |
| `SET PWMMAX <M1\|M2> <T\|L> <val>`   | 0 – 255        | Per-action PWM ceiling                         |
| `SET PWMSTART <M1\|M2> <T\|L> <val>` | 0 – 255        | Per-action startup-kick PWM                    |
| `SET GEAR <M1\|M2> <ratio>`          | 1 – 500        | Gear reduction ratio (display / RPM math only) |

The successful response echoes the canonical form, e.g.
`OK SET LIMIT M1 T 330`.

For inspection without changing values:

| Command                            | Response                                         |
| ---------------------------------- | ------------------------------------------------ |
| `GET LIMIT <M1\|M2> <T\|L>`        | `OK LIMIT <M> <A> <mA>`                          |
| `GET KP <M1\|M2>`                  | `OK KP <M> <val>`                                |
| `GET KI <M1\|M2>`                  | `OK KI <M> <val>`                                |
| `GET SLEW <M1\|M2> <T\|L>`         | `OK SLEW <M> <A> <val>`                          |
| `GET RAMP <M1\|M2>`                | `OK RAMP <M> <ms>`                               |
| `GET PWMMIN <M1\|M2>`              | `OK PWMMIN <M> <val>`                            |
| `GET PWMMAX <M1\|M2> <T\|L>`       | `OK PWMMAX <M> <A> <val>`                        |
| `GET PWMSTART <M1\|M2> <T\|L>`     | `OK PWMSTART <M> <A> <val>`                      |
| `GET GEAR <M1\|M2>`                | `OK GEAR <M> <ratio>`                            |
| `GET CONFIG <M1\|M2>`              | `OK CONFIG <M> limit_T=… limit_L=… kp=… ki=… …` |

Persistence control:

| Command         | Description                                                            | Response          |
| --------------- | ---------------------------------------------------------------------- | ----------------- |
| `RESET CONFIG`  | Wipe stored NVS config; defaults restored on the next boot.            | `OK RESET CONFIG` |
| `SAVE`          | No-op; SET commands persist automatically. Provided for explicitness.  | `OK SAVE`         |

## Error Codes

| Code | Meaning                                                |
| ---- | ------------------------------------------------------ |
| 1    | Unknown command                                        |
| 2    | Motor is stalled (send STOP to clear)                  |
| 3    | Invalid parameter (out of range, wrong type, missing)  |
| 4    | Motor index invalid (must be `M1` or `M2`)             |
| 5    | Action token invalid (must be `T` or `L`)              |
| 6    | Persistent storage error                               |

## Typical Exchange Sequences

### Initial handshake and limit setup

```
Host:  PING
Tool:  OK PONG
Host:  VERSION
Tool:  OK v1.0.0
Host:  SET LIMIT M1 T 330
Tool:  OK SET LIMIT M1 T 330
Host:  SET LIMIT M2 L 900
Tool:  OK SET LIMIT M2 L 900
```

### Tighten a scaffolding tube

```
Host:  TIGHTEN M2
Tool:  OK TIGHTEN M2
Host:  STATUS                      (poll, e.g. every 50 ms)
Tool:  OK IDLE TIGHTENING 285 56
Host:  STATUS
Tool:  OK IDLE STALLED 330 0
Host:  STOP
Tool:  OK STOP
```

### Handle a stall before retrying

```
Host:  TIGHTEN M1
Tool:  OK TIGHTEN M1
        ... (motor stalls) ...
Host:  STATUS
Tool:  OK STALLED IDLE 0 0
Host:  TIGHTEN M1                  (without STOP first)
Tool:  ERR 2 STALLED
Host:  STOP
Tool:  OK STOP
Host:  TIGHTEN M1
Tool:  OK TIGHTEN M1
```

## USB-Only Commands (bench debugging)

These verbs are accepted **only** on the USB-CDC interface. They are not
exposed on RS-485 because they emit unsolicited streaming output that
would violate the master–slave contract.

| Command   | Purpose                                                           |
| --------- | ----------------------------------------------------------------- |
| `LOG`     | Toggle a 200 ms periodic CSV log line (`LOG,…`)                   |
| `FAST`    | Toggle a per-control-cycle CSV stream (`FAST,…`)                  |
| `STATUSV` | Verbose multi-line status print (config + runtime, human reading) |
| `HELP`    | Print the command list                                            |

All other commands (motor control, status, configuration, reset) are
identical on USB and RS-485.

## Future Considerations

- **Checksums.** If RS-485 noise proves problematic, add an optional CRC-8
  suffix (`TIGHTEN M1 *A3\n`). The controller would accept lines with or
  without the suffix during transition.
- **Binary mode.** If ASCII parsing latency matters (unlikely at <10 Hz
  command rate), define a compact binary framing with the same semantics.
# Communication Protocol

RS-485 serial protocol between the upper-level controller (ROS node on UR5e)
and the scaffolding tool microcontroller.

## Design Principles

- **Master–slave.** The robot side (ROS node) initiates every exchange.
  The tool controller never sends unsolicited data.
- **Addressomg.** No specificc addressing as there will be 
  only one device shall occupy each RS485 interface.
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
| Line terminator | Line Feed (LF) `\n` (0x0A) |
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
| `TIGHTEN M1` | Run motor 1, tighten | `OK TIGHTEN M1` |
| `LOOSEN M1` | Run motor 1, loosen | `OK LOOSEN M1` |
| `TIGHTEN M2` | Run motor 2, tighten | `OK TIGHTEN M2` |
| `LOOSEN M2` | Run motor 2, loosen | `OK LOOSEN M2` |
| `STOP` | Stop all motors, reset stall flag | `OK STOP` |

Starting a motor while another is already running stops the first one
automatically. Starting a stalled motor returns `ERR 2 M1 STALLED` — send
`STOP` first to clear the stall flag.

### Status Query

| Command | Response |
|---|---|
| `STATUS` | `OK <M1_state> <M2_state> <current_mA> <active_pwm_pct>` |
| `PING` | `OK PONG` |

**STATUS fields:**

| Field | Type | Values |
|---|---|---|
| M1_state | string | `IDLE`, `TIGHTENING`, `LOOSENING`, `STALLED` |
| M2_state | string | `IDLE`, `TIGHTENING`, `LOOSENING`, `STALLED` |
| current_mA | integer | 0–825 (sensor ceiling) |
| active_pwm_pct | integer | 0–100 |

Example: 

### Configuration

| Command | Description | Response |
|---|---|---|


Configuration changes take effect immediately. They are persisted
across power cycles.

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

## Future Considerations

- **Checksums.** If RS-485 noise proves to be an issue, add an optional
  CRC-8 suffix: `RUN M1 T *A3\n`. The controller accepts lines with or
  without the checksum during transition.
- **Binary mode.** If ASCII parsing latency matters (unlikely at <10 Hz
  command rate), define a compact binary framing with the same semantics.
