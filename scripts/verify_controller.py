"""End-to-end verification of the integrated scaffolding tool firmware.

Exercises every command in docs/protocol.md against the controller, twice:
once over USB-CDC and once over RS-485 (each through its own USB serial
adapter on the PC). Compares responses to the documented format and
prints a [PASS]/[FAIL] line per test plus a final summary.

Per-suite flow (USB and RS-485 each):
    1. Snapshot the controller's current configuration.
    2. RESET CONFIG so motor tests run against the firmware's factory
       defaults (the OLED will visibly show the defaults reload thanks
       to the in-RAM reset added to configStoreReset()).
    3. Snapshot the factory defaults (post-reset GET CONFIG).
    4. Run motor TIGHTEN/LOOSEN tests against those defaults.
    5. Run handshake / status / get-config / SET-GET / error / USB-only
       tests.
    6. Restore the original configuration the user had on the device
       before this script ran.
    7. Print a diff between the original config and the factory
       defaults so the user knows what was tuned.

Hardware assumptions during the run:
- Both motors are mechanically free (will not stall). Tests start each
  motor briefly, confirm motion via STATUS / current / PWM, then STOP.
- Firmware from src/ is flashed and the OLED + buttons are present
  (their state is not checked here).

Usage:
    python scripts/verify_controller.py
    python scripts/verify_controller.py --usb-port COM22 --rs485-port COM7
    python scripts/verify_controller.py --skip-rs485       # USB only
    python scripts/verify_controller.py --skip-usb         # RS-485 only
    python scripts/verify_controller.py --skip-motor       # config tests only
    python scripts/verify_controller.py --no-restore       # leave defaults

Pip requirements:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass, field

import serial

# ─────────────────────────────────────────────────────────────────────
# EDIT THESE if the COM-port assignments change. Override on the CLI
# with --usb-port / --rs485-port to skip editing this file.
# ─────────────────────────────────────────────────────────────────────
DEFAULT_USB_PORT   = "COM22"   # ESP32-S3 USB-CDC (debug + protocol)
DEFAULT_RS485_PORT = "COM7"    # USB ↔ RS-485 adapter to MAX3485
DEFAULT_BAUD       = 115200
# ─────────────────────────────────────────────────────────────────────

RESPONSE_TIMEOUT_S    = 1.5     # max wait for one OK/ERR line
MOTOR_RUN_TIME_S      = 2.0     # how long to let a motor spin per test
MOTOR_SETTLE_S        = 0.8     # delay after STOP before reading STATUS
BOOT_SETTLE_S         = 2.5     # boot + INA240 calibration

STATUS_RE = re.compile(
    r"^OK\s+(IDLE|TIGHTENING|LOOSENING|STALLED)"
    r"\s+(IDLE|TIGHTENING|LOOSENING|STALLED)"
    r"\s+(-?\d+)\s+(\d+)\s*$"
)

# `OK CONFIG M1 lim_T=330 lim_L=900 kp=0.000 ki=1.0000 ramp=200 pmin=102
#  pmax_T=160 pmax_L=160 pstart_T=102 pstart_L=155 slew_T=15 slew_L=500 gear=56.0`
CONFIG_PREFIX_RE = re.compile(r"^OK\s+CONFIG\s+(M[12])\s+(.*)$")


def set_command_for(field_name: str, tag: str, value: str) -> str | None:
    """Map a config-dump field name to the equivalent `SET ...` command."""
    table_per_action = {
        "lim_T":    ("LIMIT",    "T"),
        "lim_L":    ("LIMIT",    "L"),
        "slew_T":   ("SLEW",     "T"),
        "slew_L":   ("SLEW",     "L"),
        "pmax_T":   ("PWMMAX",   "T"),
        "pmax_L":   ("PWMMAX",   "L"),
        "pstart_T": ("PWMSTART", "T"),
        "pstart_L": ("PWMSTART", "L"),
    }
    table_single = {
        "kp":   "KP",
        "ki":   "KI",
        "ramp": "RAMP",
        "pmin": "PWMMIN",
        "gear": "GEAR",
    }
    if field_name in table_per_action:
        verb, action = table_per_action[field_name]
        return f"SET {verb} {tag} {action} {value}"
    if field_name in table_single:
        return f"SET {table_single[field_name]} {tag} {value}"
    return None


def parse_config_reply(reply: str) -> tuple[str, dict[str, str]] | None:
    """Parse `OK CONFIG M1 k=v k=v ...` into (tag, {field: value_str})."""
    m = CONFIG_PREFIX_RE.match(reply.strip())
    if not m:
        return None
    tag, body = m.group(1), m.group(2)
    fields: dict[str, str] = {}
    for tok in body.split():
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        fields[k] = v
    return tag, fields


# ─────────────────────────────────────────────────────────────────────
# Transport wrapper
# ─────────────────────────────────────────────────────────────────────
class Link:
    """Simple line-oriented serial wrapper. Only used by one thread."""

    def __init__(self, name: str, port: str, baud: int = DEFAULT_BAUD):
        self.name = name
        self.port = port
        self.ser = serial.Serial(port, baud, timeout=0.2)

    def close(self) -> None:
        self.ser.close()

    def reset_board(self) -> None:
        """USB-CDC only: pulse RTS to reboot the ESP32. No-op on RS-485."""
        self.ser.dtr = False
        self.ser.rts = False
        time.sleep(0.1)
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False

    def drain(self) -> None:
        time.sleep(0.05)
        self.ser.reset_input_buffer()

    def send(self, line: str) -> str:
        """Send one command and return the first OK/ERR line received."""
        self.ser.reset_input_buffer()
        self.ser.write((line + "\n").encode("ascii"))
        self.ser.flush()
        return self._read_response()

    def send_raw(self, line: str, settle_s: float = 0.3) -> list[str]:
        """Send one command and return ALL lines received within `settle_s`.

        Used for free-form USB-only verbs (STATUSV, HELP) that print
        multi-line output without an OK/ERR terminator.
        """
        self.ser.reset_input_buffer()
        self.ser.write((line + "\n").encode("ascii"))
        self.ser.flush()
        deadline = time.monotonic() + settle_s
        out: list[str] = []
        while time.monotonic() < deadline:
            raw = self.ser.readline()
            if not raw:
                continue
            text = raw.decode("ascii", errors="replace").strip()
            if text:
                out.append(text)
        return out

    def _read_response(self) -> str:
        deadline = time.monotonic() + RESPONSE_TIMEOUT_S
        while time.monotonic() < deadline:
            raw = self.ser.readline()
            if not raw:
                continue
            text = raw.decode("ascii", errors="replace").strip()
            if not text:
                continue
            # Skip stray boot banners / async LOG lines that may sneak in
            # on USB; the protocol contract is one OK/ERR per command.
            if text.startswith("OK") or text.startswith("ERR"):
                return text
        return ""   # timeout


# ─────────────────────────────────────────────────────────────────────
# Test runner with [PASS]/[FAIL] tally
# ─────────────────────────────────────────────────────────────────────
@dataclass
class Suite:
    label: str
    passed: int = 0
    failed: int = 0
    failures: list[str] = field(default_factory=list)

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        tag = "[PASS]" if ok else "[FAIL]"
        line = f"  {tag} {name}"
        if detail:
            line += f"  — {detail}"
        print(line)
        if ok:
            self.passed += 1
        else:
            self.failed += 1
            self.failures.append(f"{self.label}: {name}  {detail}")
        return ok


def expect_ok(suite: Suite, name: str, response: str, expected_prefix: str = "OK") -> bool:
    if not response:
        return suite.check(name, False, "timeout (no response)")
    return suite.check(name, response.startswith(expected_prefix), f"got: {response!r}")


def expect_err(suite: Suite, name: str, response: str, code: int) -> bool:
    if not response:
        return suite.check(name, False, "timeout (no response)")
    ok = response.startswith(f"ERR {code} ")
    return suite.check(name, ok, f"got: {response!r}")


# ─────────────────────────────────────────────────────────────────────
# Snapshot / restore / diff helpers
# ─────────────────────────────────────────────────────────────────────
def snapshot_config(link: Link) -> dict[str, dict[str, str]]:
    """Return {'M1': {field: value_str, ...}, 'M2': {...}}."""
    snap: dict[str, dict[str, str]] = {}
    for tag in ("M1", "M2"):
        rsp = link.send(f"GET CONFIG {tag}")
        parsed = parse_config_reply(rsp)
        if parsed is None:
            raise RuntimeError(f"GET CONFIG {tag} returned unparseable: {rsp!r}")
        ptag, fields = parsed
        snap[ptag] = fields
    return snap


def restore_config(link: Link, snap: dict[str, dict[str, str]]) -> int:
    """Push every field of `snap` back via SET commands. Returns # SETs sent."""
    n = 0
    for tag, fields in snap.items():
        for fname, val in fields.items():
            cmd = set_command_for(fname, tag, val)
            if cmd is None:
                continue
            link.send(cmd)
            n += 1
    return n


