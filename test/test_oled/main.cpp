// OLED display text-size test.
//
// Cycles through different text sizes on a 0.66" SSD1306 (64x48) OLED
// so the user can visually judge which sizes are legible.
//
// Each page is shown for 5 seconds, then automatically advances.
// The cycle repeats forever.
//
// Wiring:
//   SDA → I2C SDA pin (GPIO TBD for Waveshare ESP32-S3-Tiny)
//   SCL → I2C SCL pin (GPIO TBD for Waveshare ESP32-S3-Tiny)
//   VCC → 3.3 V
//   GND → GND
//
// Module address selector set to 0x78 (8-bit) = 0x3C (7-bit).
// Note: I2C pin assignments (D4/D5) are from XIAO testing and will need
// updating once the Waveshare pin map is finalised.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Display dimensions — 128x64 internal buffer, but the 0.66" module only
// shows a 64x48 pixel visible window at offset (32, 16) in the buffer.
// The driver must be initialised with the full 128x64 size.
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// Visible area offset — all drawing must start from here
#define VIS_X 32
#define VIS_Y 16
#define VIS_W 64    // visible width in pixels
#define VIS_H 48    // visible height in pixels

// 7-bit I2C address (module selector at 0x78 → 0x3C in Arduino Wire)
#define OLED_ADDR 0x3C

// I2C pins (from XIAO testing — update needed for Waveshare board)
#define I2C_SDA D4   // GPIO5
#define I2C_SCL D5   // GPIO6

// Time each test page is displayed (milliseconds)
#define PAGE_DURATION 5000

// No reset pin on this module — pass -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Page drawing functions ---

// Page 1: textSize(1) — 6x8 px per char → 10 chars × 6 lines visible
void page1() {
  display.setTextSize(1);
  display.setCursor(VIS_X, VIS_Y);
  display.println("SIZE 1 6x8");
  display.setCursor(VIS_X, VIS_Y + 8);
  display.println("10chr/line");
  display.setCursor(VIS_X, VIS_Y + 16);
  display.println("6 lines fit");
  display.setCursor(VIS_X, VIS_Y + 24);
  display.println("ABCDEFGHIJ");
  display.setCursor(VIS_X, VIS_Y + 32);
  display.println("0123456789");
  display.setCursor(VIS_X, VIS_Y + 40);
  display.println("!@#$%^&*()");
  Serial.println("[OLED] Page 1: textSize(1) — 6x8px, 10 chars x 6 lines");
}

// Page 2: textSize(2) — 12x16 px per char → 5 chars × 3 lines visible
void page2() {
  display.setTextSize(2);
  display.setCursor(VIS_X, VIS_Y);
  display.println("SZ 2");
  display.setCursor(VIS_X, VIS_Y + 16);
  display.println("12x16");
  display.setCursor(VIS_X, VIS_Y + 32);
  display.println("ABCDE");
  Serial.println("[OLED] Page 2: textSize(2) — 12x16px, 5 chars x 3 lines");
}

// Page 3: mixed sizes — size 2 header + size 1 body
void page3() {
  display.setTextSize(2);
  display.setCursor(VIS_X, VIS_Y);
  display.println("MOTOR");
  display.setTextSize(1);
  display.setCursor(VIS_X, VIS_Y + 16);
  display.println("Spd: 830Hz");
  display.setCursor(VIS_X, VIS_Y + 24);
  display.println("Cur: 1.2 A");
  display.setCursor(VIS_X, VIS_Y + 32);
  display.println("Dir: CW");
  display.setCursor(VIS_X, VIS_Y + 40);
  display.println("Volt: 24.1");
  Serial.println("[OLED] Page 3: mixed — size 2 header + size 1 body");
}

// Page 4: size 1, realistic status display
void page4() {
  display.setTextSize(1);
  display.setCursor(VIS_X, VIS_Y);
  display.println("= STATUS =");
  display.setCursor(VIS_X, VIS_Y + 8);
  display.println("M1:CW  830");
  display.setCursor(VIS_X, VIS_Y + 16);
  display.println("M2:CCW 450");
  display.setCursor(VIS_X, VIS_Y + 24);
  display.println("I: 1.2A");
  display.setCursor(VIS_X, VIS_Y + 32);
  display.println("V: 24.1V");
  display.setCursor(VIS_X, VIS_Y + 40);
  display.println("WiFi:OK");
  Serial.println("[OLED] Page 4: size 1, 6-line status layout");
}

// Page 5: size 1, max fill — all 60 character slots
void page5() {
  display.setTextSize(1);
  display.setCursor(VIS_X, VIS_Y);
  display.print("ABCDEFGHIJ");
  display.setCursor(VIS_X, VIS_Y + 8);
  display.print("KLMNOPQRST");
  display.setCursor(VIS_X, VIS_Y + 16);
  display.print("UVWXYZ0123");
  display.setCursor(VIS_X, VIS_Y + 24);
  display.print("4567890abc");
  display.setCursor(VIS_X, VIS_Y + 32);
  display.print("defghijklm");
  display.setCursor(VIS_X, VIS_Y + 40);
  display.print("nopqrstuvw");
  Serial.println("[OLED] Page 5: size 1, max fill — 60 chars (10x6)");
}

// Page 6: textSize(3) — 18x24 px per char → 3 chars × 2 lines visible
void page6() {
  display.setTextSize(3);
  display.setCursor(VIS_X, VIS_Y);
  display.println("CW");
  display.setCursor(VIS_X, VIS_Y + 24);
  display.println("1.2");
  Serial.println("[OLED] Page 6: textSize(3) — 18x24px, 3 chars x 2 lines");
}

// Page 7: size 2 numbers — for large readouts
void page7() {
  display.setTextSize(2);
  display.setCursor(VIS_X, VIS_Y);
  display.println("1.2A");
  display.setTextSize(1);
  display.setCursor(VIS_X, VIS_Y + 16);
  display.println("Motor Cur.");
  display.setCursor(VIS_X, VIS_Y + 24);
  display.println("830 Hz");
  display.setCursor(VIS_X, VIS_Y + 32);
  display.println("Dir: CW");
  display.setCursor(VIS_X, VIS_Y + 40);
  display.println("24.1V OK");
  Serial.println("[OLED] Page 7: size 2 number + size 1 labels");
}

typedef void (*PageFunc)();
PageFunc pages[] = { page1, page2, page3, page4, page5, page6, page7 };
const int NUM_PAGES = sizeof(pages) / sizeof(pages[0]);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[OLED] Starting OLED text-size test");

  // Configure unused motor-driver pins as low outputs
  pinMode(D10, OUTPUT); digitalWrite(D10, LOW);
  pinMode(D9, OUTPUT);  digitalWrite(D9, LOW);

  // Initialise I2C on the correct pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialise the SSD1306 display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] SSD1306 init failed!");
    while (true) { delay(1000); }
  }
  Serial.println("[OLED] SSD1306 init OK");
  Serial.printf("[OLED] Screen: %dx%d, %d pages, %d ms each\n", SCREEN_WIDTH, SCREEN_HEIGHT, NUM_PAGES, PAGE_DURATION);
}

void loop() {
  for (int i = 0; i < NUM_PAGES; i++) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    Serial.printf("[OLED] Showing page %d/%d\n", i + 1, NUM_PAGES);
    pages[i]();
    display.display();
    delay(PAGE_DURATION);
  }
}
