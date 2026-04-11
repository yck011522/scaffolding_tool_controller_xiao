// test_motor_control — Current-limited motor control with stall detection
//
// Board: Waveshare ESP32-S3-Tiny
//
// Drives two BLDC motors with current-limiting control. A PI controller
// adjusts PWM duty cycle to keep total current draw at or below a
// configurable target. Stall detection triggers automatic shutdown.
//
// State machine (per motor):
//   IDLE ──(tighten/loosen cmd)──► RUNNING ──(stall detected)──► STALLED
//     ▲                              │                              │
//     └──────(stop/reset cmd)────────┘──────(stop/reset cmd)────────┘
//
//   - IDLE:    Motor stopped. Accepts new movement commands.
//   - RUNNING: Motor active with current limiting. Monitors for stall.
//   - STALLED: Motor stopped after stall. Must receive STOP to return
//              to IDLE before accepting new commands (failsafe).
//
// Only one motor runs at a time. Starting a second motor stops the first.
//
// Button mapping (active LOW, internal pull-up):
//   Button 1 (GPIO1): Motor 1 tighten  (hold to run, release to stop)
//   Button 2 (GPIO2): Motor 1 loosen
//   Button 3 (GPIO3): Motor 2 tighten
//   Button 4 (GPIO4): Motor 2 loosen
//
// Serial commands (115200 baud, USB-C — same protocol for future RS-485):
//   M1 TIGHTEN      Start motor 1 tightening
//   M1 LOOSEN       Start motor 1 loosening
//   M2 TIGHTEN      Start motor 2 tightening
//   M2 LOOSEN       Start motor 2 loosening
//   STOP            Stop all motors, return to IDLE (also resets STALLED)
//   STATUS          Print system state
//   LIMIT <mA>      Set all current limits
//   LIMIT T <mA>    Set tighten limit for both motors
//   LIMIT L <mA>    Set loosen limit for both motors
//   LIMIT M1 T <mA> Set motor 1 tighten limit
//   LIMIT M1 L <mA> Set motor 1 loosen limit
//   LIMIT M2 T <mA> Set motor 2 tighten limit
//   LIMIT M2 L <mA> Set motor 2 loosen limit
//   RATIO <n>       Set gearbox ratio for RPM display (default: 56)
//   LOG             Toggle periodic logging (every 200 ms)
//
// Wiring:
//   Motor 1: PWM=GPIO6, DIR=GPIO5, CAP=GPIO7  (gripper)
//   Motor 2: PWM=GPIO10, DIR=GPIO9, CAP=GPIO11 (tightening)
//   INA240 OUT → GPIO13 (shared current sense on 24V rail)
//   Buttons: GPIO1-4 (active LOW with internal pull-up)
//   4.7kΩ pull-down on both PWM pins (GPIO6, GPIO10) for boot safety
//   22kΩ/47kΩ voltage divider on both CAP pins (5V→3.3V)
//   OLED SSD1306: SDA=GPIO15, SCL=GPIO16, 0x3C

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Pin definitions ─────────────────────────────────────────────────
// Motor 1 (gripper)
const int PIN_M1_PWM = 6;
const int PIN_M1_DIR = 5;
const int PIN_M1_CAP = 7;

// Motor 2 (tightening)
const int PIN_M2_PWM = 10;
const int PIN_M2_DIR = 9;
const int PIN_M2_CAP = 11;

// Current sensor
const int PIN_INA_OUT = 13;

// Buttons
const int PIN_BTN1 = 1;
const int PIN_BTN2 = 2;
const int PIN_BTN3 = 3;
const int PIN_BTN4 = 4;

// OLED I2C pins
const int PIN_SDA = 15;
const int PIN_SCL = 16;

// RS-485 (unused in this test — set to high-Z)
const int PIN_DE_RE = 18;

// ── OLED display ────────────────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
const unsigned long DISPLAY_INTERVAL_MS = 40; // 25 Hz refresh
unsigned long lastDisplayTime = 0;

// ── Forward declarations ────────────────────────────────────────────
void printStatus();
void printLogLine();
void updateDisplay();

