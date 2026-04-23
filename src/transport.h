// transport.h — Line-buffered serial transports for USB and RS-485.
//
// Both transports share the same protocol grammar (see protocol.h) but
// each owns its own input buffer and direction-control state. They are
// driven by polling — call usbConsolePoll() and rs485Poll() once per
// main-loop iteration.

#pragma once

#include <Arduino.h>

void usbConsoleBegin();
void usbConsolePoll();

void rs485Begin();
void rs485Poll();
