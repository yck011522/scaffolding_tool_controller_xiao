// display.h — SSD1306 OLED rendering for the motor controller UI.
//
// 128x64, I²C address 0x3C, on Wire (SDA=GPIO15, SCL=GPIO16).

#pragma once

void setupDisplay();          // initializes Wire + OLED, shows splash
void updateDisplay();         // throttled refresh; safe to call every loop
void displayCalibrating();    // optional explicit splash during cal
