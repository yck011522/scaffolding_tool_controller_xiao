// Minimal camera HTTP server for XIAO ESP32S3 Sense.
//
// Serves two endpoints on port 80:
//   /capture  — single JPEG snapshot (LED on during capture+send, then off)
//   /stream   — MJPEG stream (LED on while streaming, off when client disconnects)
//
// The camera outputs native JPEG frames, so no pixel-format conversion is
// needed.  Each MJPEG frame is sent as a separate "part" in a
// multipart/x-mixed-replace HTTP response — the browser (or Python client)
// simply receives a continuous series of JPEG images.
//
// Derived from the Espressif CameraWebServer example.
// Face detection, browser control UI, and register-twiddling
// endpoints have been removed — camera settings are hardcoded
// in main.cpp at init time.
//
// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
// Licensed under the Apache License, Version 2.0.

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include <Arduino.h>
#include "camera_pins.h"

// ---------------------------------------------------------------------------
// LED indicator (on-board LED on GPIO 21, simple digital on/off)
// Used to signal activity: lights up during capture sends and streaming.
// ---------------------------------------------------------------------------
static void led_on()  { digitalWrite(LED_GPIO_NUM, HIGH); }
static void led_off() { digitalWrite(LED_GPIO_NUM, LOW); }

// ---------------------------------------------------------------------------
// MJPEG stream constants
//
// An MJPEG stream is an HTTP response with content-type
// "multipart/x-mixed-replace".  Each frame is separated by a boundary
// string and has its own Content-Type / Content-Length sub-headers.
// ---------------------------------------------------------------------------
// Unique boundary string that separates individual JPEG frames in the stream
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
// Template for the per-frame sub-header (content type, length, timestamp)
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

static httpd_handle_t camera_httpd = NULL;

// ---------------------------------------------------------------------------
// Running-average filter for FPS logging
//
// Keeps a circular buffer of the last N frame times and computes a
// smoothed average, used only in log_i() messages when the Arduino
// log level is INFO or higher.
// ---------------------------------------------------------------------------
typedef struct {
    size_t size;
    size_t index;
    size_t count;
    int sum;
    int *values;
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
        return NULL;
    memset(filter->values, 0, sample_size * sizeof(int));
    filter->size = sample_size;
    return filter;
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static int ra_filter_run(ra_filter_t *filter, int value)
{
    if (!filter->values)
        return value;
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index = (filter->index + 1) % filter->size;
    if (filter->count < filter->size)
        filter->count++;
    return filter->sum / filter->count;
}
#endif

// ---------------------------------------------------------------------------
// /capture — single JPEG frame
//
// Grabs one frame from the camera, sends it as an HTTP response with
// content-type image/jpeg, then returns.  The LED is on for the entire
// duration (capture + network send) so the user can see activity.
// ---------------------------------------------------------------------------
static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    int64_t fr_start = esp_timer_get_time();
#endif

    led_on();                    // Turn on LED to indicate capture in progress
    fb = esp_camera_fb_get();     // Grab one JPEG frame from the camera

    if (!fb)
    {
        log_e("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");  // Allow cross-origin requests

    // Embed the camera's hardware timestamp in a custom header
    char ts[32];
    snprintf(ts, 32, "%ld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
    httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    size_t fb_len = fb->len;
#endif
    // Send the raw JPEG buffer directly — no conversion needed
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);     // Return the frame buffer to the camera driver
    led_off();                    // Capture+send complete, turn LED off

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    int64_t fr_end = esp_timer_get_time();
#endif
    log_i("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
    return res;
}

// ---------------------------------------------------------------------------
// /stream — MJPEG stream
//
// Continuously grabs frames and sends them as an MJPEG multipart response.
// The LED stays on for the entire duration of the stream.  The loop exits
// when a send fails (typically because the client disconnected), at which
// point the LED is turned off.
// ---------------------------------------------------------------------------
static esp_err_t stream_handler(httpd_req_t *req)
{
    Serial.println("[STREAM] stream_handler entered");
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t *jpg_buf = NULL;
    char part_buf[128];

    static int64_t last_frame = 0;
    if (!last_frame)
        last_frame = esp_timer_get_time();

    // Set the multipart content type so the client knows to expect a stream
    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
        return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");  // Allow cross-origin requests
    httpd_resp_set_hdr(req, "X-Framerate", "60");                 // Hint to client

    led_on();  // LED stays on for the entire stream session

    while (true)
    {
        fb = esp_camera_fb_get();   // Grab the latest JPEG frame
        if (!fb)
        {
            log_e("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            _timestamp.tv_sec = fb->timestamp.tv_sec;
            _timestamp.tv_usec = fb->timestamp.tv_usec;
            jpg_buf_len = fb->len;
            jpg_buf = fb->buf;
        }

        // Send the three parts of each MJPEG frame:
        //   1. Boundary string (separates this frame from the previous one)
        //   2. Per-frame sub-header (content type, length, timestamp)
        //   3. The raw JPEG data
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK)
        {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART,
                                   jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);

        if (fb)
        {
            esp_camera_fb_return(fb);  // Return frame buffer to the camera driver
            fb = NULL;
            jpg_buf = NULL;
        }

        // A failed send usually means the client closed the connection
        if (res != ESP_OK)
        {
            Serial.printf("[STREAM] send failed, res=%d — client likely disconnected\n", res);
            break;
        }

        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        frame_time /= 1000;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
#endif
        log_i("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)",
              (uint32_t)(jpg_buf_len),
              (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
              avg_frame_time, 1000.0 / avg_frame_time);
        last_frame = fr_end;
    }

    Serial.println("[STREAM] stream ended");
    led_off();  // Client disconnected, turn LED off

    return res;
}

// ---------------------------------------------------------------------------
// Server startup
//
// Creates an HTTP server on port 80 with two URI handlers.
// - lru_purge_enable: automatically closes the oldest idle connection when
//   the server runs out of slots, preventing lockups.
// - send/recv_wait_timeout: 3 seconds — quickly detects dead clients so
//   the stream handler loop can exit instead of blocking forever.
// - stack_size: 32 KB — the stream handler's while-loop with per-frame
//   logging needs more stack than the default 4 KB.
// ---------------------------------------------------------------------------
void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size = 32768;         // Stream handler needs extra stack
    config.lru_purge_enable = true;    // Purge oldest connection when full
    config.send_wait_timeout = 3;      // Detect dead clients faster (seconds)
    config.recv_wait_timeout = 3;

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    ra_filter_init(&ra_filter, 20);

    Serial.printf("[SERVER] Starting camera server on port %d\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &stream_uri);
        Serial.println("[SERVER] Registered /capture and /stream");
    }
    else
    {
        Serial.println("[SERVER] Failed to start!");
    }
}


