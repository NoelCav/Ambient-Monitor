#include "pms_parser.h"

bool PmsParser::push(uint8_t b, PmsFrame& out) {
    bytes++;
    // Header sync: the first two bytes must be 0x42 0x4D. A stray 0x42 that
    // isn't followed by 0x4D may itself start the real header.
    if (len_ == 0 && b != 0x42) return false;
    if (len_ == 1 && b != 0x4D) {
        len_ = b == 0x42 ? 1 : 0;
        return false;
    }
    buf_[len_++] = b;
    if (len_ == 4 && ((buf_[2] << 8) | buf_[3]) != FRAME_LEN - 4) {
        errors++;  // not an active-mode frame
        len_ = 0;
        return false;
    }
    if (len_ < FRAME_LEN) return false;

    len_ = 0;
    uint16_t sum = 0;
    for (size_t i = 0; i < FRAME_LEN - 2; i++) sum += buf_[i];
    if (sum != ((buf_[FRAME_LEN - 2] << 8) | buf_[FRAME_LEN - 1])) {
        errors++;
        return false;
    }
    // Data word n (1-based) sits at byte 2 + 2n, big-endian.
    auto data = [this](int n) { return (uint16_t)((buf_[2 + 2 * n] << 8) | buf_[3 + 2 * n]); };
    for (int i = 0; i < 3; i++) {
        out.cf1[i] = data(1 + i);
        out.pm[i] = data(4 + i);
    }
    for (int i = 0; i < 6; i++) out.cnt[i] = data(7 + i);
    frames++;
    return true;
}
