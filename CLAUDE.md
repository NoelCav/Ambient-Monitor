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
| `src/data_log.h/.cpp` | Persistent CSV log on LittleFS (one row per sample), served by the portal |
| `lib/PMS5003/` | Custom PMS5003 UART parser (non-blocking, SET pin power control) |
| `src/secrets.h` | WiFi + Firebase device credentials (gitignored) |
| `src/secrets.h.example` | Template for `secrets.h` |

## Runtime params (web config portal)
- Stored in NVS (`Preferences` namespace `ambient`); loaded once at boot, **applied on reboot**.
- Edit by browsing to `http://<device_id>.local` (mDNS, advertised once on WiFi) or the device IP (printed at boot as a fallback). If WiFi can't be joined, the device hosts a SoftAP `AmbientMonitor-setup` (`AP_SSID` in `config.h`, `AP_PASSWORD` in `secrets.h`) serving the same form.
- The portal root also shows a live, auto-refreshing reading panel (env/sound/light/PM with levels), backed by `GET /status.json`.
- Params: `device_id`, `wifi_ssid`/`wifi_password`, `upload_enabled`, `sample_interval_s`, `batch_size`. Defaults come from `DEFAULT_*` in `config.h` and `WIFI_SSID`/`WIFI_PASSWORD` in `secrets.h`.
- `upload_enabled=false` → serial-only mode: WiFi/portal stay active, but readings are neither queued nor sent to Firestore.

## Data flow
1. Sensors sampled every `sample_interval_s` (runtime param, default 10 min)
2. Readings populate a `SensorReading` struct
3. `printReading()` outputs to Serial
4. If `upload_enabled`, `queueReading()` pushes **every** sample into a `QUEUE_SIZE`-slot ring buffer (`config.h`, default 1008 = ~7 days offline at 10-min intervals)
5. A flush opens once `batch_size` samples have accumulated; `firestoreLoop()` then drains the queue to the `raw` collection non-blockingly (5 per loop) until empty. Effective upload interval = `sample_interval_s × batch_size`.
6. If upload fails, reading stays in queue and is retried on the next flush. If buffer overflows, oldest reading is dropped.
7. Sensor dropouts degrade gracefully: invalid BME/PMS reads set `*_valid=false` (BME re-inits on the next sample); no crash or data interruption.

