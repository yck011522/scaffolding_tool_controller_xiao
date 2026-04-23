// motor_control.cpp — see motor_control.h for the API.

#include "motor_control.h"

// ── Stall detection ────────────────────────────────────────────────
const float STALL_RPM_THRESHOLD = 500.0;
const unsigned long STALL_CONFIRM_MS = 200;
const unsigned long PULSE_TIMEOUT_MS = 200;
const unsigned long STARTUP_GRACE_MS = 700;

// ── Control loop period ────────────────────────────────────────────
const unsigned long CONTROL_INTERVAL_MS = 10;   // 100 Hz

// ── PWM hardware (LEDC) ────────────────────────────────────────────
static const int PWM_FREQ = 20000;       // 20 kHz
static const int PWM_RESOLUTION = 8;     // 0–255

// ── INA240 current sensor ──────────────────────────────────────────
const int PIN_INA_OUT = 13;
static const float INA_GAIN = 20.0;
static const float SHUNT_OHM = 0.1;      // 100 mΩ
static const int ADC_SAMPLES = 16;
static const float ADC_MAX = 4095.0;
static const float ADC_VREF_MV = 3100.0;
float currentOffsetMa = 0.0;

// ── Pulse counters (one per motor) ─────────────────────────────────
// Incremented by encoder ISRs (RISING edge on CAP pin). Shared with the
// main loop — reads must be wrapped in noInterrupts()/interrupts().
volatile unsigned long m1PulseCount = 0;
volatile unsigned long m2PulseCount = 0;
void IRAM_ATTR onM1Pulse() { m1PulseCount++; }
void IRAM_ATTR onM2Pulse() { m2PulseCount++; }

// ── Per-motor configuration ────────────────────────────────────────
// THE ONE PLACE TO TUNE M1/M2 INDEPENDENTLY. Both motors are listed in
// the same shape so a parameter change on M1 vs M2 is instantly visible.
// Indices: array[0] = tighten, array[1] = loosen.
//                                       [── tighten ──]   [── loosen ──]
MotorConfig cfg[NUM_MOTORS] = {
    // Motor 1 — gripper (PWM=GPIO6, DIR=GPIO5, CAP=GPIO7)
    {
        /* pinPWM */ 6,  /* pinDIR */ 5,  /* pinCAP */ 7,
        /* pwmChannel */ 0,
        /* pulseCountPtr */ &m1PulseCount,
        /* gearRatio */ 56.0f,

        /* pwmMin */ 102,
        /* pwmMax */ 255,
        /* pwmStart   */ { 102, 155 },
        /* pwmCeiling */ { 160, 160 },
        /* slew       */ {  15, 500 },

        /* limitMa    */ { 330.0f, 900.0f },

        /* kp */ 0.0f,
        /* ki */ 1.0f,
        /* rampMs */ 200,
    },
    // Motor 2 — tightening (PWM=GPIO10, DIR=GPIO9, CAP=GPIO11)
    {
        /* pinPWM */ 10, /* pinDIR */ 9, /* pinCAP */ 11,
        /* pwmChannel */ 1,
        /* pulseCountPtr */ &m2PulseCount,
        /* gearRatio */ 90.0f,

        /* pwmMin */ 102,
        /* pwmMax */ 255,
        /* pwmStart   */ { 102, 155 },
        /* pwmCeiling */ { 160, 160 },
        /* slew       */ {  15, 500 },

        /* limitMa    */ { 330.0f, 900.0f },

        /* kp */ 0.0f,
        /* ki */ 1.0f,
        /* rampMs */ 200,
    },
};

