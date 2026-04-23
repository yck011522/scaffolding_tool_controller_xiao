// transport.cpp — see transport.h
//
// Two line-readers feeding the same protocol parser:
//
//   USB-CDC : Serial   (full command set + LOG/FAST/STATUSV/HELP extras)
//   RS-485  : Serial1  (production link, no streaming, half-duplex DE/RE)
//
// Both follow the same pattern: accumulate characters until a CR or LF,
// then hand the line to handleProtocolLine() with the appropriate
// `allowUsbExtras` flag. Lines longer than the buffer are silently
// truncated (caller will get an ERR for the malformed line).

#include "transport.h"
#include "protocol.h"

// ── USB-CDC ─────────────────────────────────────────────────────────
static const size_t USB_BUF_MAX = 96;
static String usbBuf;

void usbConsoleBegin()
{
    Serial.begin(115200);
    usbBuf.reserve(USB_BUF_MAX);
}

void usbConsolePoll()
{
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (usbBuf.length() > 0)
            {
                handleProtocolLine(usbBuf, Serial, /*allowUsbExtras=*/true);
                usbBuf = "";
            }
        }
        else if (usbBuf.length() < USB_BUF_MAX)
        {
            usbBuf += c;
        }
        // else: drop excess characters — line will fail to parse cleanly,
        // caller retries with a shorter command.
    }
}

// ── RS-485 (Serial1 + DE/RE direction control) ─────────────────────
// MAX3485 wiring on the Waveshare ESP32-S3-Tiny board:
//   GPIO43 (TX header) → MAX3485 DI   (driver input)
//   GPIO44 (RX header) → MAX3485 RO   (receiver output)
//   GPIO18             → MAX3485 DE+RE (tied; HIGH=transmit, LOW=receive)
static const int PIN_RS485_TX = 43;
static const int PIN_RS485_RX = 44;
static const int PIN_DE_RE    = 18;
static const size_t RS485_BUF_MAX = 96;

static String rs485Buf;

// Print adapter that flips the transceiver into TX mode for the duration
// of one response, then drops back to RX. Buffers writes into a String
// because flush() must be called exactly once after the whole response
// is queued — otherwise we'd flap DE between every printf chunk.
class Rs485Reply : public Print
{
public:
    String body;
    Rs485Reply() { body.reserve(RS485_BUF_MAX); }
    size_t write(uint8_t b) override { body += (char)b; return 1; }
    size_t write(const uint8_t *buf, size_t n) override
    {
        for (size_t i = 0; i < n; i++) body += (char)buf[i];
        return n;
    }
    void send()
    {
        if (body.length() == 0) return;
        digitalWrite(PIN_DE_RE, HIGH);    // enter transmit mode
        delayMicroseconds(10);            // let the line settle
        Serial1.print(body);
        Serial1.flush();                  // wait for TX FIFO to drain
        delayMicroseconds(10);
        digitalWrite(PIN_DE_RE, LOW);     // back to receive mode
    }
};

void rs485Begin()
{
    pinMode(PIN_DE_RE, OUTPUT);
    digitalWrite(PIN_DE_RE, LOW);   // start in receive mode
    Serial1.begin(115200, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    rs485Buf.reserve(RS485_BUF_MAX);
}

void rs485Poll()
{
    while (Serial1.available())
    {
        char c = (char)Serial1.read();
        if (c == '\n' || c == '\r')
        {
            if (rs485Buf.length() > 0)
            {
                Rs485Reply reply;
                handleProtocolLine(rs485Buf, reply, /*allowUsbExtras=*/false);
                reply.send();
                rs485Buf = "";
            }
        }
        else if (rs485Buf.length() < RS485_BUF_MAX)
        {
            rs485Buf += c;
        }
    }
}
