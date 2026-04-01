// test_rs485 — RS-485 half-duplex communication test
//
// Verifies bidirectional RS-485 communication via MAX3485 transceiver.
// USB-C Serial is used for commands and debug output.
// Serial1 (UART1) is used for the RS-485 data path.
//
// Wiring:
//   XIAO D6 (GPIO43, TX) → MAX3485 DI (driver input)
//   XIAO D7 (GPIO44, RX) → MAX3485 RO (receiver output)
//   XIAO D0 (GPIO1)      → MAX3485 DE + RE (tied together, direction control)
//   MAX3485 VCC → 3.3 V
//   MAX3485 GND → GND
//   MAX3485 A/B → RS-485 bus (to USB-RS485 adapter on PC)
//
// ⚠ Module label confusion:
//   The pin labeled "TXD" on the MAX3485 module is RO (receiver output)
//   → connect to XIAO D7 (RX).
//   The pin labeled "RXD" on the module is DI (driver input)
//   → connect to XIAO D6 (TX).
//   In short: module TXD → MCU RX, module RXD → MCU TX.
//
// Direction control:
//   DE/RE HIGH → transmit mode (DI drives A/B)
//   DE/RE LOW  → receive mode  (A/B drives RO)
//
// Serial commands (115200 baud, via USB-C):
//   SEND <message>   Send a message on RS-485
//   ECHO             Toggle echo mode (RS-485 RX → RS-485 TX)
//   BAUD <rate>      Change RS-485 baud rate (default: 115200)
//   PING             Send "PING" on RS-485, expect "PONG" reply
//   STATUS           Print current config
//
// Any data received on RS-485 is printed on USB-C Serial for monitoring.

#include <Arduino.h>

// ── Pin definitions ─────────────────────────────────────────────────
const int PIN_RS485_TX = D6;   // GPIO43 → MAX3485 DI
const int PIN_RS485_RX = D7;   // GPIO44 → MAX3485 RO
const int PIN_DE_RE    = D0;   // GPIO1  → MAX3485 DE + ~RE

// ── RS-485 settings ─────────────────────────────────────────────────
unsigned long rs485Baud = 115200;
bool echoEnabled = true;

// ── Direction control ───────────────────────────────────────────────
void rs485Transmit() {
    digitalWrite(PIN_DE_RE, HIGH);
    delayMicroseconds(10);  // allow transceiver to settle
}

void rs485Receive() {
    delayMicroseconds(10);
    digitalWrite(PIN_DE_RE, LOW);
}

// ── RS-485 send ─────────────────────────────────────────────────────
void rs485Send(const char* msg) {
    rs485Transmit();
    Serial1.print(msg);
    Serial1.print("\r\n");
    Serial1.flush();  // wait for TX buffer to drain before switching direction
    rs485Receive();
}

void rs485SendRaw(const uint8_t* data, size_t len) {
    rs485Transmit();
    Serial1.write(data, len);
    Serial1.flush();
    rs485Receive();
}

// ── Command parsing ─────────────────────────────────────────────────
void processCommand(String cmd) {
    cmd.trim();
    String upper = cmd;
    upper.toUpperCase();

    if (upper.startsWith("SEND ")) {
        String msg = cmd.substring(5);
        Serial.print("[TX] ");
        Serial.println(msg);
        rs485Send(msg.c_str());
    }
    else if (upper == "ECHO") {
        echoEnabled = !echoEnabled;
        Serial.print("Echo mode: ");
        Serial.println(echoEnabled ? "ON" : "OFF");
    }
    else if (upper.startsWith("BAUD ")) {
        rs485Baud = upper.substring(5).toInt();
        if (rs485Baud < 300) rs485Baud = 115200;
        Serial1.end();
        Serial1.begin(rs485Baud, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
        Serial.print("RS-485 baud rate set to ");
        Serial.println(rs485Baud);
    }
    else if (upper == "PING") {
        Serial.println("[TX] PING");
        rs485Send("PING");
        // Wait for reply
        unsigned long start = millis();
        String reply = "";
        while (millis() - start < 2000) {
            if (Serial1.available()) {
                char c = Serial1.read();
                if (c == '\n' || c == '\r') {
                    if (reply.length() > 0) break;
                } else {
                    reply += c;
                }
            }
        }
        if (reply.length() > 0) {
            Serial.print("[RX] ");
            Serial.print(reply);
            Serial.print("  (");
            Serial.print(millis() - start);
            Serial.println(" ms)");
        } else {
            Serial.println("[RX] No reply (timeout 2s)");
        }
    }
    else if (upper == "STATUS") {
        Serial.println("────────────────────────────");
        Serial.print("RS-485 baud: ");
        Serial.println(rs485Baud);
        Serial.print("Echo mode:   ");
        Serial.println(echoEnabled ? "ON" : "OFF");
        Serial.print("TX pin:      D6 (GPIO");
        Serial.print(PIN_RS485_TX);
        Serial.println(")");
        Serial.print("RX pin:      D7 (GPIO");
        Serial.print(PIN_RS485_RX);
        Serial.println(")");
        Serial.print("DE/RE pin:   D0 (GPIO");
        Serial.print(PIN_DE_RE);
        Serial.println(")");
        Serial.println("────────────────────────────");
    }
    else {
        Serial.println("Commands: SEND <msg>, ECHO, PING, BAUD <rate>, STATUS");
    }
}

// ── Setup & Loop ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // DE/RE direction control — start in receive mode
    pinMode(PIN_DE_RE, OUTPUT);
    digitalWrite(PIN_DE_RE, LOW);

    // RS-485 UART
    Serial1.begin(rs485Baud, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);

    delay(1000);
    Serial.println();
    Serial.println("=== test_rs485 ===");
    Serial.println("TX: D6, RX: D7, DE/RE: D0");
    Serial.print("Baud: ");
    Serial.println(rs485Baud);
    Serial.println();
    Serial.println("Commands: SEND <msg>, ECHO, PING, BAUD <rate>, STATUS");
    Serial.println();
}

void loop() {
    // Process USB-C serial commands
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }

    // Process incoming RS-485 data (line-terminated)
    static String rs485Buffer = "";
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n' || c == '\r') {
            if (rs485Buffer.length() > 0) {
                Serial.print("[RX] ");
                Serial.println(rs485Buffer);

                // Echo back if enabled
                if (echoEnabled) {
                    Serial.print("[ECHO TX] ");
                    Serial.println(rs485Buffer);
                    rs485Send(rs485Buffer.c_str());
                }
                rs485Buffer = "";
            }
        } else {
            rs485Buffer += c;
        }
    }
}
