// OLED display text-size test.
//
// Board: Waveshare ESP32-S3-Tiny
//
// Cycles through different text sizes on a 0.96" SSD1306 (128x64) OLED
// so the user can visually judge which sizes are legible.
//
// Each page is shown for 5 seconds, then automatically advances.
// The cycle repeats forever.
//
// Wiring:
//   SDA → GPIO15
//   SCL → GPIO16
//   VCC → 3.3 V
//   GND → GND
//
// Module address selector set to 0x78 (8-bit) = 0x3C (7-bit).

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Display dimensions — 0.96" module uses the full 128x64 framebuffer.
// No offset needed (unlike the old 0.66" module which had a 64x48 window).
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// 7-bit I2C address (module selector at 0x78 → 0x3C in Arduino Wire)
#define OLED_ADDR 0x3C

// I2C pins (Waveshare ESP32-S3-Tiny)
#define I2C_SDA 15 // GPIO15
#define I2C_SCL 16 // GPIO16

// Other system pins — set to high-Z in setup()
const int PIN_BTN1 = 1;
const int PIN_BTN2 = 2;
const int PIN_BTN3 = 3;
const int PIN_BTN4 = 4;
const int PIN_M1_DIR = 5;
const int PIN_M1_PWM = 6;
const int PIN_M1_CAP = 7;
const int PIN_M2_DIR = 9;
const int PIN_M2_PWM = 10;
const int PIN_M2_CAP = 11;
const int PIN_INA_OUT = 13;
const int PIN_DE_RE = 18;

// Time each test page is displayed (milliseconds)
#define PAGE_DURATION 5000

// No reset pin on this module — pass -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Page drawing functions ---

// Page 1: textSize(1) — 6x8 px per char → 21 chars × 8 lines
void page1()
{
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SIZE 1  6x8 per char");
  display.setCursor(0, 8);
  display.println("21 chars per line");
  display.setCursor(0, 16);
  display.println("8 lines fit on scrn");
  display.setCursor(0, 24);
  display.println("ABCDEFGHIJKLMNOPQRSTU");
  display.setCursor(0, 32);
  display.println("0123456789 !@#$%^&*()");
  display.setCursor(0, 40);
  display.println("The quick brown fox");
  display.setCursor(0, 48);
  display.println("jumps over lazy dog");
  display.setCursor(0, 56);
  display.println("128x64 full screen!");
  Serial.println("[OLED] Page 1: textSize(1) — 6x8px, 21 chars x 8 lines");
}

// Page 2: textSize(2) — 12x16 px per char → 10 chars × 4 lines
void page2()
{
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("SIZE 2");
  display.setCursor(0, 16);
  display.println("12x16 px");
  display.setCursor(0, 32);
  display.println("10chr/line");
  display.setCursor(0, 48);
  display.println("ABCDEFGHIJ");
  Serial.println("[OLED] Page 2: textSize(2) — 12x16px, 10 chars x 4 lines");
}

// Page 3: mixed sizes — size 2 header + size 1 body
void page3()
{
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("MOTOR 1");
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println("Speed:   830 Hz");
  display.setCursor(0, 24);
  display.println("Current: 1.2 A");
  display.setCursor(0, 32);
  display.println("Direction: CW");
  display.setCursor(0, 40);
  display.println("Voltage: 24.1 V");
  display.setCursor(0, 48);
  display.println("Status:  Running");
  display.setCursor(0, 56);
  display.println("Gearbox: 1:56");
  Serial.println("[OLED] Page 3: mixed — size 2 header + size 1 body");
}

// Page 4: size 1, realistic status display
void page4()
{
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("====== STATUS =======");
  display.setCursor(0, 8);
  display.println("M1: CW   830 Hz");
  display.setCursor(0, 16);
  display.println("M2: CCW  450 Hz");
  display.setCursor(0, 24);
  display.println("Current: 1.2 A");
  display.setCursor(0, 32);
  display.println("Voltage: 24.1 V");
  display.setCursor(0, 40);
  display.println("RS-485:  Connected");
  display.setCursor(0, 48);
  display.println("Btns: 0 0 0 0");
  display.setCursor(0, 56);
  display.println("Uptime: 00:12:34");
  Serial.println("[OLED] Page 4: size 1, 8-line status layout");
}

