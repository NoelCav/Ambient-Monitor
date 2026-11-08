# Ambient Monitor — ESP32-S3

## Project overview
Environmental monitoring firmware for an ESP32-S3 DevKit C1 (N16R8 clone, CH343 USB-serial). Reads BME280 (temp/humidity/pressure), PMS5003 (particulate matter), KY-037 (sound) and a photoresistor (light). Logs to a persistent on-device CSV and is debugged and updated entirely over WiFi (web portal, network console, OTA). No cloud backend: Firestore and the `cloud/` dashboard/rollup were removed on 2026-11-08 and are in git history if needed.

## Build / flash
- **PlatformIO**, Arduino framework (core 3.x), board `esp32-s3-devkitc-1`. CLI at `~/.platformio/penv/Scripts/pio.exe` on the user's desktop.
- `pio run` builds. `pio run -t upload --upload-port COM3` flashes over USB. **`pio run -e ota -t upload`** flashes over WiFi to `esp32-ambient-01.local` (needs `secrets.ini`, see Credentials).
- Pins and runtime defaults live in `src/config.h`. `platformio.ini` holds board/toolchain config and pins the partition table (`default_8MB.csv`); don't change it, because the data log lives on its `spiffs` partition.
- On this board, opening the USB serial port can reset the ESP32. Use the network console instead when possible.

## Key files
| File | Purpose |
|------|---------|
| `src/main.cpp` | Setup, loop, sensor sampling, BME diagnostics, Serial/console output, status JSON |
| `src/config.h` | Pins, param defaults, PMS warm-up, log/console sizes, AP + OTA passwords. Includes `secrets.h` |
| `src/params.h/.cpp` | Runtime params in NVS (`Preferences` namespace `ambient`) with validation |
| `src/config_portal.h/.cpp` | Web portal: settings form, live reading panel, console page, log download |
| `src/console.h/.cpp` | `Console` (a `Print`): tees all log output to Serial plus a 16 KB RAM backlog served at `/console` and on raw TCP port 23 |
| `src/data_log.h/.cpp` | Persistent CSV log on LittleFS, one row per sample |
| `src/health.h/.cpp` | Reset-reason string (poweron / software / panic / brownout / task_wdt …) |
| `src/wifi_manager.h/.cpp` | WiFi + NTP + SoftAP fallback + mDNS + ArduinoOTA |
| `src/sensor_reading.h` | `SensorReading` struct shared across modules |
| `lib/PMS5003/` | PMS5003 UART frame parser (CF=1, atmospheric, particle counts, frame interval) + SET-pin power |

