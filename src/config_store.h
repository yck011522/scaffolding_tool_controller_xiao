// config_store.h — NVS persistence for the per-motor MotorConfig array.
//
// Every successful SET command writes the affected field to NVS so the
// tuned parameter survives a power cycle. On boot, configStoreLoad()
// fills cfg[] with stored values (falling back to the compiled defaults
// for any field that was never written).
//
// Storage layout (Preferences namespace "scaff"):
//   Keys are "<field>_<motor>[_<action>]" where motor ∈ {1,2}, action ∈ {T,L}.
//   Examples: "lim_1_T", "kp_1", "slew_2_L", "ramp_1", "pmin_2",
//             "pmax_1_T", "pstart_2_L", "gear_2".

#pragma once

#include <Arduino.h>
#include "motor_config.h"

void configStoreBegin();          // open the NVS namespace (call once in setup)
void configStoreLoad();           // overwrite cfg[] with stored values
void configStoreReset();          // wipe the NVS namespace (defaults restore on next boot)

// Per-field savers. Each is called from the protocol handler immediately
// after the in-memory cfg[] field is updated, so the value is durable.
void saveLimit(int mIdx, int aIdx);
void saveKp(int mIdx);
void saveKi(int mIdx);
void saveSlew(int mIdx, int aIdx);
void saveRamp(int mIdx);
void savePwmMin(int mIdx);
void savePwmMax(int mIdx, int aIdx);
void savePwmStart(int mIdx, int aIdx);
void saveGear(int mIdx);
