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
void setupLedFlash(int pin);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // --- Camera sensor configuration ---
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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_LATEST;   // Always get the newest frame
  config.fb_location  = CAMERA_FB_IN_PSRAM;   // XIAO ESP32S3 has 8MB PSRAM
  config.jpeg_quality = 10;                    // 0-63, lower = better quality
  config.fb_count     = 2;                     // Double-buffer for smooth streaming
  config.frame_size   = FRAMESIZE_QVGA;        // 320x240 — good balance of speed/detail

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // LED flash
  setupLedFlash(LED_GPIO_NUM);

  // WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // mDNS
  if (MDNS.begin(mdns_hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS started: http://%s.local\n", mdns_hostname);
  } else {
    Serial.println("mDNS failed to start");
  }

  // HTTP server
  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.print("' or 'http://");
  Serial.print(mdns_hostname);
  Serial.println(".local' to connect");
}

void loop() {
  delay(10000);
}