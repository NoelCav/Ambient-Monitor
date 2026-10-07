#pragma once

// --- Secrets (from secrets.h, gitignored) ---
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef FIREBASE_DEVICE_EMAIL
#define FIREBASE_DEVICE_EMAIL ""
#endif
#ifndef FIREBASE_DEVICE_PASSWORD
#define FIREBASE_DEVICE_PASSWORD ""
#endif

// --- Firebase (public values, safe to commit) ---
#ifndef FIREBASE_API_KEY
#define FIREBASE_API_KEY "AIzaSyCh8eG5kJ47smEPUcvR7SrgJG6mdAZGx4E"
#endif
#ifndef FIREBASE_PROJECT_ID
#define FIREBASE_PROJECT_ID "ambient-monitor-f9e46"
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "esp32-ambient-01"
#endif

// --- Pins (defaults set in platformio.ini) ---
#ifndef PIN_BME_SDA
#define PIN_BME_SDA 1
#endif
#ifndef PIN_BME_SCL
#define PIN_BME_SCL 2
#endif

#ifndef PIN_KY037_A
#define PIN_KY037_A 4
#endif

#ifndef PIN_PHOTO
#define PIN_PHOTO 6
#endif

#ifndef PIN_PMS_SET
#define PIN_PMS_SET 7
#endif

#ifndef PIN_PMS_RX
#define PIN_PMS_RX 17
#endif

#ifndef PIN_PMS_TX
#define PIN_PMS_TX 18
#endif

// --- Runtime param defaults (seed values for first boot / NVS fallback) ---
// Live values are loaded from NVS and editable via the web config portal.
#ifndef DEFAULT_SAMPLE_INTERVAL_S
#define DEFAULT_SAMPLE_INTERVAL_S 600  // 10 minutes
#endif

#ifndef DEFAULT_BATCH_SIZE
#define DEFAULT_BATCH_SIZE 1  // samples collected per Firestore flush
#endif

#ifndef DEFAULT_UPLOAD_ENABLED
#define DEFAULT_UPLOAD_ENABLED false
#endif

// --- Timing (hardware constants) ---
#ifndef PMS_WARMUP_MS
#define PMS_WARMUP_MS 90000  // 90s warmup before sampling (cold-start stabilization)
#endif

// Debug: set to 1 to keep the PMS powered continuously and print the average of
// the last 5 frames every 5s (for live smoke/dust testing). Leave 0 in normal
// use — compiles out entirely when off.
#ifndef PMS_DEBUG_LIVE
#define PMS_DEBUG_LIVE 1
#endif

// --- Offline ring buffer ---
// 1008 slots = 7 days of offline storage at the 10-min default sample interval
// (1008 x ~40 bytes ~= 40 KB RAM). Coverage scales with sample interval.
#ifndef QUEUE_SIZE
#define QUEUE_SIZE 1008
#endif

// --- AP fallback (used when the device can't join the configured WiFi) ---
#ifndef AP_SSID
#define AP_SSID "AmbientMonitor-setup"
#endif
// AP_PASSWORD comes from secrets.h so the published repo doesn't reveal it
#ifndef AP_PASSWORD
#error "Define AP_PASSWORD (min 8 chars) in src/secrets.h"
#endif