def diff_configs(a: dict[str, dict[str, str]],
                 b: dict[str, dict[str, str]]) -> list[tuple[str, str, str, str]]:
    """Return list of (motor_tag, field, a_val, b_val) where they differ."""
    diffs: list[tuple[str, str, str, str]] = []
    for tag in sorted(set(a) | set(b)):
        ad = a.get(tag, {})
        bd = b.get(tag, {})
        for fname in sorted(set(ad) | set(bd)):
            if ad.get(fname) != bd.get(fname):
                diffs.append((tag, fname, ad.get(fname, "(missing)"),
                              bd.get(fname, "(missing)")))
    return diffs


def print_config_diff(label: str, original: dict[str, dict[str, str]],
                      defaults: dict[str, dict[str, str]]) -> None:
    diffs = diff_configs(defaults, original)
    print(f"\n[{label}] User config vs. factory defaults:")
    if not diffs:
        print("    (identical — controller was already at factory defaults)")
        return
    print(f"    {'motor':5s}  {'field':10s}  {'default':>10s}  ->  {'original':>10s}")
    for tag, fname, dv, ov in diffs:
        print(f"    {tag:5s}  {fname:10s}  {dv:>10s}  ->  {ov:>10s}")


# ─────────────────────────────────────────────────────────────────────
# Individual test groups
# ─────────────────────────────────────────────────────────────────────
def test_handshake(link: Link, suite: Suite) -> None:
    print(f"\n[GROUP] Handshake ({suite.label})")
    expect_ok(suite, "PING -> OK PONG", link.send("PING"), "OK PONG")
    rsp = link.send("VERSION")
    suite.check("VERSION format", bool(rsp) and rsp.startswith("OK v"),
                f"got: {rsp!r}")


