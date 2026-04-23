// config_store.cpp — see config_store.h
//
// Wraps the ESP32 Preferences (NVS) library. One key per scalar field.
// A field that was never saved returns the in-memory default unchanged,
// so a fresh device with empty NVS just runs with the compiled defaults.

#include "config_store.h"
#include "motor_control.h"
#include <Preferences.h>

static Preferences prefs;
static const char *NS = "scaff";

// ── Key builders ───────────────────────────────────────────────────
// NVS keys are limited to 15 characters; the formats below stay well
// inside that bound. mIdx/aIdx are 0-based but encoded as 1-based and
// T/L for human-readable nvs dumps.
static String key1(const char *field, int mIdx)
{
    return String(field) + "_" + String(mIdx + 1);
}
static String key2(const char *field, int mIdx, int aIdx)
{
    return String(field) + "_" + String(mIdx + 1) + "_" + (aIdx == 0 ? "T" : "L");
}

void configStoreBegin()
{
    // Read/write mode. Preferences.begin returns false if the namespace
    // can't be opened; we keep going regardless because the runtime is
    // safe with empty NVS (defaults already in cfg[]).
    if (!prefs.begin(NS, false))
        Serial.println("[CFG] WARN: Preferences.begin failed");
}

void configStoreLoad()
{
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        MotorConfig &c = cfg[i];

        // Per-action arrays (tighten = 0, loosen = 1).
        for (int a = 0; a < 2; a++)
        {
            c.limitMa[a]    = prefs.getFloat(key2("lim",    i, a).c_str(), c.limitMa[a]);
            c.slew[a]       = prefs.getInt  (key2("slew",   i, a).c_str(), c.slew[a]);
            c.pwmCeiling[a] = prefs.getInt  (key2("pmax",   i, a).c_str(), c.pwmCeiling[a]);
            c.pwmStart[a]   = prefs.getInt  (key2("pstart", i, a).c_str(), c.pwmStart[a]);
        }

        // Single-value fields.
        c.kp        = prefs.getFloat (key1("kp",   i).c_str(), c.kp);
        c.ki        = prefs.getFloat (key1("ki",   i).c_str(), c.ki);
        c.rampMs    = prefs.getULong (key1("ramp", i).c_str(), c.rampMs);
        c.pwmMin    = prefs.getInt   (key1("pmin", i).c_str(), c.pwmMin);
        c.gearRatio = prefs.getFloat (key1("gear", i).c_str(), c.gearRatio);
    }
    Serial.println("[CFG] Loaded persisted configuration from NVS");
}

void configStoreReset()
{
    // 1) Wipe NVS so future boots start from defaults.
    // 2) Restore the in-memory cfg[] immediately from the immutable
    //    factory table — otherwise the live tunables (and the OLED that
    //    reads them) would still show whatever was last SET, until the
    //    next reboot. Doing both keeps NVS and RAM in sync.
    prefs.clear();
    motorLoadDefaults();
    Serial.println("[CFG] NVS cleared and cfg[] restored to factory defaults");
}

// ── Per-field savers ───────────────────────────────────────────────
// Each takes the current value out of cfg[] (the protocol handler has
// already written it there) and stores it under its NVS key.
void saveLimit(int mIdx, int aIdx)    { prefs.putFloat (key2("lim",    mIdx, aIdx).c_str(), cfg[mIdx].limitMa[aIdx]); }
void saveKp(int mIdx)                 { prefs.putFloat (key1("kp",     mIdx).c_str(),       cfg[mIdx].kp); }
void saveKi(int mIdx)                 { prefs.putFloat (key1("ki",     mIdx).c_str(),       cfg[mIdx].ki); }
void saveSlew(int mIdx, int aIdx)     { prefs.putInt   (key2("slew",   mIdx, aIdx).c_str(), cfg[mIdx].slew[aIdx]); }
void saveRamp(int mIdx)               { prefs.putULong (key1("ramp",   mIdx).c_str(),       cfg[mIdx].rampMs); }
void savePwmMin(int mIdx)             { prefs.putInt   (key1("pmin",   mIdx).c_str(),       cfg[mIdx].pwmMin); }
void savePwmMax(int mIdx, int aIdx)   { prefs.putInt   (key2("pmax",   mIdx, aIdx).c_str(), cfg[mIdx].pwmCeiling[aIdx]); }
void savePwmStart(int mIdx, int aIdx) { prefs.putInt   (key2("pstart", mIdx, aIdx).c_str(), cfg[mIdx].pwmStart[aIdx]); }
void saveGear(int mIdx)               { prefs.putFloat (key1("gear",   mIdx).c_str(),       cfg[mIdx].gearRatio); }