// ── PWM settings ────────────────────────────────────────────────────
const int PWM_CH_M1 = 0;
const int PWM_CH_M2 = 1;
const int PWM_FREQ = 20000;   // 20 kHz
const int PWM_RESOLUTION = 8; // 0–255

// ── INA240 settings ─────────────────────────────────────────────────
const float INA_GAIN = 20.0;
const float SHUNT_OHM = 0.1;    // 100 mΩ
const int ADC_SAMPLES = 16;     // fewer samples for faster control loop
const float ADC_MAX = 4095.0;
const float ADC_VREF_MV = 3100.0;
float currentOffsetMa = 0.0;

// ── Motor feedback ──────────────────────────────────────────────────
const int PULSES_PER_REV = 6;
float gearRatio = 56.0;

// Motor 1 pulse counter
volatile unsigned long m1PulseCount = 0;
void IRAM_ATTR onM1Pulse() { m1PulseCount++; }

// Motor 2 pulse counter
volatile unsigned long m2PulseCount = 0;
void IRAM_ATTR onM2Pulse() { m2PulseCount++; }

// ── Stall detection tuning ──────────────────────────────────────────
const float STALL_RPM_THRESHOLD = 500.0;    // motor-side RPM below this = stall
const unsigned long STALL_CONFIRM_MS = 200;  // must stay below threshold this long
const unsigned long PULSE_TIMEOUT_MS = 200;  // no pulse at all for this long = stall
const unsigned long STARTUP_GRACE_MS = 700;  // ignore stall detection during ramp-up

// ── Current control tuning ──────────────────────────────────────────
// Per-motor, per-action current limits [motor][0=tighten, 1=loosen]
float motorLimitMa[2][2] = {
    {330.0, 900.0},   // M1: tighten, loosen
    {330.0, 900.0},   // M2: tighten, loosen
};
float currentLimitMa = 300.0;         // active limit (set on motor start)
const int PWM_MIN = 102;             // ~40% of 255 — dead band edge
const int PWM_MAX = 255;             // 100%
const int PWM_START = 255;           // start at full PWM; PI pulls back near limit
const float KP_CURRENT = 1.0;         // proportional gain: PWM counts per mA error
const float KI_CURRENT = 0.1;        // integral gain
const float INTEGRAL_MAX = 100.0;    // anti-windup clamp
const unsigned long CONTROL_INTERVAL_MS = 10; // 100 Hz control loop
const unsigned long RAMP_DURATION_MS = 300;   // soft-start ramp from PWM_MIN to PWM_MAX
const int SLEW_TIGHTEN = 15;          // smooth tightening — no vibration
const int SLEW_LOOSEN = 500;          // aggressive loosening — impact-driver effect

// ── Button debounce ─────────────────────────────────────────────────
const int NUM_BUTTONS = 4;
const int btnPins[NUM_BUTTONS] = {PIN_BTN1, PIN_BTN2, PIN_BTN3, PIN_BTN4};
const unsigned long DEBOUNCE_MS = 50;
unsigned long lastDebounce[NUM_BUTTONS] = {};
bool btnState[NUM_BUTTONS] = {};     // true = pressed
bool lastReading[NUM_BUTTONS] = {};
bool prevBtnState[NUM_BUTTONS] = {}; // for edge detection

// ── Motor state machine ─────────────────────────────────────────────
enum MotorState
{
    STATE_IDLE,
    STATE_RUNNING,
    STATE_STALLED
};

enum MotorAction
{
    ACTION_NONE,
    ACTION_TIGHTEN,
    ACTION_LOOSEN
};

struct Motor
{
    int pinPWM;
    int pinDIR;
    int pinCAP;
    int pwmChannel;
    volatile unsigned long *pulseCountPtr;

    MotorState state;
    MotorAction action;
    int pwmValue;            // current LEDC duty (0–255)
    float motorRpm;
    float integralError;

    // RPM measurement
    unsigned long lastRpmTime;
    unsigned long lastPulseSnap;