// Immutable factory defaults. Used by motorLoadDefaults() so RESET CONFIG
// can restore the in-memory cfg[] to the as-shipped values without a reboot.
// Keep this in lock-step with the cfg[] initializer above.
static const MotorConfig kDefaults[NUM_MOTORS] = {
    {
        /* pinPWM */ 6,  /* pinDIR */ 5,  /* pinCAP */ 7,
        /* pwmChannel */ 0,
        /* pulseCountPtr */ &m1PulseCount,
        /* gearRatio */ 56.0f,
        /* pwmMin */ 102,
        /* pwmMax */ 255,
        /* pwmStart   */ { 102, 155 },
        /* pwmCeiling */ { 160, 160 },
        /* slew       */ {  15, 500 },
        /* limitMa    */ { 330.0f, 900.0f },
        /* kp */ 0.0f,
        /* ki */ 1.0f,
        /* rampMs */ 200,
    },
    {
        /* pinPWM */ 10, /* pinDIR */ 9, /* pinCAP */ 11,
        /* pwmChannel */ 1,
        /* pulseCountPtr */ &m2PulseCount,
        /* gearRatio */ 90.0f,
        /* pwmMin */ 102,
        /* pwmMax */ 255,
        /* pwmStart   */ { 102, 155 },
        /* pwmCeiling */ { 160, 160 },
        /* slew       */ {  15, 500 },
        /* limitMa    */ { 330.0f, 900.0f },
        /* kp */ 0.0f,
        /* ki */ 1.0f,
        /* rampMs */ 200,
    },
};

// Restore tunable fields of cfg[] from the immutable kDefaults table.
// Hardware-binding fields (pins, pwmChannel, pulseCountPtr) are not
// touched — they're set once at boot and must never change.
void motorLoadDefaults()
{
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        cfg[i].gearRatio   = kDefaults[i].gearRatio;
        cfg[i].pwmMin      = kDefaults[i].pwmMin;
        cfg[i].pwmMax      = kDefaults[i].pwmMax;
        cfg[i].kp          = kDefaults[i].kp;
        cfg[i].ki          = kDefaults[i].ki;
        cfg[i].rampMs      = kDefaults[i].rampMs;
        for (int a = 0; a < 2; a++)
        {
            cfg[i].pwmStart[a]   = kDefaults[i].pwmStart[a];
            cfg[i].pwmCeiling[a] = kDefaults[i].pwmCeiling[a];
            cfg[i].slew[a]       = kDefaults[i].slew[a];
            cfg[i].limitMa[a]    = kDefaults[i].limitMa[a];
        }
    }
}

Motor motors[NUM_MOTORS];     // runtime state, populated by setupMotors()
int activeMotor = -1;         // which motor is currently RUNNING (-1 = none)
bool fastLogEnabled = false;  // when true, controlLoop emits a CSV row every cycle

// ── Stringification ────────────────────────────────────────────────
const char *stateStr(MotorState s)
{
    switch (s)
    {
    case STATE_IDLE:    return "IDLE";
    case STATE_RUNNING: return "RUNNING";
    case STATE_STALLED: return "STALLED";
    default:            return "?";
    }
}

const char *actionStr(MotorAction a)
{
    switch (a)
    {
    case ACTION_TIGHTEN: return "TIGHTEN";
    case ACTION_LOOSEN:  return "LOOSEN";
    default:             return "NONE";
    }
}

// ═══════════════════════════════════════════════════════════════════
// Current reading
//
//   Average ADC_SAMPLES raw reads, convert to mV, then mA via the INA240
//   transfer function. Subtract the boot-time offset so 0 mA reads as 0.
// ════════════════════════════════════════════════════════════════════
float readCurrentMa()
{
    long sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++)
        sum += analogRead(PIN_INA_OUT);
    float rawAdc = (float)sum / ADC_SAMPLES;
    float voltageMv = (rawAdc / ADC_MAX) * ADC_VREF_MV;
    return voltageMv / (INA_GAIN * SHUNT_OHM) - currentOffsetMa;
}

// One-shot calibration: motors must be OFF when this runs. Captures the
// quiescent ADC reading and stores it as the offset so subsequent reads
// of "no load" report ~0 mA instead of the INA240's bias voltage.
void calibrateCurrentSensor()
{
    Serial.print("[CAL] Calibrating current sensor");
    const int CAL_READINGS = 20;
    float calSum = 0.0;
    // Take CAL_READINGS averages, 50 ms apart, while motors are off.
    for (int i = 0; i < CAL_READINGS; i++)
    {
        long adcSum = 0;
        for (int j = 0; j < ADC_SAMPLES; j++)
            adcSum += analogRead(PIN_INA_OUT);
        float rawMv = ((float)(adcSum / ADC_SAMPLES) / ADC_MAX) * ADC_VREF_MV;
        calSum += rawMv / (INA_GAIN * SHUNT_OHM);
        delay(50);
        Serial.print(".");
    }
    currentOffsetMa = calSum / CAL_READINGS;
    Serial.println(" done");
    Serial.printf("[CAL] Baseline offset: %.1f mA\n", currentOffsetMa);
}

