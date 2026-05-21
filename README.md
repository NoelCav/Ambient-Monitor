# Ambient-Monitor
A robust, ESP32-powered IoT node that samples data from an array of environmental and telemetry sensors, serving it over a local web interface and logging data points dynamically.

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
      |   [GPIO 17] ------------> [RX]  PMS5003 Laser    |
      |   [GPIO 18] ------------> [TX]  PMS5003 Laser    |
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
   [VCC] -> 5.0V Power Rail  <-- (Uses 5V for better accuracy)
   [GND] -> Ground Rail (-)
   [AO]  -> ESP32 GPIO 4
   [DO]  -> [Leave Disconnected]

3. PMS5003 (Laser Dust)
   [VCC]   -> 5.0V Power Rail  <-- (Required to spin internal fan)
   [GND]   -> Ground Rail (-)
   [RX]    -> ESP32 GPIO 17
   [TX]    -> ESP32 GPIO 18
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
   5.0V Rail (+) <=== ESP32 [5V]   <--- Powers: KY-037, PMS5003
   3.3V Rail (Y) <=== ESP32 [3V3]  <--- Powers: BME280, Photoresistor
   Ground Rail(-)<=== ESP32 [GND]  <--- Connects ALL sensor GND lines
                                        & the base of the 10kΩ resistor
===================================================================
```