    // Stall detection
    unsigned long stallStartTime; // when RPM first dropped below threshold
    unsigned long lastPulseTime;  // last time a pulse was seen
    unsigned long runStartTime;   // when motor started running

    // Control loop
    unsigned long lastControlTime;
};

Motor motors[2];

// Active motor index: -1 = none, 0 = motor 1, 1 = motor 2
int activeMotor = -1;

// ── Logging ─────────────────────────────────────────────────────────
bool logEnabled = false;
unsigned long lastLogTime = 0;

// ═══════════════════════════════════════════════════════════════════
// Current reading
// ═══════════════════════════════════════════════════════════════════
float readCurrentMa()
{
    long sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++)
    {
        sum += analogRead(PIN_INA_OUT);
    }
    float rawAdc = (float)sum / ADC_SAMPLES;
    float voltageMv = (rawAdc / ADC_MAX) * ADC_VREF_MV;
    return voltageMv / (INA_GAIN * SHUNT_OHM) - currentOffsetMa;
}

// ═══════════════════════════════════════════════════════════════════
// RPM measurement
// ═══════════════════════════════════════════════════════════════════
void updateMotorRpm(Motor &m)
{
    unsigned long now = millis();
    unsigned long elapsed = now - m.lastRpmTime;
    if (elapsed < 50) // 20 Hz RPM update
        return;

    noInterrupts();
    unsigned long count = *m.pulseCountPtr;
    interrupts();

    unsigned long delta = count - m.lastPulseSnap;
    m.lastPulseSnap = count;
    m.lastRpmTime = now;

    if (delta > 0)
        m.lastPulseTime = now;

    float pulsesPerSec = (float)delta * 1000.0 / (float)elapsed;
    m.motorRpm = (pulsesPerSec / PULSES_PER_REV) * 60.0;
}

// ═══════════════════════════════════════════════════════════════════
// Motor hardware control
// ═══════════════════════════════════════════════════════════════════
void setMotorPWM(Motor &m, int value)
{
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    m.pwmValue = value;
    ledcWrite(m.pwmChannel, value);
}

void setMotorDir(Motor &m, int dir)
{
    digitalWrite(m.pinDIR, dir ? HIGH : LOW);
}

void stopMotorHW(Motor &m)
{
    setMotorPWM(m, 0);
    m.integralError = 0.0;
}

// ═══════════════════════════════════════════════════════════════════
// State transitions
// ═══════════════════════════════════════════════════════════════════
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

void startMotor(int motorIdx, MotorAction action)
{
    Motor &m = motors[motorIdx];

    // If this motor is stalled, reject the command
    if (m.state == STATE_STALLED)
    {
        Serial.printf("[M%d] REJECTED — motor is STALLED, send STOP to reset\n",
                      motorIdx + 1);
        return;
    }

    // If another motor is active, stop it first
    if (activeMotor >= 0 && activeMotor != motorIdx)
    {
        Motor &prev = motors[activeMotor];
        stopMotorHW(prev);
        prev.state = STATE_IDLE;
        prev.action = ACTION_NONE;
        Serial.printf("[M%d] Stopped (preempted by M%d)\n",
                      activeMotor + 1, motorIdx + 1);
    }

    // If this motor is already running with the same action, ignore
    if (m.state == STATE_RUNNING && m.action == action)
        return;

    // Set direction: TIGHTEN = DIR 1, LOOSEN = DIR 0 (inverted from schematic)
    setMotorDir(m, (action == ACTION_TIGHTEN) ? 1 : 0);

    // Set active current limit based on motor and action
    int actionIdx = (action == ACTION_TIGHTEN) ? 0 : 1;
    currentLimitMa = motorLimitMa[motorIdx][actionIdx];

    // Reset control state
    m.integralError = 0.0;
    m.lastControlTime = millis();
    m.runStartTime = millis();
    m.stallStartTime = 0;

    // Reset RPM tracking to avoid stale data triggering false stall
    noInterrupts();
    m.lastPulseSnap = *m.pulseCountPtr;
    interrupts();
    m.lastRpmTime = millis();
    m.lastPulseTime = millis();
    m.motorRpm = 0.0;

    // Start PWM
    setMotorPWM(m, PWM_START);

    m.state = STATE_RUNNING;
    m.action = action;
    activeMotor = motorIdx;

    Serial.printf("[M%d] %s — starting at PWM %d%%, limit %d mA\n",
                  motorIdx + 1, actionStr(action),
                  (int)(PWM_START * 100 / 255), (int)currentLimitMa);
}

