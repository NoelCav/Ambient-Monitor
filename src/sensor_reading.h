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
    bool pms_valid;

    // Sound (rolling 1s buffer)
    int sound_avg;
    int sound_peak;

    // Light (rolling average)
    int light;
};
