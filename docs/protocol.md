# Communication Protocol

RS-485 serial protocol between the upper-level controller (ROS node on UR5e) and this microcontroller.

> **Status:** Template — protocol to be defined after RS-485 communication is verified in testing.

## Transport

- Physical: RS-485 half-duplex via MAX3485
- Baud rate: TBD (115200 default)
- Format: TBD (ASCII vs binary framing)

## Command Interface

The controller accepts commands from **two** serial interfaces:
1. **RS-485** — primary, used in production on the robot
2. **USB-C serial** — secondary, used for bench debugging

Both interfaces accept the same command format. Replies are sent back on the originating interface (or optionally on both, configurable).

## Command List (Draft)

| Command | Description | Parameters | Response |
|---|---|---|---|
| TBD | Motor speed command | motor_id, direction, speed | Acknowledgement + status |
| TBD | Motor stop | motor_id | Acknowledgement |
| TBD | Query status | — | Motor state, current, RPM |
| TBD | Start camera stream | — | Acknowledgement |
| TBD | Stop camera stream | — | Acknowledgement |
| TBD | Set current limit | threshold_mA | Acknowledgement |
| TBD | Echo / ping | payload | Echo payload back |

## Message Framing

TBD — options to evaluate:
- Simple ASCII with delimiters (Preferred) (e.g., `CMD,param1,param2\n`)
- Binary with header, length, CRC

## Response Structure

TBD — should include at minimum:
- Command acknowledgement (OK / ERROR)
- Motor status (running, stalled, idle)
- Current draw (mA)
- Motor RPM (from feedback)