void stopAllMotors()
{
    for (int i = 0; i < 2; i++)
    {
        stopMotorHW(motors[i]);
        if (motors[i].state != STATE_IDLE)
        {
            Serial.printf("[M%d] %s → IDLE (stopped)\n",
                          i + 1, stateStr(motors[i].state));
        }
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
// Current-limiting control loop (called at CONTROL_INTERVAL_MS)
// ═══════════════════════════════════════════════════════════════════
void controlLoop(int motorIdx)
{
    Motor &m = motors[motorIdx];
    if (m.state != STATE_RUNNING)
        return;

    unsigned long now = millis();
    if (now - m.lastControlTime < CONTROL_INTERVAL_MS)
        return;
    m.lastControlTime = now;

    // Read current
    float currentMa = readCurrentMa();

    // Update RPM
    updateMotorRpm(m);

    // ── Stall detection (skip during startup grace period) ──────────
    if (now - m.runStartTime > STARTUP_GRACE_MS)
    {
        // Check pulse timeout — no pulses at all
        bool pulseTimeout = (now - m.lastPulseTime > PULSE_TIMEOUT_MS);

        // Check low RPM
        bool lowRpm = (m.motorRpm < STALL_RPM_THRESHOLD) && (m.pwmValue >= PWM_MIN);

        if (pulseTimeout || lowRpm)
        {
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
            m.stallStartTime = 0; // reset if RPM recovered
        }
    }

    // ── Current-limiting PI controller ──────────────────────────────
    // Error is positive when current is below limit (room to increase PWM)
    // Error is negative when current exceeds limit (must decrease PWM)
    float error = currentLimitMa - currentMa;

    // Conditional integration (anti-windup):
    // - Do NOT integrate positive error if PWM is already at max
    //   (no-load case: motor can't draw more current, don't wind up)
    // - Always integrate negative error (over-limit: must reduce fast)
    if (error < 0 || m.pwmValue < PWM_MAX)
    {
        m.integralError += error * (CONTROL_INTERVAL_MS / 1000.0);
    }
    // Clamp integral: only clamp the positive side; negative is unclamped
    // so the controller can reduce PWM aggressively when over-limit
    if (m.integralError > INTEGRAL_MAX) m.integralError = INTEGRAL_MAX;

    float adjustment = KP_CURRENT * error + KI_CURRENT * m.integralError;

    int newPWM = PWM_START + (int)adjustment;

    // Never go below dead-band edge — motor would stop and fake a stall
    if (newPWM < PWM_MIN) newPWM = PWM_MIN;
    if (newPWM > PWM_MAX) newPWM = PWM_MAX;

    // Soft-start ramp ceiling — linearly increase max allowed PWM
    unsigned long elapsed = now - m.runStartTime;
    if (elapsed < RAMP_DURATION_MS) {
        int rampCeiling = PWM_MIN + (int)((long)(PWM_MAX - PWM_MIN) * elapsed / RAMP_DURATION_MS);
        if (newPWM > rampCeiling) newPWM = rampCeiling;
    }

    // Slew rate limit — cap how fast PWM can change per cycle
    int slewMax = (m.action == ACTION_TIGHTEN) ? SLEW_TIGHTEN : SLEW_LOOSEN;
    int delta = newPWM - m.pwmValue;
    if (delta > slewMax)  newPWM = m.pwmValue + slewMax;
    if (delta < -slewMax) newPWM = m.pwmValue - slewMax;
    if (newPWM < PWM_MIN) newPWM = PWM_MIN;

    setMotorPWM(m, newPWM);
}

// ═══════════════════════════════════════════════════════════════════
// Button handling
// ═══════════════════════════════════════════════════════════════════
void updateButtons()
{
    unsigned long now = millis();
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        bool reading = (digitalRead(btnPins[i]) == LOW);
        if (reading != lastReading[i])
            lastDebounce[i] = now;

        if ((now - lastDebounce[i]) > DEBOUNCE_MS)
        {
            if (reading != btnState[i])
            {
                btnState[i] = reading;
                // Edge detection handled below
            }
        }
        lastReading[i] = reading;
    }
}

void processButtonEvents()
{
    // Button mapping:
    //   B1 (idx 0) → M1 TIGHTEN
    //   B2 (idx 1) → M1 LOOSEN
    //   B3 (idx 2) → M2 TIGHTEN
    //   B4 (idx 3) → M2 LOOSEN
    const int btnMotor[4]      = {0, 0, 1, 1};
    const MotorAction btnAction[4] = {ACTION_TIGHTEN, ACTION_LOOSEN,
                                      ACTION_TIGHTEN, ACTION_LOOSEN};

    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        bool wasPressed = prevBtnState[i];
        bool isPressed  = btnState[i];

        if (isPressed && !wasPressed)
        {
            // Button just pressed → start motor
            Serial.printf("[BTN] Button %d pressed → M%d %s\n",
                          i + 1, btnMotor[i] + 1, actionStr(btnAction[i]));
            startMotor(btnMotor[i], btnAction[i]);
        }
        else if (!isPressed && wasPressed)
        {
            // Button released
            int mIdx = btnMotor[i];
            Motor &m = motors[mIdx];

            if (m.state == STATE_RUNNING && activeMotor == mIdx)
            {
                // Motor was running from this button — stop it
                stopMotorHW(m);
                m.state = STATE_IDLE;
                m.action = ACTION_NONE;
                activeMotor = -1;
                Serial.printf("[BTN] Button %d released → M%d stopped → IDLE\n",
                              i + 1, mIdx + 1);
            }
            else if (m.state == STATE_STALLED)
            {
                // Release after stall → return to IDLE
                m.state = STATE_IDLE;
                m.action = ACTION_NONE;
                Serial.printf("[BTN] Button %d released → M%d STALLED → IDLE\n",
                              i + 1, mIdx + 1);
            }
        }

        prevBtnState[i] = btnState[i];
    }
}

