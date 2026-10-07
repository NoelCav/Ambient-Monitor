# Ambient Monitor - ESP32 Firmware

This folder contains a minimal ESP32 (S3) firmware written for PlatformIO / Arduino framework.

Features implemented:
- Reads BME280 (I2C at 0x76) for temperature, pressure and humidity using Adafruit BME280 library.
- Reads two analog inputs: KY-037 sound analog output and a photoresistor voltage divider.
- Parses PMS5003 serial frames (via Serial1) and logs PM1.0/PM2.5/PM10 values.
- Periodic sampling controlled by the `sample_interval_s` runtime param (default 600s).
- Serial output plus optional Firestore upload, with a web config portal for runtime params.

Wiring (matching repository README):
- BME280 SDA -> GPIO 1, SCL -> GPIO 2 (I2C)
- KY-037 AO -> GPIO 4 (analog)
- Photoresistor node -> GPIO 6 (analog)
- PMS5003 RX -> GPIO 17, TX -> GPIO 18, SET -> GPIO 7

Build and flash with PlatformIO:

1. Install PlatformIO extension for VS Code or use the CLI.
2. From this repo root run (PowerShell):

   pio run -e esp32s3 -t upload

3. Open the serial monitor:

   pio device monitor -e esp32s3

Notes and next steps:
- Runtime settings (WiFi creds, device ID, sample interval, batch size, upload on/off) are edited via the web config portal — browse to the device IP, or to the `AmbientMonitor-setup` AP if it can't join WiFi. Changes apply on reboot.
- For more robust parsing and sleep/power modes, consider using driver libraries and hardware flow control for PMS5003.