def test_status_parse(link: Link, suite: Suite) -> None:
    print(f"\n[GROUP] STATUS ({suite.label})")
    rsp = link.send("STATUS")
    m = STATUS_RE.match(rsp) if rsp else None
    suite.check("STATUS matches '<s1> <s2> <mA> <pwm%>'", bool(m),
                f"got: {rsp!r}")


def test_get_config(link: Link, suite: Suite) -> None:
    print(f"\n[GROUP] GET CONFIG ({suite.label})")
    for tag in ("M1", "M2"):
        rsp = link.send(f"GET CONFIG {tag}")
        ok = bool(rsp) and rsp.startswith(f"OK CONFIG {tag} ")
        suite.check(f"GET CONFIG {tag}", ok, f"got: {rsp!r}")


def test_set_get_roundtrip(link: Link, suite: Suite) -> None:
    """Round-trip a few SET/GET pairs. Caller is responsible for restoration."""
    print(f"\n[GROUP] SET/GET round-trip ({suite.label})")

    cases: list[tuple[str, str, str]] = [
        # (set_cmd, get_cmd, expected_get_body)
        ("SET LIMIT M1 T 333",   "GET LIMIT M1 T",   "OK LIMIT M1 T 333"),
        ("SET LIMIT M2 L 444",   "GET LIMIT M2 L",   "OK LIMIT M2 L 444"),
        ("SET KP M1 1.250",      "GET KP M1",        "OK KP M1 1.250"),
        ("SET KI M2 0.0500",     "GET KI M2",        "OK KI M2 0.0500"),
        ("SET SLEW M1 T 12",     "GET SLEW M1 T",    "OK SLEW M1 T 12"),
        ("SET RAMP M2 600",      "GET RAMP M2",      "OK RAMP M2 600"),
        ("SET PWMMIN M1 30",     "GET PWMMIN M1",    "OK PWMMIN M1 30"),
        ("SET PWMMAX M2 L 222",  "GET PWMMAX M2 L",  "OK PWMMAX M2 L 222"),
        ("SET PWMSTART M1 T 80", "GET PWMSTART M1 T","OK PWMSTART M1 T 80"),
        ("SET GEAR M2 90.0",     "GET GEAR M2",      "OK GEAR M2 90.0"),
    ]

    for set_cmd, get_cmd, expected in cases:
        rsp_set = link.send(set_cmd)
        suite.check(set_cmd, bool(rsp_set) and rsp_set.startswith("OK SET "),
                    f"got: {rsp_set!r}")
        rsp_get = link.send(get_cmd)
        suite.check(f"{get_cmd} == {expected!r}", rsp_get == expected,
                    f"got: {rsp_get!r}")