## Data log (on-device)
- `dataLogAppend()` writes one CSV row per sample to `/log.csv` on the LittleFS `spiffs` partition. It always logs, whatever `upload_enabled` is set to. Columns: epoch (empty before NTP), uptime_s, boot (NVS counter, namespace `datalog`), device_id, BME, PM + particle counts, sound avg/peak/p2p/p2p_max, light. Invalid sensors leave their columns empty.
- It **survives firmware uploads** because `upload` only writes the app partition. It is lost on `pio run -t uploadfs`, a chip erase, or a partition-table change, which is why `platformio.ini` pins `board_build.partitions = default_8MB.csv`.
- It rotates to `/log.old.csv` at `LOG_MAX_BYTES` (512 KB, `config.h`), and also at boot when the stored header differs from the current columns, so each file has exactly one layout. Don't append rows under a mismatched header.
- Portal: `GET /log.csv` and `/log.old.csv` download as `<device_id>-log.csv`; `POST /log/clear` deletes both.
- Sound peak-hold: `sound_p2p_max` is the loudest 1 s block since the previous sample. The live `[ENV]` line shows `max5s`, the loudest 1 s block since the previous print.

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
- `PMS_DEBUG_LIVE` (`config.h`): keeps the sensor powered continuously and prints two lines every 5 s. `[PMS t+Ns]` gives seconds since wake, atmospheric and CF=1 PM, particle counts (Data7–12), ms between frames and frames/checksum failures in the window. `[ENV]` gives live BME temp/RH/pressure, sound avg and peak-to-peak, and light. **Currently committed as `1`**, so duty-cycling is disabled.
- At boot, `dumpBmeCalibration()` prints the BME chip ID and the dig_T1–T3 calibration words, to diagnose corrupted I2C reads.
- On this board (CH343 USB-serial, COM3 on the user's desktop), opening the serial port can reset the ESP32.
- Datasheet facts (Plantower PMS5003 manual v2.3): VCC 4.5–5.5 V (fan needs 5 V), ~100 mA active; UART/SET/RESET are 3.3 V TTL; SET high = run, low = sleep (internal pull-up); stable data needs **≥30 s after wake**; active-mode frames every 2.3 s (stable) or 200–800 ms (fast); total response time 10 s; MTTF 3 years. Frame = 32 bytes, Data1–3 = CF=1, Data4–6 = atmospheric (bytes 10–15, what we use). Near-zero PM in clean indoor air is normal.
- The parser in the first commit (834ae40) never decoded a frame (bad checksum range); fixed in c22e528. Readings before June 2026 saying "PMS: no data" were a software bug, not dead sensors.

## Audit notes (2026-11-04) — open issues, not yet fixed
- **PMS readings (2026-11-08, PMS powered at ~4.5 V):** clean frames (`bad=0`), PM2.5 dropped from ~190 to ~57 µg/m³ over an hour on the bedroom floor, which has heavy dust. Counts >5 µm and >10 µm stay 0, and PM10 ≈ PM2.5. The PMS5003 is weak at coarse (>~5 µm) particles, which are what you see in sunbeams. The >0.3 µm count is nearly equal to the >0.5 µm count (often ~3× in published data), and frames arrive every ~1048 ms instead of the datasheet's 2.3 s / 200–800 ms. Compare all 3 units on the same firmware.
- **Main symptom:** even after the c22e528 parser fix, the PMS reads 0 in visibly heavy dust (sweeping a garage) unless it's held next to the broom, and only improves after running for minutes. This happens on all 3 units, so the cause is probably shared (airflow, mounting, power under load or firmware) rather than three bad sensors. The fan is near-silent and only lightly moves paper at the inlet and outlet. The sensor sits in the open on its 6-inch cable, so mounting is ruled out. Next tests: an independent 5 V supply (shared GND) with the voltage measured at the sensor connector while it runs, plus a diagnostic build that logs the particle counts (Data7–12) and the time between frames.
- **Unpushed work:** the user's old laptop may hold unpushed commits from the last round of testing. Pull those before changing code.
- **Hardware state (from the user):** the board's `IN-OUT` 5 V pad is bridged and the 5 V output was checked with a meter. The KY-037 AO goes through a voltage divider that isn't in README.md. The BME is a genuine BME280. Swapping PMS RX/TX in the README was a port test and isn't the real wiring. Nothing is uploading and the cloud side isn't deployed.
- **Build:** `src/secrets.h` lives on the user's other computer; `config.h` `#error`s without `AP_PASSWORD`.
- **Timestamp bug:** `sendReading()` stamps `timestamp` with the *upload* time, not the sample time. Queued/batched/offline readings all get the flush time. `SensorReading` only stores `millis()`.
- **Sound:** KY-037 AO is a DC bias plus the audio signal, so `sound_avg` is mostly the bias, not loudness. Use peak-to-peak (max − min) over the window.
- **BME280 (2026-11-08, diagnosed):** the board in use (GY-BME/PMP280, suspected solder damage) **drops off I2C as soon as it starts a conversion**. `bmeForcedTest()` shows the ctrl_meas write landing (`25`), then the next read fails with err 4 and the chip comes back reset. ctrl_meas reads `00` (sleep) and the raw data stays at the reset values 0x800000/0x800000/0x8000. The installed Adafruit lib has no reset-value check, so it computes a constant fake **23.52 °C / 82.4 % / 699 hPa** from those registers. Every BME reading from this board is invalid. Likely cause is power or contact at the sensor (VCC/GND, header pins) or a damaged die. **Resolved:** the cause was a cracked GND solder joint on BME #1. After resoldering it reads 0x60, valid calibration and real changing values. BME #2 is press-fit and loses contact intermittently (the I2C scan comes up empty), so it needs soldering. Firmware now runs the BME in forced mode with x1 oversampling (`bmeInit()`/`bmeMeasure()`) and rejects reads still at the 0x800000 reset value.
- **BME280 (earlier):** temperature reads about **100 °C too high**. That is far beyond self-heating (about 1 °C) and points to a hardware or communication fault: bad calibration data read at `begin()`, corrupted I2C reads, or a damaged sensor. Test with the stock Adafruit `bme280test` example and dump the chip ID and calibration registers. Separately, forced mode with x1 oversampling avoids self-heating. BME #1 was hand-soldered (suspected heat damage). BME #2 is press-fit with no solder, which reads better but is still off. Press-fit header contacts are a prime suspect for corrupted I2C reads. The BME280 is factory-calibrated, so the only user calibration is an optional fixed temperature offset against a reference thermometer.
- **Planned direction:** restructure the firmware around FreeRTOS tasks and queues. The user wants to learn RTOS, so build it up incrementally.
- **README wiring** for the PMS RX/TX is a leftover test swap. Firmware uses GPIO17 as ESP **RX**, so PMS **TX** goes to GPIO17.
- **Cloud:** rollup drops `device_id` (mixes devices into one aggregate), `merge:true` re-runs overwrite earlier window averages, and the dashboard doesn't filter by `device_id`. Most Firebase setup boxes in `cloud/README.md` are unchecked, so the cloud state is unverified.

## Conventions
- Non-blocking patterns throughout — no `delay()` in loop, only `yield()`
- Sound: 1000-sample rolling buffer (1ms interval = 1s window)
- Light: 10-sample rolling buffer (100ms interval = 1s window)
- Level functions (tempLevel, soundLevel, etc.) classify readings into human-readable categories

## Dependencies
- `adafruit/Adafruit BME280 Library@^2.1.2`
- `mobizt/Firebase Arduino Client Library for ESP8266 and ESP32@^4.4.14`
