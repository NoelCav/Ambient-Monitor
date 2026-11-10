# Hardware Plans

- RTC module (DS3231) - Keeps time through power loss, so rows logged before WiFi/NTP comes up still get a timestamp.

# Calibration

- Sound - Colour thresholds (`SQ` in `web/common.js`, `soundLevel` in `lib/ambient/levels.h`, 4 / 15 / 60 RMS) are guesses. Set them from a quiet room, a TV at normal volume and a clap, ideally next to a phone sound-meter app.
- Light - Lux is order-of-magnitude until calibrated. Measure lux at the sensor with a phone light-meter app, then run `light cal <lux>`.
- Second dust sensor - Before deploying a second monitor, run both side by side and note any offset.
