# verify_controller results

- Run started: 2026-04-23T13:41:57
- USB port:   `COM9`
- RS-485 port: `COM7`
- Baud:        115200
- Skip USB / RS-485 / motor: False / False / False
- Restore original config: True

```text

[INFO] Resetting controller via USB (COM9)...

══════════════════════════════════════════════════════════
 Running suite: USB-CDC  (port=COM9)
══════════════════════════════════════════════════════════

[GROUP] Handshake (USB-CDC)
  [PASS] PING -> OK PONG  — got: 'OK PONG'
  [PASS] VERSION format  — got: 'OK v1.0.0'

[GROUP] STATUS (USB-CDC)
  [PASS] STATUS matches '<s1> <s2> <mA> <pwm%>'  — got: 'OK IDLE IDLE 0 0'

[STEP] Snapshotting current configuration...
  [PASS] snapshot original config  — M1 lim_T=330 M2 lim_L=900
[STEP] RESET CONFIG (motor tests run on factory defaults)...
  [PASS] RESET CONFIG accepted  — got: 'OK RESET CONFIG'
  [PASS] snapshot factory defaults  — M1 lim_T=330 M2 lim_L=900

[GROUP] Motor exercise — using FACTORY DEFAULTS (USB-CDC)
  [PASS] TIGHTEN M1 accepted  — got: 'OK TIGHTEN M1'
  [PASS] STATUS during TIGHTEN M1 parses  — got: 'OK TIGHTENING IDLE 102 62'
  [PASS] TIGHTEN M1: motor state == TIGHTENING  — got M1=TIGHTENING M2=IDLE
  [PASS] TIGHTEN M1: current > 0 mA  — got 102 mA
  [PASS] TIGHTEN M1: pwm > 0 %  — got 62 %
  [PASS] STOP after TIGHTEN M1  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 0 0'
  [PASS] TIGHTEN M1: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] TIGHTEN M1: pwm == 0 after STOP  — got 0 %
  [PASS] LOOSEN M1 accepted  — got: 'OK LOOSEN M1'
  [PASS] STATUS during LOOSEN M1 parses  — got: 'OK LOOSENING IDLE 91 62'
  [PASS] LOOSEN M1: motor state == LOOSENING  — got M1=LOOSENING M2=IDLE
  [PASS] LOOSEN M1: current > 0 mA  — got 91 mA
  [PASS] LOOSEN M1: pwm > 0 %  — got 62 %
  [PASS] STOP after LOOSEN M1  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 3 0'
  [PASS] LOOSEN M1: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] LOOSEN M1: pwm == 0 after STOP  — got 0 %
  [PASS] TIGHTEN M2 accepted  — got: 'OK TIGHTEN M2'
  [PASS] STATUS during TIGHTEN M2 parses  — got: 'OK IDLE TIGHTENING 108 62'
  [PASS] TIGHTEN M2: motor state == TIGHTENING  — got M1=IDLE M2=TIGHTENING
  [PASS] TIGHTEN M2: current > 0 mA  — got 108 mA
  [PASS] TIGHTEN M2: pwm > 0 %  — got 62 %
  [PASS] STOP after TIGHTEN M2  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 0 0'
  [PASS] TIGHTEN M2: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] TIGHTEN M2: pwm == 0 after STOP  — got 0 %
  [PASS] LOOSEN M2 accepted  — got: 'OK LOOSEN M2'
  [PASS] STATUS during LOOSEN M2 parses  — got: 'OK IDLE LOOSENING 79 62'
  [PASS] LOOSEN M2: motor state == LOOSENING  — got M1=IDLE M2=LOOSENING
  [PASS] LOOSEN M2: current > 0 mA  — got 79 mA
  [PASS] LOOSEN M2: pwm > 0 %  — got 62 %
  [PASS] STOP after LOOSEN M2  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 0 0'
  [PASS] LOOSEN M2: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] LOOSEN M2: pwm == 0 after STOP  — got 0 %

[GROUP] GET CONFIG (USB-CDC)
  [PASS] GET CONFIG M1  — got: 'OK CONFIG M1 lim_T=330 lim_L=900 kp=0.000 ki=1.0000 ramp=200 pmin=102 pmax_T=160 pmax_L=160 pstart_T=102 pstart_L=155 slew_T=15 slew_L=500 gear=56.0'
  [PASS] GET CONFIG M2  — got: 'OK CONFIG M2 lim_T=330 lim_L=900 kp=0.000 ki=1.0000 ramp=200 pmin=102 pmax_T=160 pmax_L=160 pstart_T=102 pstart_L=155 slew_T=15 slew_L=500 gear=90.0'

[GROUP] SET/GET round-trip (USB-CDC)
  [PASS] SET LIMIT M1 T 333  — got: 'OK SET LIMIT M1 T 333'
  [PASS] GET LIMIT M1 T == 'OK LIMIT M1 T 333'  — got: 'OK LIMIT M1 T 333'
  [PASS] SET LIMIT M2 L 444  — got: 'OK SET LIMIT M2 L 444'
  [PASS] GET LIMIT M2 L == 'OK LIMIT M2 L 444'  — got: 'OK LIMIT M2 L 444'
  [PASS] SET KP M1 1.250  — got: 'OK SET KP M1 1.250'
  [PASS] GET KP M1 == 'OK KP M1 1.250'  — got: 'OK KP M1 1.250'
  [PASS] SET KI M2 0.0500  — got: 'OK SET KI M2 0.0500'
  [PASS] GET KI M2 == 'OK KI M2 0.0500'  — got: 'OK KI M2 0.0500'
  [PASS] SET SLEW M1 T 12  — got: 'OK SET SLEW M1 T 12'
  [PASS] GET SLEW M1 T == 'OK SLEW M1 T 12'  — got: 'OK SLEW M1 T 12'
  [PASS] SET RAMP M2 600  — got: 'OK SET RAMP M2 600'
  [PASS] GET RAMP M2 == 'OK RAMP M2 600'  — got: 'OK RAMP M2 600'
  [PASS] SET PWMMIN M1 30  — got: 'OK SET PWMMIN M1 30'
  [PASS] GET PWMMIN M1 == 'OK PWMMIN M1 30'  — got: 'OK PWMMIN M1 30'
  [PASS] SET PWMMAX M2 L 222  — got: 'OK SET PWMMAX M2 L 222'
  [PASS] GET PWMMAX M2 L == 'OK PWMMAX M2 L 222'  — got: 'OK PWMMAX M2 L 222'
  [PASS] SET PWMSTART M1 T 80  — got: 'OK SET PWMSTART M1 T 80'
  [PASS] GET PWMSTART M1 T == 'OK PWMSTART M1 T 80'  — got: 'OK PWMSTART M1 T 80'
  [PASS] SET GEAR M2 90.0  — got: 'OK SET GEAR M2 90.0'
  [PASS] GET GEAR M2 == 'OK GEAR M2 90.0'  — got: 'OK GEAR M2 90.0'

[GROUP] Error cases (USB-CDC)
  [PASS] Unknown verb -> ERR 1  — got: 'ERR 1 unknown command'
  [PASS] TIGHTEN M3 -> ERR 4  — got: 'ERR 4 motor must be M1 or M2'
  [PASS] SET LIMIT M1 X 100 -> ERR 5  — got: 'ERR 5 action must be T or L'
  [PASS] SET LIMIT M1 T 9999 -> ERR 3  — got: 'ERR 3 LIMIT must be 50..1500 mA'
  [PASS] GET KP -> ERR 3  — got: 'ERR 3 GET needs <param> <motor> [<action>]'

[GROUP] USB-only verbs (USB-CDC)
  [PASS] LOG accepted on USB  — got: 'OK LOG ON'
  [PASS] FAST accepted on USB  — got: 'OK FAST ON'
  [PASS] STATUSV prints output on USB  — lines=10
  [PASS] HELP prints output on USB  — lines=8

[STEP] Restoring original configuration via SET commands...
        sent 26 SET commands
  [PASS] restored config matches original snapshot

[USB-CDC] User config vs. factory defaults:
    (identical — controller was already at factory defaults)

══════════════════════════════════════════════════════════
 Running suite: RS-485  (port=COM7)
══════════════════════════════════════════════════════════

[GROUP] Handshake (RS-485)
  [PASS] PING -> OK PONG  — got: 'OK PONG'
  [PASS] VERSION format  — got: 'OK v1.0.0'

[GROUP] STATUS (RS-485)
  [PASS] STATUS matches '<s1> <s2> <mA> <pwm%>'  — got: 'OK IDLE IDLE 1 0'

[STEP] Snapshotting current configuration...
  [PASS] snapshot original config  — M1 lim_T=330 M2 lim_L=900
[STEP] RESET CONFIG (motor tests run on factory defaults)...
  [PASS] RESET CONFIG accepted  — got: 'OK RESET CONFIG'
  [PASS] snapshot factory defaults  — M1 lim_T=330 M2 lim_L=900

[GROUP] Motor exercise — using FACTORY DEFAULTS (RS-485)
  [PASS] TIGHTEN M1 accepted  — got: 'OK TIGHTEN M1'
  [PASS] STATUS during TIGHTEN M1 parses  — got: 'OK TIGHTENING IDLE 130 62'
  [PASS] TIGHTEN M1: motor state == TIGHTENING  — got M1=TIGHTENING M2=IDLE
  [PASS] TIGHTEN M1: current > 0 mA  — got 130 mA
  [PASS] TIGHTEN M1: pwm > 0 %  — got 62 %
  [PASS] STOP after TIGHTEN M1  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 0 0'
  [PASS] TIGHTEN M1: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] TIGHTEN M1: pwm == 0 after STOP  — got 0 %
  [PASS] LOOSEN M1 accepted  — got: 'OK LOOSEN M1'
  [PASS] STATUS during LOOSEN M1 parses  — got: 'OK LOOSENING IDLE 68 62'
  [PASS] LOOSEN M1: motor state == LOOSENING  — got M1=LOOSENING M2=IDLE
  [PASS] LOOSEN M1: current > 0 mA  — got 68 mA
  [PASS] LOOSEN M1: pwm > 0 %  — got 62 %
  [PASS] STOP after LOOSEN M1  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 2 0'
  [PASS] LOOSEN M1: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] LOOSEN M1: pwm == 0 after STOP  — got 0 %
  [PASS] TIGHTEN M2 accepted  — got: 'OK TIGHTEN M2'
  [PASS] STATUS during TIGHTEN M2 parses  — got: 'OK IDLE TIGHTENING 71 62'
  [PASS] TIGHTEN M2: motor state == TIGHTENING  — got M1=IDLE M2=TIGHTENING
  [PASS] TIGHTEN M2: current > 0 mA  — got 71 mA
  [PASS] TIGHTEN M2: pwm > 0 %  — got 62 %
  [PASS] STOP after TIGHTEN M2  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 5 0'
  [PASS] TIGHTEN M2: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] TIGHTEN M2: pwm == 0 after STOP  — got 0 %
  [PASS] LOOSEN M2 accepted  — got: 'OK LOOSEN M2'
  [PASS] STATUS during LOOSEN M2 parses  — got: 'OK IDLE LOOSENING 64 62'
  [PASS] LOOSEN M2: motor state == LOOSENING  — got M1=IDLE M2=LOOSENING
  [PASS] LOOSEN M2: current > 0 mA  — got 64 mA
  [PASS] LOOSEN M2: pwm > 0 %  — got 62 %
  [PASS] STOP after LOOSEN M2  — got: 'OK STOP'
  [PASS] STATUS after STOP parses  — got: 'OK IDLE IDLE 3 0'
  [PASS] LOOSEN M2: motor returned to IDLE  — got M1=IDLE M2=IDLE
  [PASS] LOOSEN M2: pwm == 0 after STOP  — got 0 %

[GROUP] GET CONFIG (RS-485)
  [PASS] GET CONFIG M1  — got: 'OK CONFIG M1 lim_T=330 lim_L=900 kp=0.000 ki=1.0000 ramp=200 pmin=102 pmax_T=160 pmax_L=160 pstart_T=102 pstart_L=155 slew_T=15 slew_L=500 gear=56.0'
  [PASS] GET CONFIG M2  — got: 'OK CONFIG M2 lim_T=330 lim_L=900 kp=0.000 ki=1.0000 ramp=200 pmin=102 pmax_T=160 pmax_L=160 pstart_T=102 pstart_L=155 slew_T=15 slew_L=500 gear=90.0'

[GROUP] SET/GET round-trip (RS-485)
  [PASS] SET LIMIT M1 T 333  — got: 'OK SET LIMIT M1 T 333'
  [PASS] GET LIMIT M1 T == 'OK LIMIT M1 T 333'  — got: 'OK LIMIT M1 T 333'
  [PASS] SET LIMIT M2 L 444  — got: 'OK SET LIMIT M2 L 444'
  [PASS] GET LIMIT M2 L == 'OK LIMIT M2 L 444'  — got: 'OK LIMIT M2 L 444'
  [PASS] SET KP M1 1.250  — got: 'OK SET KP M1 1.250'
  [PASS] GET KP M1 == 'OK KP M1 1.250'  — got: 'OK KP M1 1.250'
  [PASS] SET KI M2 0.0500  — got: 'OK SET KI M2 0.0500'
  [PASS] GET KI M2 == 'OK KI M2 0.0500'  — got: 'OK KI M2 0.0500'
  [PASS] SET SLEW M1 T 12  — got: 'OK SET SLEW M1 T 12'
  [PASS] GET SLEW M1 T == 'OK SLEW M1 T 12'  — got: 'OK SLEW M1 T 12'
  [PASS] SET RAMP M2 600  — got: 'OK SET RAMP M2 600'
  [PASS] GET RAMP M2 == 'OK RAMP M2 600'  — got: 'OK RAMP M2 600'
  [PASS] SET PWMMIN M1 30  — got: 'OK SET PWMMIN M1 30'
  [PASS] GET PWMMIN M1 == 'OK PWMMIN M1 30'  — got: 'OK PWMMIN M1 30'
  [PASS] SET PWMMAX M2 L 222  — got: 'OK SET PWMMAX M2 L 222'
  [PASS] GET PWMMAX M2 L == 'OK PWMMAX M2 L 222'  — got: 'OK PWMMAX M2 L 222'
  [PASS] SET PWMSTART M1 T 80  — got: 'OK SET PWMSTART M1 T 80'
  [PASS] GET PWMSTART M1 T == 'OK PWMSTART M1 T 80'  — got: 'OK PWMSTART M1 T 80'
  [PASS] SET GEAR M2 90.0  — got: 'OK SET GEAR M2 90.0'
  [PASS] GET GEAR M2 == 'OK GEAR M2 90.0'  — got: 'OK GEAR M2 90.0'

[GROUP] Error cases (RS-485)
  [PASS] Unknown verb -> ERR 1  — got: 'ERR 1 unknown command'
  [PASS] TIGHTEN M3 -> ERR 4  — got: 'ERR 4 motor must be M1 or M2'
  [PASS] SET LIMIT M1 X 100 -> ERR 5  — got: 'ERR 5 action must be T or L'
  [PASS] SET LIMIT M1 T 9999 -> ERR 3  — got: 'ERR 3 LIMIT must be 50..1500 mA'
  [PASS] GET KP -> ERR 3  — got: 'ERR 3 GET needs <param> <motor> [<action>]'

[GROUP] USB-only verbs (RS-485)
  [PASS] LOG rejected on RS-485  — got: 'ERR 1 unknown command'
  [PASS] FAST rejected on RS-485  — got: 'ERR 1 unknown command'
  [PASS] STATUSV rejected on RS-485  — got: 'ERR 1 unknown command'
  [PASS] HELP rejected on RS-485  — got: 'ERR 1 unknown command'

[STEP] Restoring original configuration via SET commands...
        sent 26 SET commands
  [PASS] restored config matches original snapshot

[RS-485] User config vs. factory defaults:
    (identical — controller was already at factory defaults)

══════════════════════════════════════════════════════════
 Summary
══════════════════════════════════════════════════════════
  USB-CDC   pass= 74  fail=  0
  RS-485    pass= 74  fail=  0
  ----------------------------------
  TOTAL     pass=148  fail=  0

[OK] All checks passed.
```