def test_error_cases(link: Link, suite: Suite) -> None:
    print(f"\n[GROUP] Error cases ({suite.label})")
    expect_err(suite, "Unknown verb -> ERR 1",        link.send("FROBNICATE"), 1)
    expect_err(suite, "TIGHTEN M3 -> ERR 4",          link.send("TIGHTEN M3"), 4)
    expect_err(suite, "SET LIMIT M1 X 100 -> ERR 5",  link.send("SET LIMIT M1 X 100"), 5)
    expect_err(suite, "SET LIMIT M1 T 9999 -> ERR 3", link.send("SET LIMIT M1 T 9999"), 3)
    expect_err(suite, "GET KP -> ERR 3",              link.send("GET KP"), 3)


def test_motor_run(link: Link, suite: Suite, motor: str, action: str) -> None:
    """Run motor briefly, confirm STATUS reflects motion, then STOP."""
    label = f"{action} {motor}"
    expected_state = "TIGHTENING" if action == "TIGHTEN" else "LOOSENING"

    rsp = link.send(f"{action} {motor}")
    expect_ok(suite, f"{label} accepted", rsp, f"OK {action} {motor}")

    time.sleep(MOTOR_RUN_TIME_S)
    rsp = link.send("STATUS")
    m = STATUS_RE.match(rsp) if rsp else None
    if not suite.check(f"STATUS during {label} parses", bool(m), f"got: {rsp!r}"):
        link.send("STOP")
        return

    s1, s2, ma, pwm = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
    motor_state = s1 if motor == "M1" else s2
    suite.check(f"{label}: motor state == {expected_state}",
                motor_state == expected_state,
                f"got M1={s1} M2={s2}")
    suite.check(f"{label}: current > 0 mA", ma > 0, f"got {ma} mA")
    suite.check(f"{label}: pwm > 0 %",      pwm > 0, f"got {pwm} %")

    rsp = link.send("STOP")
    expect_ok(suite, f"STOP after {label}", rsp, "OK STOP")

    time.sleep(MOTOR_SETTLE_S)
    rsp = link.send("STATUS")
    m = STATUS_RE.match(rsp) if rsp else None
    if suite.check(f"STATUS after STOP parses", bool(m), f"got: {rsp!r}"):
        s1, s2, ma, pwm = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
        motor_state = s1 if motor == "M1" else s2
        suite.check(f"{label}: motor returned to IDLE",
                    motor_state == "IDLE", f"got M1={s1} M2={s2}")
        suite.check(f"{label}: pwm == 0 after STOP",
                    pwm == 0, f"got {pwm} %")


