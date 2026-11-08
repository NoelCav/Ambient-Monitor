#pragma once
#include <stddef.h>

// Plain-language levels for the console. The portal shows the same words with
// colours from the same thresholds (tempQ/humQ/presQ/pmQ/sndQ/lightQ in
// config_portal.cpp): keep the two in step.

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

// US EPA AQI bands (2024). PM1.0 has no standard; the portal uses PM2.5's.
inline const char* pm25Level(float v) {
    static const Band b[] = {{9, "good"}, {35.4f, "moderate"}, {55.4f, "unhealthy for sensitive"},
                             {125.4f, "unhealthy"}, {225.4f, "very unhealthy"}, {1e9f, "hazardous"}};
    return bandName(b, v);
}

inline const char* pm10Level(float v) {
    static const Band b[] = {{54, "good"}, {154, "moderate"}, {254, "unhealthy for sensitive"},
                             {354, "unhealthy"}, {424, "very unhealthy"}, {1e9f, "hazardous"}};
    return bandName(b, v);
}

// Sound RMS (ADC counts). Provisional thresholds until a TV/clap calibration.
inline const char* soundLevel(float v) {
    static const Band b[] = {{4, "quiet"}, {15, "some noise"}, {60, "loud"}, {1e9f, "very loud"}};
    return bandName(b, v);
}

inline const char* lightLevel(float v) {
    static const Band b[] = {{400, "dark"}, {1000, "dim"}, {2000, "indoor lighting"}, {3000, "bright indoor"},
                             {4094, "bright"}, {1e9f, "saturated"}};
    return bandName(b, v);
}
