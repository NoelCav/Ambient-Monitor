# Ambient Monitor — ESP32-S3

## Project overview
Environmental monitoring firmware for an ESP32-S3 DevKit C1 (N16R8 clone, CH343 USB-serial). Reads BME280 (temp/humidity/pressure), PMS5003 (particulate matter), KY-037 (sound) and a photoresistor (light). Logs to a persistent on-device CSV and is debugged and updated entirely over WiFi (web portal, network console, OTA). Uploads to a Google Sheet (Apps Script web app, `cloud/`), and a dashboard for all monitors is served by this repo's GitHub Pages (`web/index.html`, https://noelcav.github.io/Ambient-Monitor/). The earlier Firestore backend was removed on 2026-11-08 (in git history).

## Build / flash
- **PlatformIO**, Arduino framework (core 3.x), board `esp32-s3-devkitc-1`. CLI at `~/.platformio/penv/Scripts/pio.exe` on the user's desktop.
- `pio run` builds. `pio run -t upload --upload-port COM3` flashes over USB. Over WiFi (both need `secrets.ini`, see Credentials):
  - **`python scripts/ota.py`** builds, finds every monitor on the network by its mDNS `_arduino._tcp` advertisement (TXT: `device_id`, `build`), lists them and updates the ones you pick (`--list`, `--all`, `--no-build`). Standard library only: the `zeroconf` package's compiled DLLs are blocked by Windows Application Control on this PC.
  - `pio run -e ota -t upload` flashes `esp32-ambient-01.local` by name (kept as a fallback).
- Pins and runtime defaults live in `src/config.h`. `platformio.ini` holds board/toolchain config and pins the partition table (`default_8MB.csv`); don't change it, because the data log lives on its `spiffs` partition.
- On this board, opening the USB serial port can reset the ESP32. Use the network console instead when possible.

## Architecture (FreeRTOS on Arduino)
Tasks, all on core 1 (WiFi runs on core 0). Each sensor task owns its hardware behind one mutex; other code only calls its functions. All are watched by the task watchdog (`WATCHDOG_S`, 30 s): a hung task reboots with reset reason `task_wdt`. The loop task leaves the watchdog during an OTA upload.

| Task | Prio | Module | Owns |
|------|------|--------|------|
| soundlight | 5 | `soundlight.cpp` | ADC1 continuous DMA (sound GPIO4 + light GPIO6, 10 kHz each) |
| dust | 4 | `dust.cpp` | PMS5003 UART (Serial1) + SET power pin |
| sampler | 3 | `sampler.cpp` | schedule, dust power requests, readings, data log appends |
| upload | 1 | `upload.cpp` | HTTPS uploads of new log rows to the Google Sheet (leaves the watchdog during a request) |
| led | 2 | `led.cpp` | status RGB LED (GPIO 48): air-quality colour, room-light dimming, event patterns |
| loopTask | 1 | `main.cpp` loop() | WiFi, OTA, web portal, console, commands |

The BME280 (`climate.cpp`) has no task: forced reads take ~10 ms and are mutex-guarded, so any task may call `climateRead()`.

**One `Reading` type** (`reading.h`) is built by one function (`sampler.cpp` takeReading) for both the logged sample (span = sample interval) and the test reading (span = last 5 s, `LIVE_INTERVAL_MS`). It is serialized once each for the log row (`data_log.cpp`), status JSON (`status.cpp` readingJson) and console text (`sampler.cpp` readingText), so the portal's "Last sample" and "Test readings" always measure the same way.