// Page 5: size 1, max fill — all 168 character slots (21x8)
void page5()
{
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ABCDEFGHIJKLMNOPQRSTU");
  display.setCursor(0, 8);
  display.print("VWXYZabcdefghijklmnop");
  display.setCursor(0, 16);
  display.print("qrstuvwxyz0123456789!");
  display.setCursor(0, 24);
  display.print("@#$%^&*()-=_+[]{}|;:");
  display.setCursor(0, 32);
  display.print("'\",./<>?`~ ABCDEFGH");
  display.setCursor(0, 40);
  display.print("IJKLMNOPQRSTUVWXYZ012");
  display.setCursor(0, 48);
  display.print("3456789abcdefghijklmn");
  display.setCursor(0, 56);
  display.print("opqrstuvwxyz!@#$%^&*(");
  Serial.println("[OLED] Page 5: size 1, max fill — 168 chars (21x8)");
}

// Page 6: textSize(3) — 18x24 px per char → 7 chars × 2 lines
void page6()
{
  display.setTextSize(3);
  display.setCursor(0, 0);
  display.println("1.2 A");
  display.setCursor(0, 24);
  display.println("830 Hz");
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.println("Motor 1  CW  Running");
  Serial.println("[OLED] Page 6: textSize(3) — 18x24px, 7 chars x 2 lines");
}

// Page 7: size 2 numbers — for large readouts
void page7()
{
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("1.2 A");
  display.setCursor(0, 16);
  display.println("830 Hz");
  display.setTextSize(1);
  display.setCursor(0, 32);
  display.println("Motor Current");
  display.setCursor(0, 40);
  display.println("Feedback Frequency");
  display.setCursor(0, 48);
  display.println("Direction: CW");
  display.setCursor(0, 56);
  display.println("Voltage: 24.1 V  OK");
  Serial.println("[OLED] Page 7: size 2 numbers + size 1 labels");
}

typedef void (*PageFunc)();
PageFunc pages[] = {page1, page2, page3, page4, page5, page6, page7};
const int NUM_PAGES = sizeof(pages) / sizeof(pages[0]);

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[OLED] Starting OLED text-size test");

  // ── Set all non-tested pins to high-Z (INPUT) ──────────────────────
  pinMode(PIN_BTN1, INPUT);
  pinMode(PIN_BTN2, INPUT);
  pinMode(PIN_BTN3, INPUT);
  pinMode(PIN_BTN4, INPUT);
  pinMode(PIN_M1_DIR, INPUT);
  pinMode(PIN_M1_PWM, INPUT);
  pinMode(PIN_M1_CAP, INPUT);
  pinMode(PIN_M2_DIR, INPUT);
  pinMode(PIN_M2_PWM, INPUT);
  pinMode(PIN_M2_CAP, INPUT);
  pinMode(PIN_INA_OUT, INPUT);
  pinMode(PIN_DE_RE, INPUT);

  // Initialise I2C on the correct pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialise the SSD1306 display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("[OLED] SSD1306 init failed!");
    while (true)
    {
      delay(1000);
    }
  }
  Serial.println("[OLED] SSD1306 init OK");
  Serial.printf("[OLED] Screen: %dx%d, %d pages, %d ms each\n", SCREEN_WIDTH, SCREEN_HEIGHT, NUM_PAGES, PAGE_DURATION);
}

void loop()
{
  for (int i = 0; i < NUM_PAGES; i++)
  {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    Serial.printf("[OLED] Showing page %d/%d\n", i + 1, NUM_PAGES);
    pages[i]();
    display.display();
    delay(PAGE_DURATION);
  }
}