// ═══════════════════════════════════════════════════════════════════
// RPM measurement
//
//   Throttled to 20 Hz (50 ms). Each call snapshots the ISR pulse counter
//   and computes pulses/sec → motor RPM using PULSES_PER_REV. Also tracks
//   lastPulseTime so stall detection can spot a stuck shaft.
// ════════════════════════════════════════════════════════════════════
static const int PULSES_PER_REV = 6;     // hall-effect encoder pulses per motor rev

void updateMotorRpm(Motor &m)
{
    unsigned long now = millis();
    unsigned long elapsed = now - m.lastRpmTime;
    if (elapsed < 50)   // throttle: only recompute at ≈20 Hz
        return;

    // Find which counter belongs to this motor by comparing pointers.
    // Avoids storing a back-reference in Motor; cfg[i] already knows.
    volatile unsigned long *counter = nullptr;
    for (int i = 0; i < NUM_MOTORS; i++)
        if (&motors[i] == &m) { counter = cfg[i].pulseCountPtr; break; }
    if (!counter) return;

    // Atomic snapshot of the ISR-updated counter.
    noInterrupts();
    unsigned long count = *counter;
    interrupts();

    unsigned long delta = count - m.lastPulseSnap;
    m.lastPulseSnap = count;
    m.lastRpmTime = now;

    if (delta > 0)
        m.lastPulseTime = now;   // remember last activity for stall detection

    float pulsesPerSec = (float)delta * 1000.0 / (float)elapsed;
    m.motorRpm = (pulsesPerSec / PULSES_PER_REV) * 60.0;
}

// ═══════════════════════════════════════════════════════════════════
// Hardware helpers
// ═══════════════════════════════════════════════════════════════════
void setMotorPWM(Motor &m, int value)
{
    if (value < 0)   value = 0;
    if (value > 255) value = 255;
    m.pwmValue = value;
    // Look up channel from cfg
    for (int i = 0; i < NUM_MOTORS; i++)
        if (&motors[i] == &m) { ledcWrite(cfg[i].pwmChannel, value); return; }
}

static void setMotorDir(Motor &m, int dir)
{
    for (int i = 0; i < NUM_MOTORS; i++)
        if (&motors[i] == &m) { digitalWrite(cfg[i].pinDIR, dir ? HIGH : LOW); return; }
}

void stopMotorHW(Motor &m)
{
    setMotorPWM(m, 0);
    m.integralError = 0.0;
}

// ═══════════════════════════════════════════════════════════════════
// Setup
//
//   For each motor: zero its runtime state, configure DIR/PWM/CAP pins,
//   attach the LEDC channel, install the encoder ISR, prime the timing
//   fields. Also configures the shared ADC for the current sensor.
// ═══════════════════════════════════════════════════════════════════
void setupMotors()
{
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        const MotorConfig &c = cfg[i];
        Motor &m = motors[i];
        m = {};
        m.state = STATE_IDLE;
        m.action = ACTION_NONE;

        pinMode(c.pinDIR, OUTPUT);
        digitalWrite(c.pinDIR, LOW);

        ledcSetup(c.pwmChannel, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(c.pinPWM, c.pwmChannel);
        ledcWrite(c.pwmChannel, 0);

        pinMode(c.pinCAP, INPUT);
    }

    attachInterrupt(digitalPinToInterrupt(cfg[0].pinCAP), onM1Pulse, RISING);
    attachInterrupt(digitalPinToInterrupt(cfg[1].pinCAP), onM2Pulse, RISING);

    pinMode(PIN_INA_OUT, INPUT);
    analogSetAttenuation(ADC_11db);

    unsigned long now = millis();
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        motors[i].lastRpmTime = now;
        motors[i].lastPulseTime = now;
    }
}