// ═══════════════════════════════════════════════════════════════════
// Serial command parsing
// ═══════════════════════════════════════════════════════════════════
void processCommand(const String &raw)
{
    String cmd = raw;
    cmd.trim();
    String upper = cmd;
    upper.toUpperCase();

    if (upper == "M1 TIGHTEN")
    {
        startMotor(0, ACTION_TIGHTEN);
    }
    else if (upper == "M1 LOOSEN")
    {
        startMotor(0, ACTION_LOOSEN);
    }
    else if (upper == "M2 TIGHTEN")
    {
        startMotor(1, ACTION_TIGHTEN);
    }
    else if (upper == "M2 LOOSEN")
    {
        startMotor(1, ACTION_LOOSEN);
    }
    else if (upper == "STOP")
    {
        stopAllMotors();
    }
    else if (upper.startsWith("LIMIT "))
    {
        String arg = upper.substring(6);
        arg.trim();

        // Helper: parse motor-specific limit "M1 T 300", "M2 L 500", etc.
        int mIdx = -1; // -1 = both motors
        if (arg.startsWith("M1 ") || arg.startsWith("M2 ")) {
            mIdx = arg.charAt(1) - '1';
            arg = arg.substring(3);
            arg.trim();
        }

        int aIdx = -1; // -1 = both actions
        if (arg.startsWith("T ")) {
            aIdx = 0;
            arg = arg.substring(2);
        } else if (arg.startsWith("L ")) {
            aIdx = 1;
            arg = arg.substring(2);
        }
        arg.trim();

        float v = arg.toFloat();
        if (v >= 50 && v <= 1500) {
            int mStart = (mIdx >= 0) ? mIdx : 0;
            int mEnd   = (mIdx >= 0) ? mIdx : 1;
            int aStart = (aIdx >= 0) ? aIdx : 0;
            int aEnd   = (aIdx >= 0) ? aIdx : 1;
            for (int mi = mStart; mi <= mEnd; mi++)
                for (int ai = aStart; ai <= aEnd; ai++)
                    motorLimitMa[mi][ai] = v;

            // Update active limit if currently running
            if (activeMotor >= 0 && motors[activeMotor].state == STATE_RUNNING) {
                int ai = (motors[activeMotor].action == ACTION_TIGHTEN) ? 0 : 1;
                currentLimitMa = motorLimitMa[activeMotor][ai];
            }

            // Print confirmation
            const char *mStr = (mIdx >= 0) ? (mIdx == 0 ? "M1" : "M2") : "All";
            const char *aStr = (aIdx == 0) ? " tighten" : (aIdx == 1) ? " loosen" : "";
            Serial.printf("[CFG] %s%s limit set to %.0f mA\n", mStr, aStr, v);
        } else {
            Serial.println("[ERR] LIMIT must be 50-1500 mA");
        }
    }
    else if (upper.startsWith("RATIO "))
    {
        float v = cmd.substring(6).toFloat();
        if (v > 0)
        {
            gearRatio = v;
            Serial.printf("[CFG] Gear ratio set to %.1f\n", gearRatio);
        }
    }
    else if (upper == "LOG")
    {
        logEnabled = !logEnabled;
        Serial.printf("Periodic logging: %s\n", logEnabled ? "ON (200ms)" : "OFF");
    }
    else if (upper == "STATUS")
    {
        printStatus();
    }
    else
    {
        Serial.println("Commands: M1 TIGHTEN, M1 LOOSEN, M2 TIGHTEN, M2 LOOSEN");
        Serial.println("  STOP, STATUS, LIMIT [M1|M2] [T|L] <mA>, RATIO <n>, LOG");
    }
}

