# Ambient-Monitor
ESP32-S3 environmental monitor: temperature, humidity and pressure (BME280), particulate matter (PMS5003), sound (KY-037) and light (photoresistor). It logs to an on-device CSV that survives firmware updates and is run entirely over WiFi: a web page with live readings, graphs and logs, a network console, and OTA updates.

## Firmware
PlatformIO, Arduino framework on FreeRTOS. Copy `src/secrets.h.example` to `src/secrets.h` (and `secrets.ini.example` to `secrets.ini` for OTA), then:

```text
pio run -t upload --upload-port COM3   # first flash, over USB
python scripts/ota.py                  # later flashes, over WiFi: finds monitors, you pick
pio run -e ota -t upload               # same, to esp32-ambient-01.local by name
pio test -e native                     # host unit tests
```

Then open `http://esp32-ambient-01.local` (or the `AmbientMonitor-setup` WiFi network at 192.168.4.1 if it can't join your WiFi). Developer notes, architecture and sensor findings are in `CLAUDE.md`.

## 🔌 Hardware Architecture & Wiring

To replicate this environmental data logger, connect the components to your ESP32-S3 (N16R8) according to the master diagram and breakout specifications below:

```text
===================================================================
                     MASTER WIRING DIAGRAM
===================================================================

      +-------------------| ESP32-S3 |-------------------+
      |                                                  |
      |   [3V3] -----------------> To 3.3V Power Rail    |
      |   [5V]  -----------------> To 5.0V Power Rail    |
      |   [GND] -----------------> To Ground Rail (-)    |
      |                                                  |
      |   [GPIO 1]  ------------> [SDA] BME280 Sensor    |
      |   [GPIO 2]  ------------> [SCL] BME280 Sensor    |
      |   [GPIO 4]  ------------> [AO]  KY-037 Sound     |
      |   [GPIO 6]  ------------> [Node] Photoresistor   |
      |   [GPIO 7]  ------------> [SET] PMS5003 Laser    |
      |   [GPIO 17] <------------ [TX]  PMS5003 Laser    |
      |   [GPIO 18] ------------> [RX]  PMS5003 Laser    |
      |                                                  |
      +--------------------------------------------------+

-------------------------------------------------------------------
COMPONENT CONNECTIONS DETAILED
-------------------------------------------------------------------

1. BME280 (Atmospheric)
   [VCC] -> 3.3V Power Rail
   [GND] -> Ground Rail (-)
   [SDO] -> Ground Rail (-)  <-- (Locks I2C address to 0x76)
   [SDA] -> ESP32 GPIO 1
   [SCL] -> ESP32 GPIO 2
   [CSB] -> [Leave Disconnected]

2. KY-037 (Sound Module)
   [VCC] -> 3.3V Power Rail
   [GND] -> Ground Rail (-)
   [AO]  -> ESP32 GPIO 4     <-- (currently through a voltage divider, which
                                  isn't needed at 3.3 V and only weakens the signal)
   [DO]  -> [Leave Disconnected]

3. PMS5003 (Laser Dust)
   [VCC]   -> 5.0V Power Rail  <-- (Required to spin internal fan)
   [GND]   -> Ground Rail (-)
   [TX]    -> ESP32 GPIO 17  <-- (ESP32 receives on 17)
   [RX]    -> ESP32 GPIO 18
   [SET]   -> ESP32 GPIO 7
   [RESET] -> [Leave Disconnected]

4. Photoresistor Circuit (Ambient Light)
   3.3V Power Rail ---> [ Photoresistor ] ---> [ Node ] ---> ESP32 GPIO 6
                                                 |
                                                 v
                                          [ 10kΩ Resistor ]
                                                 |
                                                 v
                                           Ground Rail (-)

-------------------------------------------------------------------
BREADBOARD POWER BUS DISTRIBUTION (The Rails)
-------------------------------------------------------------------
   5.0V Rail (+) <=== ESP32 [5V]   <--- Powers: PMS5003
   3.3V Rail (Y) <=== ESP32 [3V3]  <--- Powers: BME280, KY-037, Photoresistor
   Ground Rail(-)<=== ESP32 [GND]  <--- Connects ALL sensor GND lines
                                        & the base of the 10kΩ resistor
===================================================================
```
