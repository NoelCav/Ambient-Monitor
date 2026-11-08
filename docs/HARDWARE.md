# Hardware notes and improvements

Current prototype: ESP32-S3 DevKitC-1 clone (N16R8) on a breadboard, powered over USB. Wiring is in the README.

## Planned changes

| # | Change | Why | Priority |
|---|--------|-----|----------|
| 1 | **Remove the KY-037 voltage divider** (AO straight to GPIO 4, module stays on 3.3 V) | At 3.3 V the output can't exceed the ADC's range, so the divider only shrinks the signal. Don't move the module to 5 V instead: its output would then exceed the ADC and need the divider again, ending at the same signal. | Next |
| 2 | **RTC module (DS3231, ~$3)** on the I2C bus with the BME280 | Keeps time through power loss, so rows logged before WiFi/NTP comes up still get a timestamp (today they are logged without one and skipped by the graphs). Less important once data is uploaded with server-side timestamps. | Later |
| 3 | **Check the PMS5003 supply under load** | It measured ~4.5 V, the bottom of its 4.5-5.5 V spec. The fan speed sets the airflow the sensor's maths assumes, so a sagging supply reads slightly low. Two units agreed (~57 ug/m3), so it's fine today. Measure VCC-GND while WiFi is transmitting; if it drops below 4.5 V, use a better USB cable/port or a separate 5 V supply with a common ground. | Watch |
| 4 | **Solder BME280 #2** | Press-fit, loses contact. Needed for the second monitor. | With monitor #2 |
| 5 | **Permanent build** (see below) | A breadboard's contacts loosen over time; the BME #1 GND joint already cracked once. | Soon |

## Moving off the breadboard (no custom PCB)

1. **Solder onto perfboard / stripboard** (a 5x7 cm board is ~$1). Put the ESP32 on female headers so it can be swapped. Run 5 V and GND as short, thick traces. Use JST or Dupont connectors for the sensors, so each one can be unplugged.
2. **Enclosure:** a small ventilated plastic project box (or a 3D-printed one):
   - **PMS5003:** its inlet and outlet must open to room air, unobstructed, and not facing each other (or it re-measures its own exhaust). Mount it against a wall of the box with holes over both openings.
   - **BME280:** on a short lead, outside the box or in a vented corner away from the ESP32 and the PMS motor, which both make heat. Inside a closed box it reads several degrees warm.
   - **Microphone:** a hole in front of it, not covered by foam.
   - **Photoresistor:** behind a hole or a clear window, facing the room, not the status LED.
   - **Status LED:** a light pipe or a small hole.
3. **Placement:** about 1-1.5 m up, away from vents, windows, kitchens and direct sun. It measures the room, not a draught.

## Calibration to do (needs a person in the room)

- **Sound:** quiet room, TV at normal volume and a clap, ideally next to a phone sound-meter app, to set the colour thresholds (`SQ` in the portal, `soundLevel` in `lib/ambient/levels.h`) and to map RMS to approximate dB.
- **Light:** measure lux at the sensor with a phone light-meter app, then run `light cal <lux>`. Until then lux is an order-of-magnitude estimate.
- **Second dust sensor:** before deploying monitor #2, run both side by side for a day and record any offset between them.