// ═══════════════════════════════════════════════════════════════════
// Status / logging
// ═══════════════════════════════════════════════════════════════════
void printStatus()
{
    float currentMa = readCurrentMa();
    Serial.println("════════════════════════════════════════");
    Serial.printf("  M1: T=%.0f mA  L=%.0f mA\n", motorLimitMa[0][0], motorLimitMa[0][1]);
    Serial.printf("  M2: T=%.0f mA  L=%.0f mA\n", motorLimitMa[1][0], motorLimitMa[1][1]);
    Serial.printf("  Active limit:  %.0f mA\n", currentLimitMa);
    Serial.printf("  Measured current: %.1f mA\n", currentMa);
    Serial.printf("  Gear ratio: %.1f\n", gearRatio);
    Serial.printf("  Active motor: %s\n",
                  activeMotor >= 0 ? String("M" + String(activeMotor + 1)).c_str() : "none");
    for (int i = 0; i < 2; i++)
    {
        Motor &m = motors[i];
        updateMotorRpm(m);
        float shaftRpm = m.motorRpm / gearRatio;
        Serial.printf("  M%d: %-8s %-8s  PWM=%3d (%2d%%)  RPM=%.0f (shaft %.1f)\n",
                      i + 1, stateStr(m.state), actionStr(m.action),
                      m.pwmValue, (int)(m.pwmValue * 100 / 255),
                      m.motorRpm, shaftRpm);
    }
    Serial.printf("  Buttons: %d %d %d %d\n",
                  btnState[0], btnState[1], btnState[2], btnState[3]);
    Serial.println("════════════════════════════════════════");
}

void printLogLine()
{
    float currentMa = readCurrentMa();
    for (int i = 0; i < 2; i++)
        updateMotorRpm(motors[i]);

    // CSV format: timestamp,active,m1_state,m1_action,m1_pwm,m1_rpm,m2_state,m2_action,m2_pwm,m2_rpm,current_mA
    Serial.printf("LOG,%lu,%d,%s,%s,%d,%.0f,%s,%s,%d,%.0f,%.1f\n",
                  millis(), activeMotor + 1,
                  stateStr(motors[0].state), actionStr(motors[0].action),
                  motors[0].pwmValue, motors[0].motorRpm,
                  stateStr(motors[1].state), actionStr(motors[1].action),
                  motors[1].pwmValue, motors[1].motorRpm,
                  currentMa);
}

