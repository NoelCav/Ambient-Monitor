#pragma once
#include "pms_parser.h"

// Indoor PM2.5 estimate from particle counts: ALT-CF3 (Wallace et al. 2022,
// Sensors 22:4741), validated indoors and outdoors at a home:
//   PM2.5 = 3 * (0.00030418*N1 + 0.0018512*N2 + 0.02069706*N3)
// N1-N3 are the per-0.1 L counts in the 0.3-0.5, 0.5-1 and 1-2.5 um bins; the
// sensor reports cumulative ">x" counts, hence the differences. The raw
// atmospheric PM2.5 stays the logged trend value; this is the health estimate.
// Not logged (the counts are): the portal's graphs recompute it in JavaScript
// (est() in web/common.js), so keep the two in step.
inline float pm25AltCf3(const PmsFrame& f) {
    auto bin = [&f](int i) { return f.cnt[i] > f.cnt[i + 1] ? (float)(f.cnt[i] - f.cnt[i + 1]) : 0.0f; };
    return 3.0f * (0.00030418f * bin(0) + 0.0018512f * bin(1) + 0.02069706f * bin(2));
}

// US EPA AQI band upper limits (2024): good, moderate, unhealthy for
// sensitive groups, unhealthy, very unhealthy; above the last is hazardous.
// Used by the console words (levels.h) and the LED; the portal JS has its own
// copy (PM25 / PM10 in web/common.js).
static const float PM25_LIMITS[5] = {9, 35.4f, 55.4f, 125.4f, 225.4f};
static const float PM10_LIMITS[5] = {54, 154, 254, 354, 424};

// Where a value sits on AQI band limits, as a continuous index: 0 at or below
// the first ("good") limit, i + fraction between limit i and i+1, 4 at or
// above the last. The same mapping as the portal's colour gradient (pmQ).
inline float bandIndex(const float (&limits)[5], float v) {
    if (v <= limits[0]) return 0;
    for (int i = 1; i < 5; i++)
        if (v <= limits[i]) return i - 1 + (v - limits[i - 1]) / (limits[i] - limits[i - 1]);
    return 4;
}

// Overall air quality for the status LED: the worst of PM2.5 (ALT-CF3
// estimate), PM1.0 (on the PM2.5 scale, as the portal does) and PM10.
inline float airIndex(const PmsFrame& f) {
    float a = bandIndex(PM25_LIMITS, pm25AltCf3(f)), b = bandIndex(PM25_LIMITS, f.pm[0]), c = bandIndex(PM10_LIMITS, f.pm[2]);
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}
