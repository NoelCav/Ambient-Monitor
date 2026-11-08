#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "pms_parser.h"

// Small statistics helpers shared by the sensor tasks. Pure C++, host-tested.

// Mean and maximum of a stream of values since the last reset.
struct MeanMax {
    double sum = 0;
    float max = 0;
    uint32_t n = 0;
    void add(float v) {
        sum += v;
        if (n == 0 || v > max) max = v;
        n++;
    }
    float mean() const { return n ? (float)(sum / n) : 0; }
};

// Accumulates raw ADC samples for one window and gives their mean and their
// RMS about that mean: the KY-037 output is a DC bias plus the audio signal,
// so subtracting the window's own mean leaves only the sound.
struct Window {
    int64_t sum = 0, sumSq = 0;
    uint32_t n = 0;
    void add(int x) {
        sum += x;
        sumSq += (int64_t)x * x;
        n++;
    }
    float mean() const { return n ? (float)sum / n : 0; }
    // Mean square about the mean (variance); RMS is its square root.
    float meanSquare() const {
        if (!n) return 0;
        double m = (double)sum / n;
        double v = (double)sumSq / n - m * m;
        return v > 0 ? (float)v : 0;
    }
};

// Sound and light over a span (sample interval or test window), built from
// fixed-length windows: average RMS is the RMS of all the sound in the span
// (sqrt of the mean of window mean squares), peak is the loudest window.
struct SoundLightStats {
    MeanMax soundMs;  // per-window mean square
    MeanMax light;    // per-window mean
    void add(const Window& sound, const Window& lightWin) {
        soundMs.add(sound.meanSquare());
        light.add(lightWin.mean());
    }
    float soundAvg() const { return sqrtf(soundMs.mean()); }
    float soundPeak() const { return sqrtf(soundMs.max); }
};

// The last N frames, averaged field by field (rounded), since single frames
// are noisy. The same average is used for logged samples and test readings.
template <size_t N>
struct FrameAverage {
    PmsFrame frames[N];
    size_t count = 0, next = 0;
    void clear() { count = next = 0; }
    void add(const PmsFrame& f) {
        frames[next] = f;
        next = (next + 1) % N;
        if (count < N) count++;
    }
    PmsFrame average() const {
        uint32_t cf1[3] = {0}, pm[3] = {0}, cnt[6] = {0};
        for (size_t i = 0; i < count; i++) {
            for (int k = 0; k < 3; k++) {
                cf1[k] += frames[i].cf1[k];
                pm[k] += frames[i].pm[k];
            }
            for (int k = 0; k < 6; k++) cnt[k] += frames[i].cnt[k];
        }
        PmsFrame a = {};
        if (!count) return a;
        for (int k = 0; k < 3; k++) {
            a.cf1[k] = (uint16_t)((cf1[k] + count / 2) / count);
            a.pm[k] = (uint16_t)((pm[k] + count / 2) / count);
        }
        for (int k = 0; k < 6; k++) a.cnt[k] = (uint16_t)((cnt[k] + count / 2) / count);
        return a;
    }
};