// ═══════════════════════════════════════════════════════════════════
// OLED display update
// ═══════════════════════════════════════════════════════════════════
void drawBar(int y, float fraction)
{
    fraction = constrain(fraction, 0.0f, 1.0f);
    oled.drawRect(0, y, 128, 6, SSD1306_WHITE);
    int fillW = (int)(fraction * 126);
    if (fillW > 0)
        oled.fillRect(1, y + 1, fillW, 4, SSD1306_WHITE);
}

void updateDisplay()
{
    unsigned long now = millis();
    if (now - lastDisplayTime < DISPLAY_INTERVAL_MS)
        return;
    lastDisplayTime = now;

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    // ── Row 1: Status (size 2, y=0, 16px tall) ──
    oled.setTextSize(2);
    oled.setCursor(0, 0);

    // Find which motor to display (active, or stalled if any)
    int dm = activeMotor;
    if (dm < 0) {
        for (int i = 0; i < 2; i++) {
            if (motors[i].state == STATE_STALLED) { dm = i; break; }
        }
    }

    if (dm >= 0) {
        Motor &m = motors[dm];
        if (m.state == STATE_STALLED)
            oled.printf("M%d STALLED", dm + 1);
        else
            oled.printf("M%d %s", dm + 1,
                        m.action == ACTION_TIGHTEN ? "TIGHTEN" : "LOOSEN");
    } else {
        oled.print("IDLE");
    }

    oled.setTextSize(1);

    if (dm >= 0 && motors[dm].state == STATE_RUNNING) {
        Motor &m = motors[dm];
        float currentMa = readCurrentMa();

        // ── Row 2: Current bar (y=17, 6px) ──
        drawBar(17, currentMa / currentLimitMa);

        // ── Row 3: Current text (y=24) ──
        oled.setCursor(0, 24);
        oled.printf("%.0f / %.0f mA", currentMa, currentLimitMa);

        // ── Row 4: PWM bar (y=33, 6px) ──
        drawBar(33, (float)m.pwmValue / 255.0f);

        // ── Row 5: PWM text (y=40) ──
        oled.setCursor(0, 40);
        oled.printf("PWM: %d%%", m.pwmValue * 100 / 255);

        // ── Row 6: RPM + elapsed time (y=49) ──
        float shaftRpm = m.motorRpm / gearRatio;
        float elapsed = (now - m.runStartTime) / 1000.0f;
        oled.setCursor(0, 49);
        oled.printf("RPM:%.1f  t:%.1fs", shaftRpm, elapsed);

    } else if (dm >= 0 && motors[dm].state == STATE_STALLED) {
        oled.setCursor(0, 24);
        oled.print("Send STOP to reset");
        oled.setCursor(0, 38);
        oled.printf("M1: T=%.0f L=%.0f", motorLimitMa[0][0], motorLimitMa[0][1]);
        oled.setCursor(0, 48);
        oled.printf("M2: T=%.0f L=%.0f", motorLimitMa[1][0], motorLimitMa[1][1]);

    } else {
        // IDLE — show config info
        oled.setCursor(0, 24);
        oled.printf("M1: T=%.0f L=%.0f", motorLimitMa[0][0], motorLimitMa[0][1]);
        oled.setCursor(0, 34);
        oled.printf("M2: T=%.0f L=%.0f", motorLimitMa[1][0], motorLimitMa[1][1]);
        oled.setCursor(0, 48);
        oled.printf("Ratio: %.0f:1", gearRatio);
    }

    oled.display();
}

