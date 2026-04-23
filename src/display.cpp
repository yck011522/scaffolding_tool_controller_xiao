// display.cpp — see display.h
//
// Layout (128x64):
//   Row 1 (y= 0..15, size=2): big status header — "M1 TIGHTEN", "IDLE", etc.
//   Row 2 (y=17..22):          current bar (current/limit)
//   Row 3 (y=24..31):          "123 / 330 mA"
//   Row 4 (y=33..38):          PWM bar (pwmValue/255)
//   Row 5 (y=40..47):          "PWM: 47%"
//   Row 6 (y=49..56):          "RPM:nn.n  t:nn.ns"
// IDLE / STALLED screens show config summary instead.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "display.h"
#include "motor_control.h"

static const int PIN_SDA = 15;
static const int PIN_SCL = 16;

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C

static Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// Refresh throttled to keep I²C traffic from stealing CPU from the
// 100 Hz control loop.
static const unsigned long DISPLAY_INTERVAL_MS = 40;   // 25 Hz
static unsigned long lastDisplayTime = 0;

// Draw an outlined horizontal bar with `fraction` (0..1) filled in.
static void drawBar(int y, float fraction)
{
    fraction = constrain(fraction, 0.0f, 1.0f);
    oled.drawRect(0, y, 128, 6, SSD1306_WHITE);
    int fillW = (int)(fraction * 126);
    if (fillW > 0)
        oled.fillRect(1, y + 1, fillW, 4, SSD1306_WHITE);
}

void setupDisplay()
{
    Wire.begin(PIN_SDA, PIN_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        Serial.println("[OLED] Init FAILED");
        return;
    }
    // Boot splash — quickly overwritten once the main loop starts.
    oled.clearDisplay();
    oled.setTextSize(2);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print("Motor");
    oled.setCursor(0, 20);
    oled.print("Control");
    oled.display();
}

// Shown while calibrateCurrentSensor() is running (motors must be off).
void displayCalibrating()
{
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

void updateDisplay()
{
    unsigned long now = millis();
    if (now - lastDisplayTime < DISPLAY_INTERVAL_MS) return;
    lastDisplayTime = now;

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    // Choose what to show:
    //   1. The active (running) motor, if any.
    //   2. Otherwise any STALLED motor (so the user sees it).
    //   3. Otherwise IDLE config summary.
    int dm = activeMotor;
    if (dm < 0)
        for (int i = 0; i < NUM_MOTORS; i++)
            if (motors[i].state == STATE_STALLED) { dm = i; break; }

    // Row 1: large header (motor + action, or IDLE).
    oled.setTextSize(2);
    oled.setCursor(0, 0);
    if (dm >= 0)
    {
        Motor &m = motors[dm];
        if (m.state == STATE_STALLED)
            oled.printf("M%d STALLED", dm + 1);
        else
            oled.printf("M%d %s", dm + 1,
                        m.action == ACTION_TIGHTEN ? "TIGHTEN" : "LOOSEN");
    }
    else
    {
        oled.print("IDLE");
    }
    oled.setTextSize(1);

    if (dm >= 0 && motors[dm].state == STATE_RUNNING)
    {
        // Live telemetry while a motor runs.
        Motor &m = motors[dm];
        const MotorConfig &c = cfg[dm];
        float currentMa = readCurrentMa();

        drawBar(17, (m.activeLimitMa > 0) ? (currentMa / m.activeLimitMa) : 0.0f);
        oled.setCursor(0, 24);
        oled.printf("%.0f / %.0f mA", currentMa, m.activeLimitMa);

        drawBar(33, (float)m.pwmValue / 255.0f);
        oled.setCursor(0, 40);
        oled.printf("PWM: %d%%", m.pwmValue * 100 / 255);

        // Convert motor RPM to shaft RPM via the gear ratio for display.
        float shaftRpm = (c.gearRatio > 0) ? (m.motorRpm / c.gearRatio) : 0.0f;
        float elapsed = (now - m.runStartTime) / 1000.0f;
        oled.setCursor(0, 49);
        oled.printf("RPM:%.1f  t:%.1fs", shaftRpm, elapsed);
    }
    else if (dm >= 0 && motors[dm].state == STATE_STALLED)
    {
        // Stalled — show how to recover and a quick reminder of limits.
        oled.setCursor(0, 24);
        oled.print("Send STOP to reset");
        oled.setCursor(0, 38);
        oled.printf("M1: T=%.0f L=%.0f", cfg[0].limitMa[0], cfg[0].limitMa[1]);
        oled.setCursor(0, 48);
        oled.printf("M2: T=%.0f L=%.0f", cfg[1].limitMa[0], cfg[1].limitMa[1]);
    }
    else
    {
        // Idle screen — quick summary of the active configuration.
        oled.setCursor(0, 24);
        oled.printf("M1: T=%.0f L=%.0f", cfg[0].limitMa[0], cfg[0].limitMa[1]);
        oled.setCursor(0, 34);
        oled.printf("M2: T=%.0f L=%.0f", cfg[1].limitMa[0], cfg[1].limitMa[1]);
        oled.setCursor(0, 48);
        oled.printf("R M1:%.0f M2:%.0f", cfg[0].gearRatio, cfg[1].gearRatio);
    }

    oled.display();
}
