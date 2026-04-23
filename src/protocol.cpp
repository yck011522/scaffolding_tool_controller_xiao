// protocol.cpp — see protocol.h
//
// Command grammar (see docs/protocol.md for the canonical reference):
//
//   Motor:  TIGHTEN M1|M2 | LOOSEN M1|M2 | STOP
//   Status: STATUS | PING | VERSION
//   Config: SET <PARAM> M1|M2 [T|L] <value>
//           GET <PARAM> M1|M2 [T|L]
//           GET CONFIG M1|M2
//           RESET CONFIG | SAVE
//   USB:    LOG | FAST | STATUSV | HELP    (only when allowUsbExtras=true)

#include "protocol.h"
#include "motor_control.h"
#include "config_store.h"

bool logEnabled = false;
// fastLogEnabled is owned by motor_control.cpp (declared extern in motor_control.h
// and protocol.h). The protocol handler only toggles it.

// ── Tokenizer ──────────────────────────────────────────────────────
// Whitespace-splits up to MAX_TOK tokens. Leaves the original string
// alone (writes uppercase copies into `tokens[]` storage).
static const int MAX_TOK = 8;

struct Tokens {
    String t[MAX_TOK];
    int n = 0;
};

static void tokenize(const String &line, Tokens &out)
{
    out.n = 0;
    int i = 0, len = line.length();
    while (i < len && out.n < MAX_TOK)
    {
        // skip leading whitespace
        while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= len) break;
        int start = i;
        while (i < len && line[i] != ' ' && line[i] != '\t') i++;
        out.t[out.n] = line.substring(start, i);
        out.t[out.n].toUpperCase();
        out.n++;
    }
}

// ── Argument parsers ───────────────────────────────────────────────
// All return -1 on failure so callers can branch with a single check.
static int parseMotor(const String &tok)
{
    if (tok == "M1") return 0;
    if (tok == "M2") return 1;
    return -1;
}

static int parseAction(const String &tok)
{
    if (tok == "T") return 0;
    if (tok == "L") return 1;
    return -1;
}

static const char *motorTag(int m) { return m == 0 ? "M1" : "M2"; }
static const char *actTag(int a)   { return a == 0 ? "T"  : "L";  }

// ── Compact state token used by STATUS ──────────────────────────────
// Folds (state, action) into a single word per the protocol spec.
static const char *compactState(const Motor &m)
{
    if (m.state == STATE_STALLED) return "STALLED";
    if (m.state == STATE_RUNNING)
        return (m.action == ACTION_TIGHTEN) ? "TIGHTENING" : "LOOSENING";
    return "IDLE";
}

// If the running motor's limit was just changed, refresh its snapshot
// so the in-progress PI loop sees the new setpoint immediately.
static void refreshActiveLimit()
{
    if (activeMotor >= 0 && motors[activeMotor].state == STATE_RUNNING)
    {
        int ai = actionIdx(motors[activeMotor].action);
        motors[activeMotor].activeLimitMa = cfg[activeMotor].limitMa[ai];
    }
}

// ── Response helpers ───────────────────────────────────────────────
// Each writes exactly one line ending in '\n'.
static void replyOk(Print &out, const String &body)
{
    out.print("OK ");
    out.print(body);
    out.print('\n');
}
static void replyErr(Print &out, int code, const char *msg)
{
    out.print("ERR ");
    out.print(code);
    out.print(' ');
    out.print(msg);
    out.print('\n');
}