// ═══════════════════════════════════════════════════════════════════
// Setup
// ═══════════════════════════════════════════════════════════════════
void setup()
{
    Serial.begin(115200);

    // Unused pin → high-Z
    pinMode(PIN_DE_RE, INPUT);

    // OLED init
    Wire.begin(PIN_SDA, PIN_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[OLED] Init FAILED");
    } else {
        oled.clearDisplay();
        oled.setTextSize(2);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(0, 0);
        oled.print("Motor");
        oled.setCursor(0, 20);
        oled.print("Control");
        oled.setTextSize(1);
        oled.setCursor(0, 48);
        oled.print("Calibrating current");
        oled.setCursor(0, 56);
        oled.print("sensor...");
        oled.display();
    }

    // Buttons with pull-up
    for (int i = 0; i < NUM_BUTTONS; i++)
        pinMode(btnPins[i], INPUT_PULLUP);

    // Motor 1 setup
    motors[0] = {};
    motors[0].pinPWM = PIN_M1_PWM;
    motors[0].pinDIR = PIN_M1_DIR;
    motors[0].pinCAP = PIN_M1_CAP;
    motors[0].pwmChannel = PWM_CH_M1;
    motors[0].pulseCountPtr = &m1PulseCount;

    // Motor 2 setup
    motors[1] = {};
    motors[1].pinPWM = PIN_M2_PWM;
    motors[1].pinDIR = PIN_M2_DIR;
    motors[1].pinCAP = PIN_M2_CAP;
    motors[1].pwmChannel = PWM_CH_M2;
    motors[1].pulseCountPtr = &m2PulseCount;

    // Initialize both motors
    for (int i = 0; i < 2; i++)
    {
        Motor &m = motors[i];
        m.state = STATE_IDLE;
        m.action = ACTION_NONE;

        pinMode(m.pinDIR, OUTPUT);
        digitalWrite(m.pinDIR, LOW);

        ledcSetup(m.pwmChannel, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(m.pinPWM, m.pwmChannel);
        ledcWrite(m.pwmChannel, 0);

        pinMode(m.pinCAP, INPUT);
    }

    // Attach interrupts for CAP feedback
    attachInterrupt(digitalPinToInterrupt(PIN_M1_CAP), onM1Pulse, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_M2_CAP), onM2Pulse, RISING);

    // INA240 analog input
    pinMode(PIN_INA_OUT, INPUT);
    analogSetAttenuation(ADC_11db);

    delay(1000);
    Serial.println();
    Serial.println("=== test_motor_control ===");
    Serial.println("Current-limited motor control with stall detection");
    Serial.println("M1: PWM=GPIO6 DIR=GPIO5 CAP=GPIO7");
    Serial.println("M2: PWM=GPIO10 DIR=GPIO9 CAP=GPIO11");
    Serial.println("INA240: GPIO13 (shared 24V rail)");
    Serial.println();

    // Power-on current sensor calibration
    Serial.print("[CAL] Calibrating current sensor");
    const int CAL_READINGS = 20;
    float calSum = 0.0;
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
    Serial.println();

    // Initialize timing
    unsigned long now = millis();
    for (int i = 0; i < 2; i++)
    {
        motors[i].lastRpmTime = now;
        motors[i].lastPulseTime = now;
    }

    Serial.println("Commands: M1 TIGHTEN, M1 LOOSEN, M2 TIGHTEN, M2 LOOSEN");
    Serial.println("  STOP, STATUS, LIMIT [M1|M2] [T|L] <mA>, RATIO <n>, LOG");
    Serial.printf("M1: T=%.0f L=%.0f | M2: T=%.0f L=%.0f | Ratio: %.1f\n\n",
                  motorLimitMa[0][0], motorLimitMa[0][1],
                  motorLimitMa[1][0], motorLimitMa[1][1], gearRatio);
}

// ═══════════════════════════════════════════════════════════════════
// Main loop
// ═══════════════════════════════════════════════════════════════════
void loop()
{
    // Update button state (debounced)
    updateButtons();
    processButtonEvents();

    // Run control loop for active motor
    if (activeMotor >= 0)
        controlLoop(activeMotor);

    // Update OLED display
    updateDisplay();

    // Process serial commands
    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }

    // Periodic logging
    if (logEnabled && (millis() - lastLogTime >= 200))
    {
        lastLogTime = millis();
        printLogLine();
    }
}
