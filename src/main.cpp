// main.cpp — Scaffolding Tool Controller (integrated firmware)
//
// Wires the per-motor control subsystem (cfg[]/motors[] + PI loop),
// debounced buttons, OLED status display, NVS-backed configuration
// store, and the dual USB / RS-485 protocol transports.
//
// Hardware: Waveshare ESP32-S3-Tiny.
//   M1: PWM=GPIO6  DIR=GPIO5  CAP=GPIO7   gear 1:56
//   M2: PWM=GPIO10 DIR=GPIO9  CAP=GPIO11  gear 1:90
//   INA240: GPIO13 (shared 24V rail current sense)
//   OLED:   I²C SDA=GPIO15 SCL=GPIO16
//   RS-485: TX=GPIO43 RX=GPIO44 DE/RE=GPIO18 (MAX3485)
//   Buttons: GPIO1..4 (active-LOW, internal pull-ups)
//
// All logic is in the focused modules:
//   motor_control.{h,cpp}  cfg[], motors[], control loop, state machine
//   buttons.{h,cpp}        debounced input
//   display.{h,cpp}        OLED rendering
//   config_store.{h,cpp}   NVS persistence for cfg[]
//   protocol.{h,cpp}       shared command parser
//   transport.{h,cpp}      USB + RS-485 line readers

#include <Arduino.h>

#include "motor_control.h"
#include "buttons.h"
#include "display.h"
#include "config_store.h"
#include "protocol.h"
#include "transport.h"

// LOG-mode 200 ms cadence (USB only).
static unsigned long lastLogTime = 0;

void setup()
{
    usbConsoleBegin();      // brings up Serial @ 115200

    // Display first so the boot splash hides the multi-second cal.
    setupDisplay();
    displayCalibrating();

    setupButtons();

    // Open NVS, then configure motor pins, then overlay any persisted
    // values onto the in-memory cfg[]. setupMotors() must run before
    // configStoreLoad() reads cfg[] because it zeroes the runtime Motor
    // structs (cfg[] is left untouched, but the order is intentional:
    // pins/LEDC/ISRs are set with the compiled defaults, then the loaded
    // values just become the new tuning parameters for those pins).
    configStoreBegin();
    setupMotors();
    configStoreLoad();

    // RS-485 last so its DE pin doesn't briefly drive while other GPIOs
    // are still being configured.
    rs485Begin();

    // Give USB-CDC a moment to enumerate so the banner isn't lost.
    delay(1000);
    Serial.println();
    Serial.println("=== Scaffolding Tool Controller ===");
    Serial.print  ("Firmware "); Serial.println(FIRMWARE_VERSION);
    Serial.println("USB and RS-485 share the same command grammar.");
    Serial.println("Send HELP for a quick reference.");
    Serial.println();

    // Quiescent INA240 reading → currentOffsetMa. Motors are guaranteed
    // off here since we haven't accepted any commands yet.
    calibrateCurrentSensor();
    Serial.println();
}

void loop()
{
    // 1. Debounced button input → motor start/stop edges.
    updateButtons();
    processButtonEvents();

    // 2. PI loop for whichever motor is currently active. The loop is
    //    internally throttled to CONTROL_INTERVAL_MS, so calling it
    //    every iteration is safe.
    if (activeMotor >= 0)
        controlLoop(activeMotor);

    // 3. OLED refresh (throttled internally to ~25 Hz).
    updateDisplay();

    // 4. Drain incoming serial on both transports.
    usbConsolePoll();
    rs485Poll();

    // 5. Optional USB streaming output (LOG command toggles this).
    if (logEnabled && (millis() - lastLogTime >= 200))
    {
        lastLogTime = millis();
        printPeriodicLog(Serial);
    }
}
