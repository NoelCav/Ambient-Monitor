#pragma once
#include <Arduino.h>

struct SensorReading {
    unsigned long timestamp_ms;

    // BME280
    float temperature;
    float humidity;
    float pressure;
    bool bme_valid;

    // PMS5003
    uint16_t pm1_0;
    uint16_t pm2_5;
    uint16_t pm10;
    // Factory CF=1 values (inputs to published corrections such as the EPA
    // PurpleAir fit) and how long the dust sensor had been awake (settling).
    uint16_t pm1_0_cf1, pm2_5_cf1, pm10_cf1;
    uint32_t pms_awake_s;
    // Particle counts per 0.1 L beyond N um (latest frame)
    uint16_t cnt_0_3;
    uint16_t cnt_0_5;
    uint16_t cnt_1_0;
    uint16_t cnt_2_5;
    uint16_t cnt_5_0;
    uint16_t cnt_10;
    bool pms_valid;

    // Sound (rolling 1s buffer)
    // Sound loudness = peak-to-peak of each 1 s window of the KY-037 AO
    // (its average is only the DC bias). Over the sample interval:
    int sound_avg;      // average of the 1 s windows (steady noise)
    int sound_peak;     // loudest 1 s window (short bangs)

    // Light (rolling average)
    int light;
};
