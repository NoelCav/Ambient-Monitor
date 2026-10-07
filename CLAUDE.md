# Ambient Monitor — ESP32-S3

## Project overview
Environmental monitoring firmware for ESP32-S3 DevKit C1. Reads BME280 (temp/humidity/pressure), PMS5003 (particulate matter), KY-037 (sound), and a photoresistor (light). Outputs to Serial and uploads to Google Cloud Firestore. Operational settings are runtime params (NVS), editable via a built-in web config portal.

## Build
- **PlatformIO**, Arduino framework, board `esp32-s3-devkitc-1`
- `pio run` — build
- `pio run --target upload` — flash to device
- `pio device monitor` — serial output
- Pins and runtime defaults live in `src/config.h` (single source of truth); `platformio.ini` carries only board/toolchain config.

## Key files
| File | Purpose |
|------|---------|
| `src/main.cpp` | Setup, loop, sensor sampling, Serial output, batching |
| `src/config.h` | Pins, param defaults, ring-buffer size, AP fallback, Firebase config. Includes `secrets.h` if present |
| `src/params.h/.cpp` | Runtime params loaded from / saved to NVS (`Preferences`) with validation |
| `src/config_portal.h/.cpp` | Web (`WebServer`) config form to view/edit params; Save persists + reboots |
| `src/sensor_reading.h` | `SensorReading` struct shared across modules |
| `src/wifi_manager.h/.cpp` | Non-blocking WiFi + NTP time sync + SoftAP fallback |
| `src/firestore_upload.h/.cpp` | Ring buffer queue + Firestore upload with retry |
| `lib/PMS5003/` | Custom PMS5003 UART parser (non-blocking, SET pin power control) |
| `src/secrets.h` | WiFi + Firebase device credentials (gitignored) |
| `src/secrets.h.example` | Template for `secrets.h` |

## Runtime params (web config portal)
- Stored in NVS (`Preferences` namespace `ambient`); loaded once at boot, **applied on reboot**.
- Edit by browsing to `http://<device_id>.local` (mDNS, advertised once on WiFi) or the device IP (printed at boot as a fallback). If WiFi can't be joined, the device hosts a SoftAP `AmbientMonitor-setup` (`AP_SSID` in `config.h`, `AP_PASSWORD` in `secrets.h`) serving the same form.
- The portal root also shows a live, auto-refreshing reading panel (env/sound/light/PM with levels), backed by `GET /status.json`.
- Params: `device_id`, `wifi_ssid`/`wifi_password`, `upload_enabled`, `sample_interval_s`, `batch_size`. Defaults come from `DEFAULT_*` in `config.h` and `WIFI_SSID`/`WIFI_PASSWORD` in `secrets.h`.
- `upload_enabled=false` → serial-only mode: WiFi/portal and the offline buffer stay active, but nothing is sent to Firestore.

## Data flow
1. Sensors sampled every `sample_interval_s` (runtime param, default 10 min)
2. Readings populate a `SensorReading` struct
3. `printReading()` outputs to Serial
4. If `upload_enabled`, `queueReading()` pushes **every** sample into a `QUEUE_SIZE`-slot ring buffer (`config.h`, default 1008 = ~7 days offline at 10-min intervals)
5. A flush opens once `batch_size` samples have accumulated; `firestoreLoop()` then drains the queue to the `raw` collection non-blockingly (5 per loop) until empty. Effective upload interval = `sample_interval_s × batch_size`.
6. If upload fails, reading stays in queue and is retried on the next flush. If buffer overflows, oldest reading is dropped.
7. Sensor dropouts degrade gracefully: invalid BME/PMS reads set `*_valid=false` (BME re-inits on the next sample); no crash or data interruption.

## Firestore
- **Collection:** `raw` — one document per reading, auto-generated IDs
- **Auth:** Email/password with a dedicated device account (credentials in `secrets.h`, compile-time only)
- **`device_id`:** runtime param — identical firmware can be named per-device via the portal
- **`timestamp`:** unix epoch seconds from NTP — matches the external rollup script
- **`expireAt`:** 6 months from write time, for Firestore TTL auto-deletion
- **Rollup, rules, dashboard:** live in `cloud/` in this repo (see `cloud/README.md`); rollup is raw → agg_30m → agg_1h, run manually via `.github/workflows/rollup.yml`
- **Firebase project:** `ambient-monitor-f9e46` (API key and project ID in `config.h`)

## Credentials
- **`secrets.h`** (gitignored): `WIFI_SSID`, `WIFI_PASSWORD`, `FIREBASE_DEVICE_EMAIL`, `FIREBASE_DEVICE_PASSWORD`, `AP_PASSWORD`
- **`config.h`** (committed): `FIREBASE_API_KEY`, `FIREBASE_PROJECT_ID`, `DEVICE_ID` — these are public values

## PMS5003 power management
- Duty-cycled: powers off between samples, powers on `PMS_WARMUP_MS` (90s) before each sample so the fan/laser stabilize before sampling
- `PMS_DEBUG_LIVE` (`config.h`): keeps the sensor powered continuously and prints the average of the last 5 frames every 5s for live smoke/dust testing

## Conventions
- Non-blocking patterns throughout — no `delay()` in loop, only `yield()`
- Sound: 1000-sample rolling buffer (1ms interval = 1s window)
- Light: 10-sample rolling buffer (100ms interval = 1s window)
- Level functions (tempLevel, soundLevel, etc.) classify readings into human-readable categories

## Dependencies
- `adafruit/Adafruit BME280 Library@^2.1.2`
- `mobizt/Firebase Arduino Client Library for ESP8266 and ESP32@^4.4.14`