// ═══════════════════════════════════════════════════════════════════
// SET <PARAM> ... handler
//
// Returns true if the verb was a SET (handled or rejected). On success,
// updates cfg[] in-place and persists the field to NVS.
// ═══════════════════════════════════════════════════════════════════
static bool handleSet(const Tokens &tk, Print &out)
{
    if (tk.n < 4) { replyErr(out, 3, "SET needs <param> <motor> [<action>] <value>"); return true; }
    const String &param = tk.t[1];
    int mIdx = parseMotor(tk.t[2]);
    if (mIdx < 0) { replyErr(out, 4, "motor must be M1 or M2"); return true; }

    // ── Per-action parameters: SET <P> <M> <T|L> <value> (n == 5) ──
    if (param == "LIMIT" || param == "SLEW" || param == "PWMMAX" || param == "PWMSTART")
    {
        if (tk.n != 5) { replyErr(out, 3, "expected: SET <P> <M> T|L <value>"); return true; }
        int aIdx = parseAction(tk.t[3]);
        if (aIdx < 0) { replyErr(out, 5, "action must be T or L"); return true; }
        const String &val = tk.t[4];

        if (param == "LIMIT")
        {
            float v = val.toFloat();
            if (v < 50 || v > 1500) { replyErr(out, 3, "LIMIT must be 50..1500 mA"); return true; }
            cfg[mIdx].limitMa[aIdx] = v;
            saveLimit(mIdx, aIdx);
            refreshActiveLimit();
        }
        else if (param == "SLEW")
        {
            long v = val.toInt();
            if (v < 1 || v > 255) { replyErr(out, 3, "SLEW must be 1..255"); return true; }
            cfg[mIdx].slew[aIdx] = (int)v;
            saveSlew(mIdx, aIdx);
        }
        else if (param == "PWMMAX")
        {
            long v = val.toInt();
            if (v < 0 || v > 255) { replyErr(out, 3, "PWMMAX must be 0..255"); return true; }
            cfg[mIdx].pwmCeiling[aIdx] = (int)v;
            savePwmMax(mIdx, aIdx);
        }
        else /* PWMSTART */
        {
            long v = val.toInt();
            if (v < 0 || v > 255) { replyErr(out, 3, "PWMSTART must be 0..255"); return true; }
            cfg[mIdx].pwmStart[aIdx] = (int)v;
            savePwmStart(mIdx, aIdx);
        }
        replyOk(out, String("SET ") + param + " " + motorTag(mIdx) + " " + actTag(aIdx) + " " + val);
        return true;
    }

    // ── Single-value parameters: SET <P> <M> <value> (n == 4) ──────
    if (tk.n != 4) { replyErr(out, 3, "expected: SET <P> <M> <value>"); return true; }
    const String &val = tk.t[3];

    if (param == "KP")
    {
        float v = val.toFloat();
        if (v < 0 || v > 100) { replyErr(out, 3, "KP must be 0..100"); return true; }
        cfg[mIdx].kp = v; saveKp(mIdx);
    }
    else if (param == "KI")
    {
        float v = val.toFloat();
        if (v < 0 || v > 100) { replyErr(out, 3, "KI must be 0..100"); return true; }
        cfg[mIdx].ki = v; saveKi(mIdx);
    }
    else if (param == "RAMP")
    {
        long v = val.toInt();
        if (v < 0 || v > 5000) { replyErr(out, 3, "RAMP must be 0..5000 ms"); return true; }
        cfg[mIdx].rampMs = (unsigned long)v; saveRamp(mIdx);
    }
    else if (param == "PWMMIN")
    {
        long v = val.toInt();
        if (v < 0 || v > 255) { replyErr(out, 3, "PWMMIN must be 0..255"); return true; }
        cfg[mIdx].pwmMin = (int)v; savePwmMin(mIdx);
    }
    else if (param == "GEAR")
    {
        float v = val.toFloat();
        if (v < 1 || v > 500) { replyErr(out, 3, "GEAR must be 1..500"); return true; }
        cfg[mIdx].gearRatio = v; saveGear(mIdx);
    }
    else
    {
        replyErr(out, 1, "unknown SET parameter");
        return true;
    }
    replyOk(out, String("SET ") + param + " " + motorTag(mIdx) + " " + val);
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// GET <PARAM> ... handler
// ═══════════════════════════════════════════════════════════════════
static bool handleGet(const Tokens &tk, Print &out)
{
    if (tk.n < 3) { replyErr(out, 3, "GET needs <param> <motor> [<action>]"); return true; }
    const String &param = tk.t[1];

    if (param == "CONFIG")
    {
        int mIdx = parseMotor(tk.t[2]);
        if (mIdx < 0) { replyErr(out, 4, "motor must be M1 or M2"); return true; }
        const MotorConfig &c = cfg[mIdx];
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "CONFIG %s lim_T=%.0f lim_L=%.0f kp=%.3f ki=%.4f ramp=%lu pmin=%d pmax_T=%d pmax_L=%d pstart_T=%d pstart_L=%d slew_T=%d slew_L=%d gear=%.1f",
                 motorTag(mIdx),
                 c.limitMa[0], c.limitMa[1], c.kp, c.ki, c.rampMs,
                 c.pwmMin, c.pwmCeiling[0], c.pwmCeiling[1],
                 c.pwmStart[0], c.pwmStart[1], c.slew[0], c.slew[1], c.gearRatio);
        replyOk(out, buf);
        return true;
    }

    int mIdx = parseMotor(tk.t[2]);
    if (mIdx < 0) { replyErr(out, 4, "motor must be M1 or M2"); return true; }
    const MotorConfig &c = cfg[mIdx];

    auto needAction = [&](int &aIdx) -> bool
    {
        if (tk.n < 4) { replyErr(out, 3, "action token (T or L) required"); return false; }
        aIdx = parseAction(tk.t[3]);
        if (aIdx < 0) { replyErr(out, 5, "action must be T or L"); return false; }
        return true;
    };

    char buf[80];
    int aIdx;
    if (param == "LIMIT")    { if (!needAction(aIdx)) return true; snprintf(buf, sizeof(buf), "LIMIT %s %s %.0f",  motorTag(mIdx), actTag(aIdx), c.limitMa[aIdx]);    replyOk(out, buf); return true; }
    if (param == "SLEW")     { if (!needAction(aIdx)) return true; snprintf(buf, sizeof(buf), "SLEW %s %s %d",     motorTag(mIdx), actTag(aIdx), c.slew[aIdx]);       replyOk(out, buf); return true; }
    if (param == "PWMMAX")   { if (!needAction(aIdx)) return true; snprintf(buf, sizeof(buf), "PWMMAX %s %s %d",   motorTag(mIdx), actTag(aIdx), c.pwmCeiling[aIdx]); replyOk(out, buf); return true; }
    if (param == "PWMSTART") { if (!needAction(aIdx)) return true; snprintf(buf, sizeof(buf), "PWMSTART %s %s %d", motorTag(mIdx), actTag(aIdx), c.pwmStart[aIdx]);   replyOk(out, buf); return true; }
    if (param == "KP")       { snprintf(buf, sizeof(buf), "KP %s %.3f",     motorTag(mIdx), c.kp);        replyOk(out, buf); return true; }
    if (param == "KI")       { snprintf(buf, sizeof(buf), "KI %s %.4f",     motorTag(mIdx), c.ki);        replyOk(out, buf); return true; }
    if (param == "RAMP")     { snprintf(buf, sizeof(buf), "RAMP %s %lu",    motorTag(mIdx), c.rampMs);    replyOk(out, buf); return true; }
    if (param == "PWMMIN")   { snprintf(buf, sizeof(buf), "PWMMIN %s %d",   motorTag(mIdx), c.pwmMin);    replyOk(out, buf); return true; }
    if (param == "GEAR")     { snprintf(buf, sizeof(buf), "GEAR %s %.1f",   motorTag(mIdx), c.gearRatio); replyOk(out, buf); return true; }

    replyErr(out, 1, "unknown GET parameter");
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// Entry point — single dispatch shared by USB and RS-485
// ═══════════════════════════════════════════════════════════════════
bool handleProtocolLine(const String &lineIn, Print &out, bool allowUsbExtras)
{
    String line = lineIn;
    line.trim();
    if (line.length() == 0) return true;   // ignore blank line, no reply

    Tokens tk; tokenize(line, tk);
    if (tk.n == 0) return true;
    const String &v = tk.t[0];

    // ── Motor control ──────────────────────────────────────────────
    if (v == "TIGHTEN" || v == "LOOSEN")
    {
        if (tk.n != 2) { replyErr(out, 3, "expected: TIGHTEN|LOOSEN <M>"); return true; }
        int mIdx = parseMotor(tk.t[1]);
        if (mIdx < 0) { replyErr(out, 4, "motor must be M1 or M2"); return true; }
        if (motors[mIdx].state == STATE_STALLED)
        {
            replyErr(out, 2, "STALLED");
            return true;
        }
        MotorAction a = (v == "TIGHTEN") ? ACTION_TIGHTEN : ACTION_LOOSEN;
        startMotor(mIdx, a);
        replyOk(out, v + " " + motorTag(mIdx));
        return true;
    }
    if (v == "STOP")
    {
        stopAllMotors();
        replyOk(out, "STOP");
        return true;
    }

    // ── Status / handshake ─────────────────────────────────────────
    if (v == "STATUS")
    {
        // Take one current reading, refresh both motors' RPM (for stall
        // detection bookkeeping outside the control loop), then format.
        float currentMa = readCurrentMa();
        for (int i = 0; i < NUM_MOTORS; i++) updateMotorRpm(motors[i]);
        int pwmPct = (activeMotor >= 0) ? (motors[activeMotor].pwmValue * 100 / 255) : 0;
        char buf[80];
        snprintf(buf, sizeof(buf), "%s %s %d %d",
                 compactState(motors[0]), compactState(motors[1]),
                 (int)currentMa, pwmPct);
        replyOk(out, buf);
        return true;
    }
    if (v == "PING")    { replyOk(out, "PONG"); return true; }
    if (v == "VERSION") { replyOk(out, FIRMWARE_VERSION); return true; }

    // ── Configuration ──────────────────────────────────────────────
    if (v == "SET")          return handleSet(tk, out);
    if (v == "GET")          return handleGet(tk, out);
    if (v == "RESET")
    {
        if (tk.n == 2 && tk.t[1] == "CONFIG")
        {
            configStoreReset();
            replyOk(out, "RESET CONFIG");
            return true;
        }
        replyErr(out, 1, "expected: RESET CONFIG");
        return true;
    }
    if (v == "SAVE") { replyOk(out, "SAVE"); return true; }   // SET commands persist automatically

    // ── USB-only extras ────────────────────────────────────────────
    if (allowUsbExtras)
    {
        if (v == "LOG")
        {
            logEnabled = !logEnabled;
            replyOk(out, String("LOG ") + (logEnabled ? "ON" : "OFF"));
            return true;
        }
        if (v == "FAST")
        {
            fastLogEnabled = !fastLogEnabled;
            if (fastLogEnabled)
                out.println("FAST_START,time_ms,motor,current_mA,pwm,rpm,integral");
            replyOk(out, String("FAST ") + (fastLogEnabled ? "ON" : "OFF"));
            return true;
        }
        if (v == "STATUSV") { printVerboseStatus(out); return true; }
        if (v == "HELP")    { printHelp(out); return true; }
    }

    replyErr(out, 1, "unknown command");
    return false;
}

// ═══════════════════════════════════════════════════════════════════
// USB-side helpers (called only when allowUsbExtras is true)
// ═══════════════════════════════════════════════════════════════════
void printVerboseStatus(Print &out)
{
    float currentMa = readCurrentMa();
    out.println("════════════════════════════════════════");
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        const MotorConfig &c = cfg[i];
        out.printf("  M%d: limit T=%.0f L=%.0f mA  Kp=%.3f Ki=%.4f  ramp=%lums\n",
                   i + 1, c.limitMa[0], c.limitMa[1], c.kp, c.ki, c.rampMs);
        out.printf("       pwm[min=%d max=%d]  ceiling T=%d L=%d  start T=%d L=%d  slew T=%d L=%d  gear=%.1f\n",
                   c.pwmMin, c.pwmMax,
                   c.pwmCeiling[0], c.pwmCeiling[1],
                   c.pwmStart[0], c.pwmStart[1],
                   c.slew[0], c.slew[1], c.gearRatio);
    }
    out.printf("  Measured current: %.1f mA\n", currentMa);
    out.printf("  Active motor: %s\n",
               activeMotor >= 0 ? (activeMotor == 0 ? "M1" : "M2") : "none");
    for (int i = 0; i < NUM_MOTORS; i++)
    {
        Motor &m = motors[i];
        updateMotorRpm(m);
        float shaftRpm = (cfg[i].gearRatio > 0) ? (m.motorRpm / cfg[i].gearRatio) : 0.0f;
        out.printf("  M%d: %-10s PWM=%3d (%2d%%)  RPM=%.0f (shaft %.1f)  limit=%.0f mA\n",
                   i + 1, compactState(m),
                   m.pwmValue, (int)(m.pwmValue * 100 / 255),
                   m.motorRpm, shaftRpm, m.activeLimitMa);
    }
    out.println("════════════════════════════════════════");
}

void printPeriodicLog(Print &out)
{
    float currentMa = readCurrentMa();
    for (int i = 0; i < NUM_MOTORS; i++) updateMotorRpm(motors[i]);
    out.printf("LOG,%lu,%d,%s,%d,%.0f,%s,%d,%.0f,%.1f\n",
               millis(), activeMotor + 1,
               compactState(motors[0]), motors[0].pwmValue, motors[0].motorRpm,
               compactState(motors[1]), motors[1].pwmValue, motors[1].motorRpm,
               currentMa);
}

void printHelp(Print &out)
{
    out.println("Commands (case-insensitive):");
    out.println("  Motor:  TIGHTEN M1|M2 | LOOSEN M1|M2 | STOP");
    out.println("  Status: STATUS | PING | VERSION");
    out.println("  Config: SET LIMIT|SLEW|PWMMAX|PWMSTART <M> <T|L> <v>");
    out.println("          SET KP|KI|RAMP|PWMMIN|GEAR <M> <v>");
    out.println("          GET <param> <M> [<T|L>]   GET CONFIG <M>");
    out.println("          RESET CONFIG | SAVE");
    out.println("  USB:    LOG | FAST | STATUSV | HELP");
}
