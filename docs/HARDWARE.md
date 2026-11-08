# Hardware notes and improvements

Current prototype: ESP32-S3 DevKitC-1 clone (N16R8) on a breadboard, powered over USB. Wiring is in the README.

## Planned changes

| # | Change | Why | Priority |
|---|--------|-----|----------|
| 1 | **Remove the KY-037 voltage divider** (AO straight to GPIO 4, module stays on 3.3 V) | At 3.3 V the output can't exceed the ADC's range, so the divider only shrinks the signal. Don't move the module to 5 V instead: its output would then exceed the ADC and need the divider again, ending at the same signal. | Next |
| 2 | **RTC module (DS3231, ~$3)** on the I2C bus with the BME280 | Keeps time through power loss, so rows logged before WiFi/NTP comes up still get a timestamp (today they are logged without one and skipped by the graphs). Less important once data is uploaded with server-side timestamps. | Later |
| 3 | **PMS5003 supply** (measured 4.58 V on 2026-11-08) | Inside its 4.5-5.5 V spec, but near the bottom. The fan speed sets the airflow the sensor's maths assumes, so a sagging supply reads slightly low. The likely cause is the DevKit's 5 V pin being fed from USB through a protection diode (~0.3-0.4 V drop). Fixes, cheapest first: (a) a short, thick USB cable and a 5 V / 2 A wall adapter instead of a laptop port; (b) bridge the board's `IN-OUT` / `USB-OTG` pad, if it has one, to bypass the diode (~4.9 V), and then never power from the 5 V pin and USB at the same time; (c) power the PMS from its own 5 V supply with a common ground (best for the permanent build). | Accepted: (a) and the pad bridge are already done (3.3 -> 4.7 V); 4.58 V is in spec, and (c) adds parts. |
| 4 | **Solder BME280 #2** | Press-fit, loses contact. Needed for the second monitor. | With monitor #2 |
| 5 | **Permanent build** (see below) | A breadboard's contacts loosen over time; the BME #1 GND joint already cracked once. | Soon |

## Moving off the breadboard (no custom PCB)

1. **Solder onto a breadboard-layout protoboard** (ElectroCookie "solderable breadboard" or Adafruit Perma-Proto, full size, ~6.8 x 2 in). Its holes connect in the same rows and power rails as the breadboard, so the current layout transfers one-to-one. Put the ESP32 in two 22-pin female headers so it can be swapped. Give each sensor male header pins on the board and connect it with a female-female Dupont cable, so any sensor unplugs. Use 22 AWG solid-core wire (colour-coded: red 5 V, orange 3.3 V, black GND), laid flat with a little slack, never taut. Check 5 V / 3.3 V / GND for shorts with a multimeter before powering up.

   Parts: full-size breadboard-layout protoboard; 2.54 mm 1x40 female and male breakaway headers; F-F Dupont cables (10-20 cm); 22 AWG solid-core spool set; 0.6-0.8 mm rosin-core solder, flux pen, desoldering braid; flush cutters, wire stripper, heat-shrink. Later: a vented ABS box ~100 x 70 x 40 mm and M2.5/M3 standoffs. JST-XH connectors are neater but need a crimper.
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
