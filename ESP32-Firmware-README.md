# Ambient Monitor - ESP32 Firmware

This folder contains a minimal ESP32 (S3) firmware written for PlatformIO / Arduino framework.

Features implemented:
- Reads BME280 (I2C at 0x76) for temperature, pressure and humidity using Adafruit BME280 library.
- Reads two analog inputs: KY-037 sound analog output and a photoresistor voltage divider.
- Parses PMS5003 serial frames (via Serial1) and logs PM1.0/PM2.5/PM10 values.
- Periodic sampling controlled by SAMPLE_INTERVAL_MS (default 10000ms).
- Serial-only output (no web server). This matches the user's request: "just esp no server yet".

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
- This is minimal firmware for development. It doesn't implement Wi-Fi or a web server yet.
- For more robust parsing and sleep/power modes, consider using driver libraries and hardware flow control for PMS5003.
