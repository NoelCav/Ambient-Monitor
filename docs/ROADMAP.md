# Future goals

Not scheduled. Hardware items are in HARDWARE.md.

## Data
- **Event detection:** find and label PM spikes (cooking, sweeping) after the fact, on the dashboard rather than the device.

## Smart-home integration
- **Home Assistant over MQTT** (local, no cloud). Every reading appears as a sensor; Home Assistant can bridge to Google Home.
- **Matter** (Google Home, Apple Home, Alexa, SmartThings without Home Assistant). The Arduino core has temperature/humidity/pressure sensor types; PM2.5 support and flash cost need checking.

## Firmware
- **Updates from GitHub Releases:** the device checks for and installs a newer build itself.
- **Portal password** for write actions (`/cmd`), if the network ever isn't trusted.
