// protocol.h — Shared command parser for USB and RS-485.
//
// One entry point: handleProtocolLine() takes a single command line and
// writes one response line (terminated with '\n') to `out`. The same
// function is called from both transports so the command grammar can
// never drift between USB and RS-485.
//
// USB-only debug verbs (LOG, FAST, STATUSV, HELP) are gated by the
// `allowUsbExtras` flag so RS-485 callers receive `ERR 1` for them.
//
// See docs/protocol.md for the full command reference.

#pragma once

#include <Arduino.h>

constexpr const char *FIRMWARE_VERSION = "v1.0.0";

// USB transport sets logEnabled / fastLogEnabled to drive its own
// streaming output. The protocol handler only flips the flags.
extern bool logEnabled;       // 200 ms periodic CSV log line (USB only)
extern bool fastLogEnabled;   // per-control-cycle CSV stream (USB only)

// Process one command line. `out` receives exactly one response line
// (always ends with '\n'). Returns true if the verb was recognised
// (i.e. response is OK or domain-specific ERR), false for unknown verb
// (ERR 1 still written so callers can just forward `out` unconditionally).
bool handleProtocolLine(const String &line, Print &out, bool allowUsbExtras);

// Helpers shared with USB-side verbose output.
void printVerboseStatus(Print &out);
void printHelp(Print &out);
void printPeriodicLog(Print &out);
