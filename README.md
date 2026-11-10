# Ambient Monitor

ESP32-S3 room monitor for temperature, humidity and pressure (BME280), particulate matter (PMS5003), sound (KY-037) and light (photoresistor).

- Logs a CSV row every sample interval (default 10 min) to on-device flash. The log survives firmware updates.
- Uploads new rows to a Google Sheet. A dashboard for all monitors is at https://www.noelcaverly.com/Ambient-Monitor/.
- Runs entirely over WiFi. Each monitor serves a web page with live readings, graphs, logs, settings and a console, accepts OTA firmware updates, and rolls back automatically if a new build fails.
- A status LED shows air quality by colour and dims with the room light.

## Repository

| Path | What |
|------|------|
| `src/` | Firmware (PlatformIO, Arduino on FreeRTOS) |
| `lib/ambient/` | Hardware-free logic (frame parser, statistics, air-quality maths), unit-tested in `test/` |
| `web/` | The monitor's own page (`device.html`, built into the firmware) and the online dashboard (`index.html`) |
| `cloud/` | Google Sheet web app and its setup steps |
| `scripts/` | `ota.py` (finds and updates monitors on the network), build helpers |
| `docs/` | Hardware plans, calibration, future goals |
| `CLAUDE.md` | Developer notes: architecture, data log, sensor details |

## Setup

1. Copy `src/secrets.h.example` to `src/secrets.h` and fill in your WiFi and a setup password. For OTA, copy `secrets.ini.example` to `secrets.ini` with the same password.
2. Flash over USB:
   ```text
   pio run -t upload --upload-port COM3
   ```
3. Open `http://esp32-ambient-01.local` (or `http://<name>.local` once named). If the monitor can't join a saved network, it hosts the WiFi `AmbientMonitor-setup`; join it and open http://192.168.4.1 to add your network.
4. To upload to the Google Sheet, follow `cloud/README.md`.

Later updates go over WiFi, either from the monitor's page (Settings → Firmware update, with `.pio/build/esp32s3/firmware.bin` and the OTA password) or from the command line:

```text
python scripts/ota.py            # build, list monitors, pick which to update
python scripts/ota.py --all      # update every monitor found
pio run -e ota -t upload         # update esp32-ambient-01.local by name
pio test -e native               # host unit tests for lib/ambient
```

## Using a monitor

Everything on the page runs a console command, and the console works in three places: the page's Console section, `nc <monitor>.local 23`, or `curl "http://<monitor>.local/cmd?c=<command>"`. `help` lists every command. The common ones:

| Command | Does |
|---------|------|
| `status` | Uptime, WiFi, sensors, next sample, log size |
| `read [climate\|dust\|sound\|light]` | Latest 5 s reading (not logged) |
| `sample` | Take a logged sample now |
| `set sample <s>` | Sample interval in seconds |
| `set name <name>` | Display name and `.local` address |
| `test on\|off` | Keep the dust sensor on and print readings every 5 s |
| `wifi`, `wifi add "<ssid>" [pass]`, `wifi rm <ssid>` | Saved networks (up to 5) |
| `log`, `log tail`, `log save <name> [range]` | Log size, last rows, keep a copy of all, the last N rows, `30m`, `8h` or `since <epoch>` (the page's Save copy takes a clock time and shows when the first row is from). Also `list`, `mv`, `rm`, `clear` |
| `upload` | Upload status (also `on\|off`, `now`, `url`, `key`, `resend`) |
| `light cal <lux>` | Calibrate the light sensor |
| `reboot` | Restart |

## Wiring

ESP32-S3 DevKitC-1 (N16R8). Pins are set in `src/config.h`.

| Part | Pin | ESP32-S3 |
|------|-----|----------|
| BME280 | VCC / GND | 3.3 V / GND |
| | SDA / SCL | GPIO 1 / GPIO 2 |
| | SDO | GND (I2C address 0x76) |
| PMS5003 | VCC / GND | 5 V (the fan needs it) / GND |
| | TX | GPIO 17 (ESP32 receives) |
| | RX | GPIO 18 |
| | SET | GPIO 7 (high = run, low = sleep) |
| KY-037 | VCC / GND | 3.3 V / GND |
| | AO | GPIO 4 |
| Photoresistor | | 3.3 V - photoresistor - GPIO 6 - 2 kΩ - GND |
| Status LED | | GPIO 48 (on-board RGB) |

Unconnected: BME280 CSB, KY-037 DO, PMS5003 RESET.

## License

See `LICENSE`.