## Key files
| File | Purpose |
|------|---------|
| `src/main.cpp` | Wiring only: setup() starts modules and tasks; loop() runs network services |
| `src/tasks.h/.cpp` | `startTask` (pinned, watchdog-subscribed), `taskFeed`, `watchdogBegin`, stack report, `Guard` mutex lock |
| `src/reading.h` | `Climate` and `Reading` structs |
| `src/sampler.h/.cpp` | Sampler task: schedule, `takeReading`, `readingText`, `samplerState()` snapshot |
| `src/climate.h/.cpp` | BME280: forced-mode read with reset-value check, boot diagnostics, `diag climate`, I2C scan |
| `src/dust.h/.cpp` | Dust task: frame decode, 10-frame average, power, frame counters |
| `src/soundlight.h/.cpp` | Sound/light task: continuous ADC, 125 ms windows, per-span stats |
| `src/led.h/.cpp` | Status LED task (colour, dimming and patterns documented in led.h) |
| `src/status.h/.cpp` | `/status.json` (shape documented in status.h) |
| `src/data_log.h/.cpp` | Persistent CSV log, saved logs, archives (mutex-guarded) |
| `src/commands.h/.cpp` | Console commands (help text is the reference) |
| `src/config_portal.h/.cpp` | Web portal server: the page, /common.*, status/log/wifi JSON, downloads, /cmd |
| `web/device.html` | The portal page (embedded into the firmware at build time by `scripts/embed_web.py` -> `src/web_assets.h`, generated) |
| `web/common.css`, `web/common.js` | Shared by the portal and the dashboard: tiles, colour scales, lux/ALT-CF3 helpers, sections, multi-monitor charts |
| `web/index.html`, `web/config.js` | Online dashboard (GitHub Pages via `.github/workflows/pages.yml`); `config.js` holds the Apps Script URL |
| `src/upload.h/.cpp` | Upload task: sends new log rows to the Sheet |
| `cloud/apps_script/Code.gs`, `cloud/README.md` | The Sheet's web app (append, JSON reads, nightly hourly rollup) and its setup steps |
| `src/console.h/.cpp` | `Console` (a `Print`): Serial + 16 KB RAM backlog for `/console.txt` and TCP port 23 |
| `src/config.h` | Pins, defaults, timing/measurement constants, log/console sizes, passwords |
| `src/params.h/.cpp`, `src/health.h/.cpp`, `src/wifi_manager.h/.cpp` | NVS params, reset reason, WiFi/NTP/mDNS/OTA |
| `lib/ambient/` | Pure C++ (no Arduino): PMS frame parser, `Window`/`SoundLightStats`/`FrameAverage`, ALT-CF3 + AQI bands (`air_quality.h`), console level words (`levels.h`), LDR lux (`light.h`), argument splitting (`args.h`) |
| `docs/HARDWARE.md`, `docs/ROADMAP.md` | Hardware changes/enclosure/calibration to do; future goals |
| `test/test_core/` | Host unit tests for `lib/ambient`: `pio test -e native` |

## Testing
- `pio test -e native` runs the host unit tests with PlatformIO's bundled MinGW toolchain (`windows_x86` platform). A separately installed WinLibs GCC is blocked by Windows Application Control on this PC; don't rely on it.
- Hardware checks go over WiFi: `curl "http://esp32-ambient-01.local/cmd?c=status"` (any console command), `/status.json`, `/console.txt`.

