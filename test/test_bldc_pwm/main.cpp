// test_bldc_pwm — BLDC motor PWM drive + direction test
//
// Wiring (breadboard, no Grove connectors):
//   Motor Red   → +24V supply
//   Motor Black → GND (shared with ESP32 GND)
//   Motor Blue  (PWM in) → D10   (+ 4.7kΩ pull-DOWN to GND for boot safety)
//   Motor White (DIR)    → D9
//   Motor Yellow (CAP)   → disconnected
//   ESP32 powered via USB-C
//
// IMPORTANT: The 24V supply GND and the ESP32 GND must be connected together.
//
// Serial commands (115200 baud, via USB-C):
//   PWM <0-100>    Set duty cycle percentage (0 = stopped, 100 = full speed)
//   DIR <0|1>      Set direction pin (0 = LOW, 1 = HIGH)
//   SWEEP           Ramp from 0% to 100% in 5% steps, 2 seconds each
//   STOP            Set duty to 0%
//   STATUS          Print current PWM and direction settings
//   FREQ <hz>       Change PWM frequency live (e.g., FREQ 20000)

#include <Arduino.h>

// ── Pin definitions ─────────────────────────────────────────────────
// XIAO ESP32-S3: D9 = GPIO8, D10 = GPIO9
const int PIN_PWM = D10;   // Blue wire — PWM speed command
const int PIN_DIR = D9;    // White wire — direction control

// ── PWM settings ────────────────────────────────────────────────────
const int PWM_CHANNEL    = 0;     // LEDC channel (0–15)
int       pwmFreq        = 5000;  // Hz — tunable at runtime via FREQ command
const int PWM_RESOLUTION = 8;     // 8-bit → duty values 0–255

// ── State ───────────────────────────────────────────────────────────
int currentDutyPercent = 0;
int currentDir = 0;

// ── Helpers ─────────────────────────────────────────────────────────
void setDuty(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    currentDutyPercent = percent;

    int dutyValue = map(percent, 0, 100, 0, 255);
    ledcWrite(PWM_CHANNEL, dutyValue);

    Serial.print("PWM set to ");
    Serial.print(percent);
    Serial.print("% (duty=");
    Serial.print(dutyValue);
    Serial.println("/255)");
}

void setDir(int dir) {
    currentDir = dir ? 1 : 0;
    digitalWrite(PIN_DIR, currentDir);

    Serial.print("DIR set to ");
    Serial.println(currentDir);
}

void printStatus() {
    Serial.println("────────────────────────────");
    Serial.print("PWM: ");
    Serial.print(currentDutyPercent);
    Serial.println("%");
    Serial.print("DIR: ");
    Serial.println(currentDir);
    Serial.print("Freq: ");
    Serial.print(pwmFreq);
    Serial.println(" Hz");
    Serial.println("Dead band: ~37-40%");
    Serial.println("────────────────────────────");
}

void runSweep() {
    Serial.println("Starting PWM sweep 0% → 100% (5% steps, 2s each)...");
    for (int pct = 0; pct <= 100; pct += 5) {
        setDuty(pct);
        delay(2000);
    }
    Serial.println("Sweep complete. Motor at 100%. Use STOP to halt.");
}

// ── Command parsing ─────────────────────────────────────────────────
void processCommand(String cmd) {
    cmd.trim();
    cmd.toUpperCase();

    if (cmd.startsWith("PWM ")) {
        int val = cmd.substring(4).toInt();
        setDuty(val);
    }
    else if (cmd.startsWith("DIR ")) {
        int val = cmd.substring(4).toInt();
        setDir(val);
    }
    else if (cmd == "SWEEP") {
        runSweep();
    }
    else if (cmd == "STOP") {
        setDuty(0);
    }
    else if (cmd == "STATUS") {
        printStatus();
    }
    else if (cmd.startsWith("FREQ ")) {
        pwmFreq = cmd.substring(5).toInt();
        ledcSetup(PWM_CHANNEL, pwmFreq, PWM_RESOLUTION);
        ledcAttachPin(PIN_PWM, PWM_CHANNEL);
        // Restore current duty after frequency change
        int dutyValue = map(currentDutyPercent, 0, 100, 0, 255);
        ledcWrite(PWM_CHANNEL, dutyValue);
        Serial.print("PWM frequency changed to ");
        Serial.print(pwmFreq);
        Serial.println(" Hz");
    }
    else {
        Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, FREQ <hz>");
    }
}

// ── Setup & Loop ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Direction pin — start LOW
    pinMode(PIN_DIR, OUTPUT);
    digitalWrite(PIN_DIR, LOW);

    // PWM pin — configure LEDC channel, attach to pin, start at 0% duty
    ledcSetup(PWM_CHANNEL, pwmFreq, PWM_RESOLUTION);
    ledcAttachPin(PIN_PWM, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);

    delay(1000);
    Serial.println();
    Serial.println("=== test_bldc_pwm ===");
    Serial.println("PWM on D10, DIR on D9");
    Serial.print("PWM frequency: ");
    Serial.print(pwmFreq);
    Serial.println(" Hz");
    Serial.println();
    Serial.println("Commands: PWM <0-100>, DIR <0|1>, SWEEP, STOP, STATUS, FREQ <hz>");
    Serial.println();
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }
}
