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
    // Particle counts per 0.1 L beyond N um (latest frame)
    uint16_t cnt_0_3;
    uint16_t cnt_0_5;
    uint16_t cnt_1_0;
    uint16_t cnt_2_5;
    uint16_t cnt_5_0;
    uint16_t cnt_10;
    bool pms_valid;

    // Sound (rolling 1s buffer)
    int sound_avg;
    int sound_peak;
    int sound_p2p;      // max - min over the last 1s window; tracks loudness
    int sound_p2p_max;  // loudest 1s window since the previous sample

    // Light (rolling average)
    int light;
};