## Runtime params (web portal)
- Edit at `http://esp32-ambient-01.local` (mDNS name = sanitized `device_id`) or the device IP. If WiFi can't be joined, the device hosts SoftAP `AmbientMonitor-setup` with the same portal at 192.168.4.1. Saving persists to NVS and reboots.
- Params: `device_id`, `wifi_ssid`/`wifi_password`, `sample_interval_s` (1–86400, default 600), `live_dbg`/**Test mode** (default on, `DEFAULT_LIVE_DEBUG`).
- Test mode keeps the PMS powered and prints `[PMS t+Ns]` and `[ENV]` lines every 5 s. With it off, the PMS is duty-cycled: powered `PMS_WARMUP_MS` (90 s) before each sample, then off.
- `GET /status.json` carries the latest reading plus `sys` (uptime, reset reason, heap, RSSI, live_debug).

## Remote debugging (no USB needed)
- **Console:** all output goes through `Console.print*` (never `Serial.print*` directly). Read it at `/console` (auto-refreshing page), `/console.txt`, or raw TCP port 23 (backlog, then live). Console writes never touch the network: TCP clients are fed from the backlog by `consoleLoop()` using non-blocking `send()`, so a stuck client can't stall the loop (`WiFiClient::write` can block ~10 s).
- **OTA:** ArduinoOTA on port 3232, started in `wifiSetup()` and serviced in `wifiLoop()`. It uses our mDNS responder (ArduinoOTA's own is disabled) and also works over the SoftAP.
- **Reset reason** prints at boot, appears in `status.json` and is a column in the data log. A `brownout` or `task_wdt` there explains an unexplained gap.

## Data log (on-device)
- `dataLogAppend()` writes one CSV row per sample to `/log.csv` on LittleFS. Columns: epoch (empty before NTP), uptime_s, boot (NVS counter, namespace `datalog`), reset, device_id, BME, PM + particle counts, sound_avg_p2p/sound_peak_p2p, light. Invalid sensors leave their columns empty.
- It **survives firmware uploads** (USB or OTA only write the app partition). It is lost on `pio run -t uploadfs`, a chip erase, or a partition-table change.
- When it reaches `LOG_MAX_BYTES` (512 KB), or at boot when the stored header differs from the current columns, it is **archived** to `saved/archive-<full|columns|migrated>-<first-row time, UTC>.csv` (reason in the name) and a new log starts, so each file has one layout. Archives sit in the saved-log list with user saves; if free space drops below one log + 32 KB, the oldest `archive-*` files are deleted (user-named saves never are). Older firmware's `log.old.csv` is migrated into an archive at boot.
- PM2.5: the log keeps raw atmospheric `pm2_5` (trend data) plus `pm2_5_cf1` (for the EPA PurpleAir correction, 0.524*CF1 - 0.0862*RH + 5.75). The portal's health estimate is ALT-CF3 (Wallace 2022, validated indoors), computed from the logged particle counts: 3*(0.00030418*N0.3-0.5 + 0.0018512*N0.5-1 + 0.02069706*N1-2.5).
- Portal: Data log lists the current log and saved logs with download / save copy (whole or range) / rename / graph / delete. Graphs section plots any log or all logs combined (deduplicated by epoch). Endpoints: `GET /log.csv`, `/saved.csv?name=`, `/logs.json`; `POST /log/clear` deletes the current log only.

## Sensors — facts and findings
- **PMS5003** (Plantower manual v2.3): VCC 4.5–5.5 V (fan needs 5 V), ~100 mA; UART/SET 3.3 V TTL; SET high = run, low = sleep; stable data ≥30 s after wake; frame = 32 bytes, Data1–3 CF=1, Data4–6 atmospheric (used), Data7–12 particle counts per 0.1 L. Weak at coarse (>~5 µm) particles: counts >5/>10 µm stay ~0 and PM10 ≈ PM2.5, so visible sunbeam dust barely registers.
- Bench results (2026-11-08, PMS at ~4.5 V): two units agree at PM2.5 ≈ 57 µg/m³ once settled. A unit fresh from storage read ~245 and took ~30+ min to clear its own dust, which matches the old "it gets better after running for minutes" impression. Both units send frames every ~1048 ms (not the datasheet's 2.3 s / 200–800 ms), and the >0.3 µm count ≈ the >0.5 µm count. Likely normal for this batch.
- The first parser (834ae40) never decoded a frame, which made early "dead sensor" impressions a software bug. Don't name a helper `word`: Arduino.h `#define`s `word(...)`.
- **BME280:** runs in forced mode with x1 oversampling (`bmeInit()`/`bmeMeasure()`), which avoids self-heating. `bmeMeasure()` rejects reads still at the 0x800000 reset value, because the Adafruit lib turns those into a plausible-looking fake 23.52 °C / 82.4 % / 699 hPa. Boot prints `dumpBmeCalibration()` + `bmeForcedTest()`. BME #1 had a cracked GND joint (resoldered, works). BME #2 is press-fit and loses contact, so it needs solder.
- **KY-037:** AO is a DC bias plus the audio signal, so loudness = peak-to-peak (p2p) of each 1 s window (~1 kHz analogRead). A sample logs `sound_avg_p2p` (mean of the 1 s windows over the interval: steady noise like a TV) and `sound_peak_p2p` (loudest 1 s window: bangs); test mode shows the same over 5 s. Quiet ≈ 6–11, TV ≈ 20–40, loud clap ≈ 500+. Runs on 3.3 V through a voltage divider (not in README) that only shrinks the signal; reads stall while the loop is busy, so short sounds can be missed until continuous ADC sampling (RTOS plan).
- **Light:** photoresistor divider on GPIO 6; 4095 = saturated (bright overhead light).
- README wiring lists PMS RX/TX swapped from a port test. Firmware uses GPIO17 as ESP RX, so PMS TX goes to GPIO17.

## Planned next
FreeRTOS restructure, staying on Arduino. The user knows RTOS; skip teaching framing. Rough order: split `main.cpp` into per-sensor files + native unit tests for the PMS parser, then a PMS task, mailboxes/sampler + mutex for shared readings, logger task, continuous-ADC sound, watchdog + stack stats.
- **Sound fix (part of the RTOS work):** today the KY-037 is read with `analogRead` once per `loop()` pass (~1 kHz at best). Reads stall whenever the loop is busy (WiFi, HTTP handlers, BME reads, OTA), and 1 kHz misses short, sharp sounds, so loud events can go unrecorded. Move sound to the ESP32-S3 continuous ADC (DMA, ~8-16 kHz) in its own task. Compute per-second RMS and peak-to-peak from every sample, and keep `sound_avg_p2p` / `sound_peak_p2p` semantics (or add RMS columns). Consider removing the voltage divider: the module runs on 3.3 V, so the divider only shrinks the signal.

## Credentials
- `src/secrets.h` (gitignored): `WIFI_SSID`, `WIFI_PASSWORD`, `AP_PASSWORD`, optional `OTA_PASSWORD` (defaults to `AP_PASSWORD`). Template: `src/secrets.h.example`.
- `secrets.ini` (gitignored, repo root): `[ota] password = …` for `pio run -e ota`, which must match `OTA_PASSWORD`. Template: `secrets.ini.example`.

## Conventions
- Non-blocking loop: no long `delay()` in `loop()`. Network writes must never block sampling.
- Log through `Console`, not `Serial`.
- Level functions (`tempLevel`, `pm25Level`, …) classify readings for the serial output and portal.

## Dependencies
- `adafruit/Adafruit BME280 Library@^2.1.2` (everything else is in the arduino-esp32 core)
