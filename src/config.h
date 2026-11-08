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

#define WIFI_MAX 5  // saved WiFi networks

#ifndef DEVICE_ID
#define DEVICE_ID "esp32-ambient-01"
#endif

// --- Pins ---
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

// Test mode default: PMS stays powered and [PMS]/[ENV] diagnostics print every
// 5 s. Toggled at runtime (portal switch, `test on|off`); this only seeds NVS.
#ifndef DEFAULT_TEST_MODE
#define DEFAULT_TEST_MODE true
#endif

// --- Timing and measurement ---
#ifndef PMS_WARMUP_MS
#define PMS_WARMUP_MS 90000  // dust sensor on this long before each sample (datasheet: stable after 30 s)
#endif
#define PMS_AVG_FRAMES 10        // dust readings average the last 10 frames (~10 s)
#define LIVE_INTERVAL_MS 5000    // test reading (and [TEST] console line) period
#define ADC_SAMPLE_HZ 20000      // continuous ADC, shared by sound and light (10 kHz each)
#define SOUND_WINDOW_MS 125      // sound RMS window ("fast" sound-meter time constant)
#define WATCHDOG_S 30            // a task silent this long reboots the device
#define HEALTHY_AFTER_S 60       // new firmware is marked good (no rollback) after this long healthy

// --- Persistent data log (LittleFS, survives firmware uploads) ---
// log.csv is archived to saved/archive-full-<time>.csv at this size. ~110 bytes
// per row: 512 KB ~= 4700 rows ~= 33 days at the 10-min default.
#ifndef LOG_MAX_BYTES
#define LOG_MAX_BYTES (512 * 1024)
#endif

// --- Network console (recent serial output, served over WiFi) ---
#ifndef CONSOLE_BUF_BYTES
#define CONSOLE_BUF_BYTES (16 * 1024)  // backlog kept in RAM for /console.txt and TCP port 23
#endif
#ifndef CONSOLE_TCP_PORT
#define CONSOLE_TCP_PORT 23
#endif

// --- AP fallback (used when the device can't join the configured WiFi) ---
#ifndef AP_SSID
#define AP_SSID "AmbientMonitor-setup"
#endif
// AP_PASSWORD comes from secrets.h so the published repo doesn't reveal it
#ifndef AP_PASSWORD
#error "Define AP_PASSWORD (min 8 chars) in src/secrets.h"
#endif

// --- OTA firmware updates (ArduinoOTA on port 3232) ---
// Defaults to the setup-AP password: anyone holding it can already reconfigure
// the device. Override in secrets.h to use a separate one.
#ifndef OTA_PASSWORD
#define OTA_PASSWORD AP_PASSWORD
#endif
