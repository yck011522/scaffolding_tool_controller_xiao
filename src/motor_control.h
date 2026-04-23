// motor_control.h
//
// Public API for the motor subsystem. Owns:
//   - cfg[NUM_MOTORS]   : per-motor tunable configuration
//   - motors[NUM_MOTORS]: per-motor runtime state
//   - the INA240 current sensor read path
//   - the velocity-form PI control loop and stall detection
//
// All tunables (PWM bracket, PI gains, slew, ramp, current limit) live in
// MotorConfig so M1 and M2 can be tuned independently. See motor_config.h
// for the field-by-field documentation.

#pragma once

#include "motor_config.h"

// ── Stall detection (shared by both motors) ─────────────────────────
extern const float STALL_RPM_THRESHOLD;       // motor RPM below → stall candidate
extern const unsigned long STALL_CONFIRM_MS;  // must persist this long to confirm
extern const unsigned long PULSE_TIMEOUT_MS;  // no pulses for this long → stall
extern const unsigned long STARTUP_GRACE_MS;  // suppress detection during ramp-up

// ── Control loop period ─────────────────────────────────────────────
extern const unsigned long CONTROL_INTERVAL_MS;

// ── INA240 current sensor (shared 24V rail, both motors) ─────────────
extern const int PIN_INA_OUT;     // ADC pin tied to INA240 OUT
extern float currentOffsetMa;     // boot-time quiescent offset (subtracted)
float readCurrentMa();            // averaged ADC → calibrated mA
void calibrateCurrentSensor();    // blocking; call once with motors off

// ── Per-motor data (defined in motor_control.cpp) ──────────────────
extern MotorConfig cfg[NUM_MOTORS];   // tuning  — edit here for permanent changes
extern Motor motors[NUM_MOTORS];      // runtime — read-only for outside code
extern int activeMotor;               // -1 when idle, else 0 or 1

// Pulse-counting ISRs (declared so attachInterrupt() can find them)
void IRAM_ATTR onM1Pulse();
void IRAM_ATTR onM2Pulse();

// ── Logging flags (toggled by serial commands, read by control loop) ─
extern bool fastLogEnabled;       // when true, controlLoop emits CSV every cycle

// ── Lifecycle ─────────────────────────────────────────────────
void setupMotors();               // configure pins, LEDC, ISRs (call once)
void controlLoop(int motorIdx);   // call from main loop for activeMotor
void motorLoadDefaults();         // restore tunable cfg[] from immutable defaults

// ── State transitions ────────────────────────────────────────────
void startMotor(int motorIdx, MotorAction action);  // refused if STALLED
void stopAllMotors();                                // any state → IDLE
void enterStalled(int motorIdx);                     // RUNNING → STALLED

// ── Low-level (exposed for buttons.cpp release-stop path) ───────────
void stopMotorHW(Motor &m);              // 0 PWM and clear integrator (no state change)
void setMotorPWM(Motor &m, int value);   // clamp to 0..255 and write LEDC

// ── RPM (throttled internally to ~20 Hz) ─────────────────────────
void updateMotorRpm(Motor &m);
