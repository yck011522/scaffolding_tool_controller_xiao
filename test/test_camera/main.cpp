// Camera streaming firmware for Seeed Studio XIAO ESP32S3 Sense.
//
// On boot: initialises the OV2640 camera, connects to WiFi, starts
// mDNS (camera-one.local), and launches an HTTP server with two
// endpoints:  /capture (single JPEG)  and  /stream (MJPEG).
//
// Derived from the Espressif CameraWebServer example.

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "camera_pins.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "VictorPhone";
const char *password = "91579150";

// mDNS hostname — reachable as "camera-one.local"
const char *mdns_hostname = "camera-one";

void startCameraServer();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Configure unused motor-driver pins as low outputs so they don't float
  // and accidentally drive the motor during camera-only testing.
  pinMode(D10, OUTPUT); digitalWrite(D10, LOW);  // Motor PWM pin
  pinMode(D9, OUTPUT); digitalWrite(D9, LOW);    // Motor direction pin

  // --- Camera sensor configuration ---
  // All pins are fixed by the XIAO ESP32S3 Sense PCB layout (see camera_pins.h).
  // LEDC channel 0 drives the XCLK signal to the image sensor.
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;              // 20 MHz master clock to the sensor
  config.pixel_format = PIXFORMAT_JPEG;          // Native JPEG output (no software conversion)
  config.grab_mode    = CAMERA_GRAB_LATEST;      // Always get the newest frame, skip stale ones
  config.fb_location  = CAMERA_FB_IN_PSRAM;      // Store frame buffers in the 8 MB PSRAM
  config.jpeg_quality = 10;                       // 0-63, lower = better quality / larger file
  config.fb_count     = 2;                        // Double-buffer: one filling while one is sent
  config.frame_size   = FRAMESIZE_QVGA;           // 320x240 — good balance of speed and detail

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // On-board LED (GPIO 21) used as an activity indicator.
  // app_httpd.cpp turns it on during capture/stream and off when done.
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);

  // Connect to WiFi — blocks until connected.
  // WiFi.setSleep(false) keeps the radio active for lower-latency streaming.
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // mDNS — advertise ourselves so Python scripts can find us by hostname
  // instead of needing a hard-coded IP address.
  if (MDNS.begin(mdns_hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[MDNS] Hostname: %s.local\n", mdns_hostname);
  } else {
    Serial.println("mDNS failed to start");
  }

  // Start the HTTP server (defined in app_httpd.cpp)
  startCameraServer();

  // Print all four reachable URLs for easy copy-paste from the serial monitor
  Serial.println("Camera Ready!");
  Serial.printf("  Capture: http://%s/capture\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Stream:  http://%s/stream\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Capture: http://%s.local/capture\n", mdns_hostname);
  Serial.printf("  Stream:  http://%s.local/stream\n", mdns_hostname);
}

void loop() {
  delay(10000);
}