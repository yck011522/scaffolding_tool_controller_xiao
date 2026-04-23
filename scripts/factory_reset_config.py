"""Factory-reset the persisted configuration on the scaffolding tool controller.

Sends `RESET CONFIG` over the USB-CDC interface, waits for the acknowledgement,
then asks the controller to reboot so the compiled defaults are reloaded.

Usage:
    python scripts/factory_reset_config.py [--port COM22] [--baud 115200] [--no-verify]

After reset, the script prints `GET CONFIG M1` and `GET CONFIG M2` so the
defaults can be confirmed at a glance. Pass --no-verify to skip the readback.

Pip requirements:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import sys
import time

import serial
import serial.tools.list_ports


def find_default_port() -> str | None:
    """Pick the first USB-serial port that looks like an ESP32-S3."""
    for p in serial.tools.list_ports.comports():
        desc = (p.description or "") + " " + (p.manufacturer or "")
        if "USB" in desc.upper() or "CP210" in desc.upper() or "CH340" in desc.upper():
            return p.device
    ports = list(serial.tools.list_ports.comports())
    return ports[0].device if ports else None


def hard_reset(ser: serial.Serial) -> None:
    """Toggle DTR/RTS to reboot the ESP32 (RTS pulse, DTR held low)."""
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False


def send_line(ser: serial.Serial, line: str, label: str = ">>") -> None:
    print(f"{label} {line}")
    ser.write((line + "\n").encode("ascii"))
    ser.flush()


def read_response(ser: serial.Serial, timeout_s: float = 2.0) -> str:
    """Wait for the next OK/ERR line. Echoes other lines for visibility."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("ascii", errors="replace").rstrip()
        if not text:
            continue
        print(f"<< {text}")
        if text.startswith("OK") or text.startswith("ERR"):
            return text
    return ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default=None, help="Serial port (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--no-verify", action="store_true", help="Skip GET CONFIG readback after reset")
    ap.add_argument("--no-reboot", action="store_true",
                    help="Skip the post-reset reboot (defaults won't be live until next boot)")
    args = ap.parse_args()

    port = args.port or find_default_port()
    if not port:
        print("No serial port found. Specify with --port COMn.", file=sys.stderr)
        return 1

    print(f"[INFO] Using serial port {port} @ {args.baud} baud")
    with serial.Serial(port, args.baud, timeout=0.5) as ser:
        # Drain any boot banner so we don't confuse it with our responses.
        time.sleep(0.5)
        ser.reset_input_buffer()

        # Confirm the controller is reachable before wiping anything.
        send_line(ser, "PING")
        if not read_response(ser).startswith("OK"):
            print("[FAIL] No PING response — is the firmware running?", file=sys.stderr)
            return 2

        # Wipe the NVS namespace.
        send_line(ser, "RESET CONFIG")
        if not read_response(ser).startswith("OK"):
            print("[FAIL] RESET CONFIG was not acknowledged", file=sys.stderr)
            return 3
        print("[OK] NVS configuration cleared")

        # Reboot so configStoreLoad() runs against the empty namespace
        # and the in-memory cfg[] reverts to the compiled defaults.
        if not args.no_reboot:
            print("[INFO] Rebooting controller via DTR/RTS...")
            hard_reset(ser)
            time.sleep(2.5)         # wait for boot + INA240 calibration banner
            ser.reset_input_buffer()

        if not args.no_verify:
            send_line(ser, "GET CONFIG M1")
            read_response(ser)
            send_line(ser, "GET CONFIG M2")
            read_response(ser)

    print("[DONE] Factory reset complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