def test_motor_all(link: Link, suite: Suite) -> None:
    print(f"\n[GROUP] Motor exercise — using FACTORY DEFAULTS ({suite.label})")
    for motor in ("M1", "M2"):
        for action in ("TIGHTEN", "LOOSEN"):
            test_motor_run(link, suite, motor, action)


def test_usb_only_verbs(link: Link, suite: Suite, transport_is_usb: bool) -> None:
    """LOG / FAST / STATUSV / HELP must be accepted on USB and rejected on RS-485.

    LOG and FAST reply with `OK LOG ON/OFF` so we use the OK/ERR reader.
    STATUSV and HELP print free-form multi-line text with no terminator,
    so we use the raw line reader and just check that *something* came back.
    """
    print(f"\n[GROUP] USB-only verbs ({suite.label})")
    if transport_is_usb:
        # Toggleable streamers — confirm OK and toggle back off.
        for verb in ("LOG", "FAST"):
            rsp = link.send(verb)
            ok = bool(rsp) and rsp.startswith("OK")
            suite.check(f"{verb} accepted on USB", ok, f"got: {rsp!r}")
            link.drain()
            link.send(verb)        # toggle off so streaming doesn't pollute
        # Free-form dumps — just verify a non-empty multi-line reply.
        for verb in ("STATUSV", "HELP"):
            lines = link.send_raw(verb, settle_s=0.4)
            suite.check(f"{verb} prints output on USB",
                        len(lines) >= 2, f"lines={len(lines)}")
        link.drain()
    else:
        for verb in ("LOG", "FAST", "STATUSV", "HELP"):
            rsp = link.send(verb)
            ok = bool(rsp) and rsp.startswith("ERR")
            suite.check(f"{verb} rejected on RS-485", ok, f"got: {rsp!r}")


