#pragma once
#include <stddef.h>
#include <stdint.h>

// Plantower PMS5003 active-mode frames (manual v2.3): 0x42 0x4D, a 16-bit
// length (always 28), 13 big-endian data words, then a 16-bit checksum that is
// the byte sum of everything before it. 32 bytes in total.
//   Data1-3   PM1.0 / PM2.5 / PM10, "CF=1" factory units (ug/m3)
//   Data4-6   PM1.0 / PM2.5 / PM10, atmospheric (ug/m3)
//   Data7-12  particle counts per 0.1 L beyond 0.3 / 0.5 / 1 / 2.5 / 5 / 10 um
// Pure C++ (no Arduino) so it can be unit-tested on the host.

struct PmsFrame {
    uint16_t cf1[3];  // PM1.0, PM2.5, PM10, CF=1
    uint16_t pm[3];   // PM1.0, PM2.5, PM10, atmospheric (the values we use)
    uint16_t cnt[6];  // >0.3, >0.5, >1.0, >2.5, >5, >10 um per 0.1 L
};

class PmsParser {
public:
    static const size_t FRAME_LEN = 32;

    // Feed one received byte. Returns true when it completes a valid frame,
    // which is then in `out`. Bytes before a header, and frames with a bad
    // length or checksum, are dropped and the search for a header resumes.
    bool push(uint8_t b, PmsFrame& out);

    // Running counters (never reset here; callers diff them).
    uint32_t bytes = 0;    // bytes fed
    uint32_t frames = 0;   // valid frames
    uint32_t errors = 0;   // frames dropped for bad length or checksum

private:
    uint8_t buf_[FRAME_LEN];
    size_t len_ = 0;  // bytes of the current candidate frame in buf_
};