## Runtime params (web portal)
- Edit at `http://esp32-ambient-01.local` (mDNS name = sanitized `device_id`) or the device IP. If no saved network is in range, the device hosts SoftAP `AmbientMonitor-setup` with the same portal at 192.168.4.1. The Settings form (device ID, sample interval) saves to NVS and reboots; WiFi changes, test mode and `set sample` apply immediately.
- **WiFi:** up to `WIFI_MAX` (5) saved networks (NVS `wifi_n`, `wifi_s<i>`/`wifi_p<i>`; the old single `wifi_ssid`/`wifi_pass` migrates in). `WiFiMulti` joins the strongest in range; offline, the setup AP turns on and a rescan runs every 2 min; the AP turns off once reconnected. Manage with `wifi`, `wifi add "<ssid>" [pass]`, `wifi rm <ssid>` or the portal's Settings (passwords are masked in the console echo; the portal POSTs commands).
- Params: `device_id`, the WiFi list, `sample_interval_s` (1–86400, default 600), **test mode** (`Params::testMode`, default on, `DEFAULT_TEST_MODE`; stored under the older NVS key `live_dbg`).
- Schedule: the dust sensor wakes `PMS_WARMUP_MS` (45 s) before each sample and sleeps right after it. The first sample comes once it has warmed up after boot. `sample` (or the portal's Sample now) wakes it and samples once warm; the interval restarts from there.
- Test mode keeps the dust sensor on and prints `[TEST]` and `[DUST]` lines every 5 s. The test reading itself is computed every 5 s in both modes (portal fallback before the first sample, `read` command).

## Remote debugging (no USB needed)
- **Console:** all output goes through `Console.print*` (never `Serial.print*` directly). Read it in the portal's Console section, at `/console.txt`, or on raw TCP port 23 (backlog, then live). Every portal action runs a console command through `/cmd?c=`, so the page and the console can't diverge. Console writes never touch the network: TCP clients are fed from the backlog by `consoleLoop()` using non-blocking `send()`, so a stuck client can't stall the loop (`WiFiClient::write` can block ~10 s).
- **OTA:** ArduinoOTA on port 3232, started in `wifiSetup()` and serviced in `wifiLoop()`. It uses our mDNS responder (ArduinoOTA's own is disabled) and also works over the SoftAP.
- **Firmware rollback:** after an OTA update the new build runs "on trial"; `healthLoop()` (health.cpp) marks it good after `HEALTHY_AFTER_S` (60 s) with a reading taken and the portal reachable. A crash, watchdog reset or reboot before that makes the bootloader return to the previous build. `verifyRollbackLater()` is overridden so the core doesn't mark it good at startup. `status` and Device details show the state.
- **Watchdog self-test:** `diag watchdog` hangs the sampler; expect a reboot with reset reason `task_wdt` within 30 s.
- **Reset reason** prints at boot, appears in `status.json` and is a column in the data log. A `brownout` or `task_wdt` there explains an unexplained gap.

## Data log (on-device)
- `dataLogAppend()` writes one CSV row per sample to `/log.csv` on LittleFS. Columns: epoch (empty before NTP), uptime_s, boot (NVS counter, namespace `datalog`), reset, device_id, temp/humidity/pressure, PM (atmospheric + CF=1, 10-frame averages), pms_awake_s, particle counts, sound_avg_rms, sound_peak_rms, light (mean over the interval). Invalid sensors leave their columns empty.
- It **survives firmware uploads** (USB or OTA only write the app partition). It is lost on `pio run -t uploadfs`, a chip erase, or a partition-table change.
- When it reaches `LOG_MAX_BYTES` (512 KB), or at boot when the stored header differs from the current columns, it is **archived** to `saved/archive-<full|columns|migrated>-<first-row time, UTC>.csv` (reason in the name) and a new log starts, so each file has one layout. Archives sit in the saved-log list with user saves; if free space drops below one log + 32 KB, the oldest `archive-*` files are deleted (user-named saves never are). Older firmware's `log.old.csv` is migrated into an archive at boot.
- PM2.5: the log keeps raw atmospheric `pm2_5` (trend data) plus `pm2_5_cf1` (for the EPA PurpleAir correction, 0.524*CF1 - 0.0862*RH + 5.75). The health estimate is ALT-CF3 (Wallace 2022, validated indoors), computed from the particle counts in `lib/ambient/air_quality.h` (status JSON, console) and in the portal's graph JS (`est()`); it is not logged, to save log space: 3*(0.00030418*N0.3-0.5 + 0.0018512*N0.5-1 + 0.02069706*N1-2.5).
- Portal: Data log lists the current log and saved logs with download / save copy (whole or range) / rename / graph / delete. Graphs section plots any log or all logs combined (deduplicated by epoch). Endpoints: `GET /log.csv`, `/saved.csv?name=`, `/logs.json`; changes (save, rename, delete, clear) go through `/cmd`.

## Sensors — facts and findings
- **PMS5003** (Plantower manual v2.3): VCC 4.5–5.5 V (fan needs 5 V), ~100 mA; UART/SET 3.3 V TTL; SET high = run, low = sleep; stable data ≥30 s after wake; frame = 32 bytes, Data1–3 CF=1, Data4–6 atmospheric (used), Data7–12 particle counts per 0.1 L. Weak at coarse (>~5 µm) particles: counts >5/>10 µm stay ~0 and PM10 ≈ PM2.5, so visible sunbeam dust barely registers.
- Bench results (2026-11-08, PMS at ~4.5 V): two units agree at PM2.5 ≈ 57 µg/m³ once settled. A unit fresh from storage read ~245 and took ~30+ min to clear its own dust, which matches the old "it gets better after running for minutes" impression. Both units send frames every ~1048 ms (not the datasheet's 2.3 s / 200–800 ms), and the >0.3 µm count ≈ the >0.5 µm count. Likely normal for this batch.
- Warm-up test (2026-11-08): after 8 min of standby the sensor read steady within 10-30 s of waking, and 90 s samples sat on the same curve as continuous readings over the following 40 min, so `PMS_WARMUP_MS` was cut from 90 to 45 s (halves laser on-time). The "15-30 min to settle after a reboot" impression was the room air clearing after activity (PM2.5 halved about every 10 min that morning), not the sensor. A unit fresh from long storage is different: it needs 30+ min.
- The first parser (834ae40) never decoded a frame, which made early "dead sensor" impressions a software bug. Don't name a helper `word`: Arduino.h `#define`s `word(...)`.
- **BME280:** runs in forced mode with x1 oversampling, which avoids self-heating. `climateRead()` rejects reads still at the 0x800000 reset value, because the Adafruit lib turns those into a plausible-looking fake 23.52 °C / 82.4 % / 699 hPa. Boot prints the chip ID, calibration words and a raw forced conversion (`diag climate` repeats them). BME #1 had a cracked GND joint (resoldered, works). BME #2 is press-fit and loses contact, so it needs solder.
- **KY-037:** AO is a DC bias plus the audio signal. Sampled continuously (DMA, 10 kHz) and measured as RMS about each 125 ms window's own mean. A sample logs `sound_avg_rms` (RMS of all sound in the interval: steady noise like a TV) and `sound_peak_rms` (loudest 125 ms window: bangs). Quiet room ≈ 2-5. Portal colour thresholds (`SQ` in config_portal.cpp: 4 / 15 / 60) are guesses until a TV/clap calibration. Runs on 3.3 V through a voltage divider (not in README) that only shrinks the signal.
- **Light:** photoresistor divider on GPIO 6 (3.3 V - LDR - GPIO 6 - 10 kOhm - GND); raw >= 4090 = maxed out (bright overhead light). The log keeps the raw mean; console, portal and graphs show approximate lux (`light.h`, GL5528-type curve, gamma 0.6). `light cal <lux>` (one-point, from a phone light meter) sets `ldrR10` in NVS; until then lux is order-of-magnitude.
- README wiring lists PMS RX/TX swapped from a port test. Firmware uses GPIO17 as ESP RX, so PMS TX goes to GPIO17.

## Planned next
- Calibrate the sound colour thresholds with a TV and a clap; consider removing the KY-037 voltage divider.
- RTOS restructure done 2026-11-08 (branch `rtos`). Not done: a separate logger task (the sampler appends directly; writes are small).

## Credentials
- `src/secrets.h` (gitignored): `WIFI_SSID`, `WIFI_PASSWORD`, `AP_PASSWORD`, optional `OTA_PASSWORD` (defaults to `AP_PASSWORD`). Template: `src/secrets.h.example`.
- `secrets.ini` (gitignored, repo root): `[ota] password = …` for `pio run -e ota`, which must match `OTA_PASSWORD`. Template: `secrets.ini.example`.

## Conventions
- Sensor work belongs in its task; loop() is network-only. Never call `analogRead()` on ADC1 pins (the continuous ADC owns ADC1).
- Log through `Console`, not `Serial`.
- Level words: `lib/ambient/levels.h` (console) and the portal JS (words + colours) share the same thresholds; keep them in step.

## Dependencies
- `adafruit/Adafruit BME280 Library@^2.1.2` (everything else is in the arduino-esp32 core)