// ═══════════════════════════════════════════════════════════════════
// State transitions
//
//   startMotor()   : IDLE/RUNNING → RUNNING  (refuses if STALLED)
//   stopAllMotors(): any → IDLE
//   enterStalled() : RUNNING → STALLED  (called from controlLoop)
//
//   Only one motor can be active at a time. Starting a second motor
//   preempts (stops) the first.
// ═══════════════════════════════════════════════════════════════════
void startMotor(int motorIdx, MotorAction action)
{
    if (motorIdx < 0 || motorIdx >= NUM_MOTORS) return;

    const MotorConfig &c = cfg[motorIdx];
    Motor &m = motors[motorIdx];
    int aIdx = actionIdx(action);

    // Resolve effective starting and ceiling PWM, clamped into
    // [pwmMin, pwmMax] so cfg edits can't drive us out of range.
    int startPwm = c.pwmStart[aIdx];
    int ceiling  = c.pwmCeiling[aIdx];
    if (ceiling < c.pwmMin) ceiling = c.pwmMin;
    if (ceiling > c.pwmMax) ceiling = c.pwmMax;
    if (startPwm > ceiling) startPwm = ceiling;
    if (startPwm < c.pwmMin) startPwm = c.pwmMin;

    // Failsafe: a stalled motor must be acknowledged with STOP before
    // it can run again. Prevents repeatedly slamming into a jam.
    if (m.state == STATE_STALLED)
    {
        Serial.printf("[M%d] REJECTED — motor is STALLED, send STOP to reset\n",
                      motorIdx + 1);
        return;
    }

    // Preempt the other motor if it's running. Only one motor active.
    if (activeMotor >= 0 && activeMotor != motorIdx)
    {
        Motor &prev = motors[activeMotor];
        stopMotorHW(prev);
        prev.state = STATE_IDLE;
        prev.action = ACTION_NONE;
        Serial.printf("[M%d] Stopped (preempted by M%d)\n",
                      activeMotor + 1, motorIdx + 1);
    }

    // No-op if we're already running this exact action (debounces buttons).
    if (m.state == STATE_RUNNING && m.action == action)
        return;

    // Set hardware direction and snapshot the active current limit.
    setMotorDir(m, (action == ACTION_TIGHTEN) ? 1 : 0);
    m.activeLimitMa = c.limitMa[aIdx];

    // Reset PI state and timing fields for a clean start.
    m.integralError = 0.0;
    m.prevError = 0.0;
    m.lastControlTime = millis();
    m.runStartTime = millis();
    m.stallStartTime = 0;

    // Re-baseline pulse counting so previous run's pulses don't get
    // mistaken for current activity.
    noInterrupts();
    m.lastPulseSnap = *c.pulseCountPtr;
    interrupts();
    m.lastRpmTime = millis();
    m.lastPulseTime = millis();
    m.motorRpm = 0.0;

    // Apply the startup kick. From here, controlLoop() takes over.
    setMotorPWM(m, startPwm);

    m.state = STATE_RUNNING;
    m.action = action;
    activeMotor = motorIdx;

    Serial.printf("[M%d] %s — starting at PWM %d%%, limit %d mA (Kp=%.2f Ki=%.3f)\n",
                  motorIdx + 1, actionStr(action),
                  (int)(startPwm * 100 / 255), (int)m.activeLimitMa,
                  c.kp, c.ki);
}

void stopAllMotors()
{
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        stopMotorHW(motors[i]);
        if (motors[i].state != STATE_IDLE)
            Serial.printf("[M%d] %s → IDLE (stopped)\n",
                          i + 1, stateStr(motors[i].state));
        motors[i].state = STATE_IDLE;
        motors[i].action = ACTION_NONE;
    }
    activeMotor = -1;
}

void enterStalled(int motorIdx)
{
    Motor &m = motors[motorIdx];
    stopMotorHW(m);
    m.state = STATE_STALLED;
    activeMotor = -1;
    Serial.printf("[M%d] *** STALLED *** RPM=%.0f — send STOP to reset\n",
                  motorIdx + 1, m.motorRpm);
}

