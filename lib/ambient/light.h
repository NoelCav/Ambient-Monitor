#pragma once
#include <math.h>

// Approximate lux from the photoresistor (LDR) reading. Circuit:
//   3.3 V -- LDR -- GPIO6 -- 2 kOhm -- GND
// (2 kOhm since 2026-11-08; 10 kOhm maxed out with a lamp on across the room.)
// The ADC (12 dB attenuation) reads about 0-3.1 V as 0-4095. An LDR's
// resistance falls with light as R = R10 * (lux / 10)^-gamma, where R10 is its
// resistance at 10 lux. GL5528-type parts: R10 10-20 kOhm, gamma ~0.6. Without
// a calibration (`light cal <lux>`) this is only an order-of-magnitude figure.
// The portal's graph JS (lux() in config_portal.cpp) uses the same formula.

static const float LDR_FIXED_OHMS = 2000;  // keep in step with lux() in web/common.js
static const float LDR_GAMMA = 0.6f;
static const float LDR_R10_DEFAULT = 15000;  // ohms at 10 lux (uncalibrated)
static const float ADC_FULL_VOLTS = 3.1f;
static const float ADC_SATURATED = 4090;     // at or above: brighter than the sensor can tell

inline float ldrOhms(float raw) {
    if (raw < 1) raw = 1;
    float v = raw * ADC_FULL_VOLTS / 4095;
    return LDR_FIXED_OHMS * (3.3f - v) / v;
}

inline float lightLux(float raw, float r10) {
    return 10 * powf(r10 / ldrOhms(raw), 1 / LDR_GAMMA);
}

// R10 that makes the current raw reading equal a lux value measured with a
// reference meter (e.g. a phone light-meter app): a one-point calibration.
inline float ldrR10For(float raw, float lux) {
    return ldrOhms(raw) * powf(lux / 10, LDR_GAMMA);
}
