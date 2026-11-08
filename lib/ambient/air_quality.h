#pragma once
#include "pms_parser.h"

// Indoor PM2.5 estimate from particle counts: ALT-CF3 (Wallace et al. 2022,
// Sensors 22:4741), validated indoors and outdoors at a home:
//   PM2.5 = 3 * (0.00030418*N1 + 0.0018512*N2 + 0.02069706*N3)
// N1-N3 are the per-0.1 L counts in the 0.3-0.5, 0.5-1 and 1-2.5 um bins; the
// sensor reports cumulative ">x" counts, hence the differences. The raw
// atmospheric PM2.5 stays the logged trend value; this is the health estimate.
// Not logged (the counts are): the portal's graphs recompute it in JavaScript
// (est() in config_portal.cpp), so keep the two in step.
inline float pm25AltCf3(const PmsFrame& f) {
    auto bin = [&f](int i) { return f.cnt[i] > f.cnt[i + 1] ? (float)(f.cnt[i] - f.cnt[i + 1]) : 0.0f; };
    return 3.0f * (0.00030418f * bin(0) + 0.0018512f * bin(1) + 0.02069706f * bin(2));
}