// ═══════════════════════════════════════════════════════════════════
// Current-limiting control loop
//
//   Per cycle (every CONTROL_INTERVAL_MS):
//     1. Read current and update RPM.
//     2. Stall detection (skipped during STARTUP_GRACE_MS).
//     3. Velocity-form PI: ΔPWM = kp·Δerr + ki·err·dt, applied to pwmValue.
//     4. Clamp to [pwmMin, ceiling].
//     5. Apply soft-start ramp ceiling (linear from startPwm → ceiling
//        over rampMs).
//     6. Apply slew-rate limit (max change per cycle).
//     7. Write PWM.
// ═══════════════════════════════════════════════════════════════════
void controlLoop(int motorIdx)
{
    if (motorIdx < 0 || motorIdx >= NUM_MOTORS) return;

    const MotorConfig &c = cfg[motorIdx];
    Motor &m = motors[motorIdx];
    if (m.state != STATE_RUNNING) return;

    // Throttle to the configured cycle period (default 10 ms = 100 Hz).
    unsigned long now = millis();
    if (now - m.lastControlTime < CONTROL_INTERVAL_MS) return;
    m.lastControlTime = now;

    // Recompute the action-specific PWM bracket every cycle so runtime
    // edits to cfg take effect immediately (e.g. PWMMAX M2 T 130).
    int aIdx = actionIdx(m.action);
    int startPwm = c.pwmStart[aIdx];
    int ceiling  = c.pwmCeiling[aIdx];
    if (ceiling < c.pwmMin) ceiling = c.pwmMin;
    if (ceiling > c.pwmMax) ceiling = c.pwmMax;
    if (startPwm > ceiling) startPwm = ceiling;
    if (startPwm < c.pwmMin) startPwm = c.pwmMin;

    float currentMa = readCurrentMa();
    updateMotorRpm(m);

    // Stall detection — only after the startup grace window so the
    // soft-start ramp doesn't trigger a false stall.
    if (now - m.runStartTime > STARTUP_GRACE_MS)
    {
        bool pulseTimeout = (now - m.lastPulseTime > PULSE_TIMEOUT_MS);
        // lowRpm requires pwmValue ≥ pwmMin so we don't flag stalls
        // when the controller has intentionally held the duty low.
        bool lowRpm = (m.motorRpm < STALL_RPM_THRESHOLD) && (m.pwmValue >= c.pwmMin);

        if (pulseTimeout || lowRpm)
        {
            // Require the condition to persist for STALL_CONFIRM_MS
            // to avoid tripping on transient dips.
            if (m.stallStartTime == 0)
                m.stallStartTime = now;
            if (now - m.stallStartTime >= STALL_CONFIRM_MS)
            {
                enterStalled(motorIdx);
                return;
            }
        }
        else
        {
            m.stallStartTime = 0;   // condition cleared
        }
    }

    // Velocity-form PI controller ───────────────────────────────────
    // Positive error = below the limit → increase PWM.
    // Negative error = above the limit → decrease PWM.
    float error = m.activeLimitMa - currentMa;
    float deltaError = error - m.prevError;
    m.prevError = error;
    float dt = CONTROL_INTERVAL_MS / 1000.0;

    float deltaPWM = c.kp * deltaError + c.ki * error * dt;
    int newPWM = m.pwmValue + (int)deltaPWM;

    m.integralError += error * dt;   // logged only — not part of the law

    // Clamp into the action-specific bracket.
    if (newPWM < c.pwmMin) newPWM = c.pwmMin;
    if (newPWM > ceiling)  newPWM = ceiling;

    // Soft-start ramp: linearly raise the effective ceiling from
    // startPwm to `ceiling` over rampMs. Prevents an immediate jump
    // to full-ceiling that would create a current spike.
    unsigned long elapsed = now - m.runStartTime;
    if (elapsed < c.rampMs)
    {
        int rampCeiling = startPwm + (int)((long)(ceiling - startPwm) * elapsed / c.rampMs);
        if (newPWM > rampCeiling) newPWM = rampCeiling;
    }

    // Slew-rate limit: cap per-cycle PWM change. Small slew on tighten
    // gives a smooth squeeze; large slew on loosen gives an impact-driver
    // effect for breaking initial torque.
    int slewMax = c.slew[aIdx];
    int delta = newPWM - m.pwmValue;
    if (delta >  slewMax) newPWM = m.pwmValue + slewMax;
    if (delta < -slewMax) newPWM = m.pwmValue - slewMax;
    if (newPWM < c.pwmMin) newPWM = c.pwmMin;   // re-clamp after slew

    setMotorPWM(m, newPWM);

    // Per-cycle CSV row when fast logging is on (FAST command).
    if (fastLogEnabled)
    {
        Serial.printf("FAST,%lu,%d,%.1f,%d,%.0f,%.1f\n",
                      now, motorIdx + 1, currentMa,
                      m.pwmValue, m.motorRpm, m.integralError);
    }
}
