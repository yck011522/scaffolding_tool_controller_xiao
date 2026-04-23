// buttons.h — Debounced button input + edge-triggered motor control.
//
// Mapping (active-LOW with internal pull-up):
//   B1 (GPIO1) → M1 TIGHTEN     B2 (GPIO2) → M1 LOOSEN
//   B3 (GPIO3) → M2 TIGHTEN     B4 (GPIO4) → M2 LOOSEN
//
// Press to start, release to stop.

#pragma once

constexpr int NUM_BUTTONS = 4;

void setupButtons();
void updateButtons();         // call every loop iteration (handles debounce)
void processButtonEvents();   // call every loop iteration (acts on edges)

bool buttonPressed(int idx);  // for status / display readout
