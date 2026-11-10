#pragma once
#include <stddef.h>
#include "air_quality.h"

// Plain-language levels for the console. The portal shows the same words with
// colours from the same thresholds (tempQ/humQ/presQ/pmQ/sndQ/lightQ in
// web/common.js): keep the two in step.

struct Band {
    float max;         // the level applies up to and including this value
    const char* name;
};

template <size_t N>
const char* bandName(const Band (&bands)[N], float v) {
    for (size_t i = 0; i < N; i++)
        if (v <= bands[i].max) return bands[i].name;
    return bands[N - 1].name;
}

inline const char* tempLevel(float c) {
    static const Band b[] = {{0, "freezing"}, {10, "very cold"}, {18, "cold"}, {22, "cool"},
                             {26, "comfortable"}, {30, "warm"}, {35, "hot"}, {1e9f, "very hot"}};
    return bandName(b, c);
}

inline const char* humidityLevel(float h) {
    static const Band b[] = {{20, "very dry"}, {30, "dry"}, {50, "comfortable"}, {60, "slightly humid"},
                             {70, "humid"}, {1e9f, "very humid"}};
    return bandName(b, h);
}

inline const char* pressureLevel(float p) {
    static const Band b[] = {{980, "storm likely"}, {1000, "low, unsettled"}, {1020, "normal"},
                             {1040, "high, fair"}, {1e9f, "very high"}};
    return bandName(b, p);
}

// PM: US EPA AQI bands (PM25_LIMITS / PM10_LIMITS in air_quality.h).
inline const char* pmLevel(const float (&limits)[5], float v) {
    static const char* const NAMES[] = {"good", "moderate", "unhealthy for sensitive", "unhealthy", "very unhealthy"};
    for (int i = 0; i < 5; i++)
        if (v <= limits[i]) return NAMES[i];
    return "hazardous";
}
inline const char* pm25Level(float v) { return pmLevel(PM25_LIMITS, v); }
inline const char* pm10Level(float v) { return pmLevel(PM10_LIMITS, v); }

// Sound RMS (ADC counts). Provisional thresholds until a TV/clap calibration.
inline const char* soundLevel(float v) {
    static const Band b[] = {{4, "quiet"}, {15, "some noise"}, {60, "loud"}, {1e9f, "very loud"}};
    return bandName(b, v);
}

// Light in lux (see light.h; approximate unless calibrated).
inline const char* lightLevel(float lux) {
    static const Band b[] = {{10, "dark"}, {50, "dim"}, {200, "indoor lighting"}, {500, "bright indoor"},
                             {5000, "very bright"}, {1e9f, "daylight"}};
    return bandName(b, lux);
}
