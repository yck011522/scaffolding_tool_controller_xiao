// motor_config.h — Per-motor configuration and runtime state types
//
// MotorConfig holds everything tunable (pins, PWM envelope, current limits,
// PI gains, ramp). Motor holds runtime state only. Two parallel instances
// of each (one per motor) keep M1 and M2 fully independent so divergent
// tuning is easy to spot and adjust.

#pragma once

#include <Arduino.h>

// Number of motors. Sized [2] arrays inside MotorConfig assume this is 2.
constexpr int NUM_MOTORS = 2;

// ── Enums ───────────────────────────────────────────────────────────
// Per-motor state machine. Transitions:
//   IDLE ──(start cmd)──► RUNNING ──(stall)──► STALLED ──(STOP)──► IDLE
enum MotorState
{
    STATE_IDLE,      // motor stopped, accepts new commands
    STATE_RUNNING,   // motor active, current-limited PI loop running
    STATE_STALLED    // motor stopped after stall; STOP required to reset
};

// Direction of work the motor is doing. Maps to DIR pin level:
//   TIGHTEN → DIR=1   LOOSEN → DIR=0   (NONE only valid when IDLE)
enum MotorAction
{
    ACTION_NONE,
    ACTION_TIGHTEN,
    ACTION_LOOSEN
};

// Convert MotorAction into an index for arrays sized [2].
//   0 = tighten, 1 = loosen.
// Used everywhere we have a per-action setting (limitMa, pwmStart, ...).
static inline int actionIdx(MotorAction a) { return (a == ACTION_LOOSEN) ? 1 : 0; }

// ── Per-motor configuration (all tunable, all per-motor) ───────────
// Two instances live in cfg[2] (motor_control.cpp). All M1/M2 differences
// — pins, PWM envelope, current limit, PI gains, ramp — live here so that
// the two motors can be tuned fully independently. Plain `int`/`float`
// (not `const`) so serial commands can edit every field at runtime.
struct MotorConfig
{
    // ── Hardware wiring ───────────────────────────────────────────
    int pinPWM;                             // LEDC PWM output to driver
    int pinDIR;                             // direction signal to driver
    int pinCAP;                             // encoder pulse input (RISING IRQ)
    int pwmChannel;                         // LEDC channel index (0..7)
    volatile unsigned long *pulseCountPtr;  // address of this motor's ISR counter
    float gearRatio;                        // motor-rev / shaft-rev (display only)

    // ── PWM envelope (per-motor) ──────────────────────────────────
    // Motors live inside a band [pwmMin, pwmMax]. Below pwmMin the motor
    // physically stalls; above pwmMax we'd exceed the LEDC bit width.
    int pwmMin;          // dead-band floor (~40% duty for these motors)
    int pwmMax;          // absolute hardware ceiling (≤ 255 for 8-bit LEDC)
    int pwmStart[2];     // startup kick PWM         [tighten, loosen]
    int pwmCeiling[2];   // per-action output cap    [tighten, loosen]
                         //   ← lower this to reduce current-spike risk
    int slew[2];         // max PWM change per control cycle [T, L]
                         //   small T = smooth tighten, large L = impact-driver

    // ── Current limits (PI setpoint) ──────────────────────────────
    // Active limit is copied to Motor.activeLimitMa at startMotor() time.
    float limitMa[2];    // mA setpoint              [tighten, loosen]

    // ── Velocity-form PI gains (per motor) ────────────────────────
    // ΔPWM = kp·Δerror + ki·error·dt   (integrated each control cycle)
    float kp;
    float ki;

    // ── Soft-start ramp ───────────────────────────────────────────
    // Linearly raises the allowed PWM ceiling from pwmStart up to
    // pwmCeiling[action] over rampMs after a motor starts. Prevents
    // an instant jump that would create a current spike.
    unsigned long rampMs;
};

// ── Per-motor runtime state (no tuning here — only "what's happening now") ─
struct Motor
{
    MotorState state;        // IDLE / RUNNING / STALLED
    MotorAction action;      // current direction of work (NONE if IDLE)
    int pwmValue;            // current LEDC duty (0–255), updated every cycle
    float activeLimitMa;     // copy of cfg[i].limitMa[action] taken at startMotor()
                             //   so changing cfg.limitMa mid-run only takes effect
                             //   when the cmd handler explicitly refreshes it

    float motorRpm;          // motor-side RPM (shaft RPM = motorRpm / gearRatio)
    float integralError;     // ∑ error·dt — kept for logging only, not used by PI
    float prevError;          // previous-cycle error for velocity-form PI

    // RPM measurement (updated at ≤20 Hz inside updateMotorRpm)
    unsigned long lastRpmTime;     // when we last computed RPM
    unsigned long lastPulseSnap;   // pulse count at lastRpmTime

    // Stall detection
    unsigned long stallStartTime;  // when low-RPM condition first appeared (0 = none)
    unsigned long lastPulseTime;   // last time the encoder produced a pulse
    unsigned long runStartTime;    // when this run started — used for grace + ramp

    // Control loop
    unsigned long lastControlTime; // last time controlLoop() executed for this motor
};

// ── Stringification helpers ─────────────────────────────────────────
const char *stateStr(MotorState s);
const char *actionStr(MotorAction a);
