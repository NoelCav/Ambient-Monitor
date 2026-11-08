# Future goals

Not scheduled; roughly in order of interest. Hardware items are in HARDWARE.md.

## Data off the device
- **Upload to a hosted store** (free and long-lived). Candidates:
  - ThingSpeak (MathWorks, since 2010; free ~3 M messages/year).
  - A Google Sheet fed by an Apps Script web app (free, durable, simple).
  - InfluxDB Cloud / Grafana Cloud free tiers keep only short histories (about a month).

  The on-device log stays the master copy either way.
- **Website** combining monitors: merge by `device_id` and epoch, apply per-device offsets from side-by-side calibration, and show indoor vs. outdoor.
- **Long-term rollups** (from the old cloud plan): keep raw samples for a few months, then 30-min and 1-h averages, so years of history stay small.
- **Event detection on the data:** PM spikes (cooking, sweeping) found and labelled after the fact, rather than on the device.

## Smart-home integration
- **Home Assistant over MQTT** (local network, no cloud). The device announces itself and every reading appears as a sensor with history, automations and phone alerts. Home Assistant then bridges to Google Home.
- **Matter** (works with Google Home, Apple Home, Alexa and SmartThings without Home Assistant). The Arduino core has Matter temperature/humidity/pressure sensor types; PM2.5 support and the flash cost need checking.

## Firmware
- **Updates from GitHub Releases:** the device checks for and installs a newer build itself (the rollback guard already protects against a bad one).
- **Multiple monitors:**
  - Give each device a unique Device ID.
- **Portal password** for write actions (HTTP basic auth on `/cmd` and `/save`), if the network ever isn't trusted.

## Done (for reference)
- OTA upload script that finds monitors on the network (`scripts/ota.py`), FreeRTOS restructure, continuous-ADC sound (RMS), watchdog, firmware rollback, status LED, saved WiFi list, saved/archived logs, graphs, PM2.5 indoor estimate (ALT-CF3), host unit tests.
