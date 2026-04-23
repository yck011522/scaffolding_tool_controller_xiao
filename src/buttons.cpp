// buttons.cpp — see buttons.h
//
// Two-stage button processing each main-loop pass:
//   updateButtons()       reads pins and applies a 50 ms debounce, writing
//                         the stable level to btnState[].
//   processButtonEvents() compares btnState[] against prevBtnState[] and
//                         acts only on edges (press → startMotor, release
//                         → stop / clear stalled state).

#include <Arduino.h>
#include "buttons.h"
#include "motor_control.h"

// Active-LOW with internal pull-up. GPIO1–4 are exposed on the board.
static const int BTN_PINS[NUM_BUTTONS] = { 1, 2, 3, 4 };
static const unsigned long DEBOUNCE_MS = 50;

static unsigned long lastDebounce[NUM_BUTTONS] = {};   // last time the raw level changed
static bool btnState[NUM_BUTTONS]      = {};   // debounced level (true = pressed)
static bool lastReading[NUM_BUTTONS]   = {};   // last raw read, for change detect
static bool prevBtnState[NUM_BUTTONS]  = {};   // debounced state from previous pass

// Button → (motor index, action) mapping.
//   B1 → M1 TIGHTEN    B2 → M1 LOOSEN
//   B3 → M2 TIGHTEN    B4 → M2 LOOSEN
static const int         BTN_MOTOR[NUM_BUTTONS]  = { 0, 0, 1, 1 };
static const MotorAction BTN_ACTION[NUM_BUTTONS] = {
    ACTION_TIGHTEN, ACTION_LOOSEN, ACTION_TIGHTEN, ACTION_LOOSEN
};

void setupButtons()
{
    for (int i = 0; i < NUM_BUTTONS; i++)
        pinMode(BTN_PINS[i], INPUT_PULLUP);
}

// Debounce: if the raw level matches the stored debounced level for at
// least DEBOUNCE_MS, the new level is accepted.
void updateButtons()
{
    unsigned long now = millis();
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        bool reading = (digitalRead(BTN_PINS[i]) == LOW);
        if (reading != lastReading[i])
            lastDebounce[i] = now;   // raw changed; restart the debounce timer

        if ((now - lastDebounce[i]) > DEBOUNCE_MS)
        {
            if (reading != btnState[i])
                btnState[i] = reading;   // commit the new debounced level
        }
        lastReading[i] = reading;
    }
}

// Edge-triggered actions. Press starts the motor; release stops it (or
// clears STALLED if the motor stalled while the button was held).
void processButtonEvents()
{
    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        bool wasPressed = prevBtnState[i];
        bool isPressed  = btnState[i];

        if (isPressed && !wasPressed)
        {
            // Rising edge — start the motor for this button.
            Serial.printf("[BTN] Button %d pressed → M%d %s\n",
                          i + 1, BTN_MOTOR[i] + 1, actionStr(BTN_ACTION[i]));
            startMotor(BTN_MOTOR[i], BTN_ACTION[i]);
        }
        else if (!isPressed && wasPressed)
        {
            // Falling edge — release. Two cases:
            //   1. Motor still RUNNING and we own it → stop it.
            //   2. Motor STALLED while held → use release as the
            //      acknowledgement, return to IDLE.
            int mIdx = BTN_MOTOR[i];
            Motor &m = motors[mIdx];

            if (m.state == STATE_RUNNING && activeMotor == mIdx)
            {
                stopMotorHW(m);
                m.state = STATE_IDLE;
                m.action = ACTION_NONE;
                activeMotor = -1;
                Serial.printf("[BTN] Button %d released → M%d stopped → IDLE\n",
                              i + 1, mIdx + 1);
            }
            else if (m.state == STATE_STALLED)
            {
                m.state = STATE_IDLE;
                m.action = ACTION_NONE;
                Serial.printf("[BTN] Button %d released → M%d STALLED → IDLE\n",
                              i + 1, mIdx + 1);
            }
        }

        prevBtnState[i] = btnState[i];
    }
}

bool buttonPressed(int idx)
{
    if (idx < 0 || idx >= NUM_BUTTONS) return false;
    return btnState[idx];
}