# ─────────────────────────────────────────────────────────────────────
# Per-suite driver
# ─────────────────────────────────────────────────────────────────────
def run_suite(link: Link, label: str, transport_is_usb: bool,
              skip_motor: bool, restore: bool) -> Suite:
    suite = Suite(label=label)
    print(f"\n══════════════════════════════════════════════════════════")
    print(f" Running suite: {label}  (port={link.port})")
    print(f"══════════════════════════════════════════════════════════")

    link.drain()

    # Always-safe smoke checks first.
    test_handshake(link, suite)
    test_status_parse(link, suite)

    # ── Snapshot original cfg, then RESET to factory defaults so motor
    #    tests run against a known baseline (and the OLED visibly resets).
    print(f"\n[STEP] Snapshotting current configuration...")
    try:
        original = snapshot_config(link)
    except RuntimeError as e:
        suite.check("snapshot original config", False, str(e))
        return suite
    suite.check("snapshot original config", True,
                f"M1 lim_T={original['M1'].get('lim_T')} "
                f"M2 lim_L={original['M2'].get('lim_L')}")

    print(f"[STEP] RESET CONFIG (motor tests run on factory defaults)...")
    rsp = link.send("RESET CONFIG")
    expect_ok(suite, "RESET CONFIG accepted", rsp, "OK RESET CONFIG")

    defaults: dict[str, dict[str, str]] = {}
    try:
        defaults = snapshot_config(link)
        suite.check("snapshot factory defaults", True,
                    f"M1 lim_T={defaults['M1'].get('lim_T')} "
                    f"M2 lim_L={defaults['M2'].get('lim_L')}")
    except RuntimeError as e:
        suite.check("snapshot factory defaults", False, str(e))

    # ── Motor exercise on factory defaults ────────────────────────
    if not skip_motor:
        test_motor_all(link, suite)

    # ── Remaining protocol-level tests ────────────────────────────
    test_get_config(link, suite)
    test_set_get_roundtrip(link, suite)
    test_error_cases(link, suite)
    test_usb_only_verbs(link, suite, transport_is_usb)

    # Final safety stop in case anything went wrong above.
    link.send("STOP")

    # ── Restore the user's original config ────────────────────────
    if restore and original:
        print(f"\n[STEP] Restoring original configuration via SET commands...")
        # Wipe everything we just SET in the round-trip test, then push
        # the user's snapshot back field-by-field.
        link.send("RESET CONFIG")
        n = restore_config(link, original)
        print(f"        sent {n} SET commands")
        try:
            after = snapshot_config(link)
            mismatches = diff_configs(after, original)
            suite.check("restored config matches original snapshot",
                        len(mismatches) == 0,
                        f"{len(mismatches)} field(s) differ"
                        if mismatches else "")
            for tag, fname, ours, theirs in mismatches:
                print(f"        mismatch {tag} {fname}: now={ours} expected={theirs}")
        except RuntimeError as e:
            suite.check("verify restored config", False, str(e))

    # ── User-facing diff ──────────────────────────────────────────
    if defaults:
        print_config_diff(label, original, defaults)

    return suite


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--usb-port",   default=DEFAULT_USB_PORT)
    ap.add_argument("--rs485-port", default=DEFAULT_RS485_PORT)
    ap.add_argument("--baud",       type=int, default=DEFAULT_BAUD)
    ap.add_argument("--skip-usb",   action="store_true")
    ap.add_argument("--skip-rs485", action="store_true")
    ap.add_argument("--skip-motor", action="store_true",
                    help="Skip motor TIGHTEN/LOOSEN tests (config tests only)")
    ap.add_argument("--no-restore", action="store_true",
                    help="Skip restoring the original config (leave defaults)")
    args = ap.parse_args()

    if args.skip_usb and args.skip_rs485:
        print("[FAIL] Both transports skipped — nothing to do.", file=sys.stderr)
        return 2

    suites: list[Suite] = []

    # ── USB pass ──────────────────────────────────────────────────
    if not args.skip_usb:
        try:
            link = Link("USB", args.usb_port, args.baud)
        except serial.SerialException as e:
            print(f"[FAIL] Could not open USB port {args.usb_port}: {e}",
                  file=sys.stderr)
            return 3
        try:
            print(f"\n[INFO] Resetting controller via USB ({args.usb_port})...")
            link.reset_board()
            time.sleep(BOOT_SETTLE_S)
            link.drain()
            suites.append(run_suite(link, "USB-CDC", transport_is_usb=True,
                                    skip_motor=args.skip_motor,
                                    restore=not args.no_restore))
        finally:
            link.close()

    # ── RS-485 pass ──────────────────────────────────────────────
    if not args.skip_rs485:
        try:
            link = Link("RS485", args.rs485_port, args.baud)
        except serial.SerialException as e:
            print(f"[FAIL] Could not open RS-485 port {args.rs485_port}: {e}",
                  file=sys.stderr)
            return 4
        try:
            link.drain()
            suites.append(run_suite(link, "RS-485", transport_is_usb=False,
                                    skip_motor=args.skip_motor,
                                    restore=not args.no_restore))
        finally:
            link.close()

    # ── Summary ──────────────────────────────────────────────────
    print("\n══════════════════════════════════════════════════════════")
    print(" Summary")
    print("══════════════════════════════════════════════════════════")
    total_pass = sum(s.passed for s in suites)
    total_fail = sum(s.failed for s in suites)
    for s in suites:
        print(f"  {s.label:8s}  pass={s.passed:3d}  fail={s.failed:3d}")
    print(f"  ----------------------------------")
    print(f"  TOTAL     pass={total_pass:3d}  fail={total_fail:3d}")

    if total_fail:
        print("\n[FAIL] Some checks failed:")
        for f in (msg for s in suites for msg in s.failures):
            print(f"   - {f}")
        return 1

    print("\n[OK] All checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
