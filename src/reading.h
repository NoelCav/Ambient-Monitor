#pragma once
#include <stdint.h>
#include "pms_parser.h"

// Temperature / humidity / pressure from the BME280.
struct Climate {
    bool ok = false;
    float temp = 0;  // C
    float hum = 0;   // % RH
    float pres = 0;  // hPa
};

// One set of readings over a span: a logged sample (span = the sample
// interval) or a test reading (span = the last LIVE_INTERVAL_MS). Both are
// built by the same function (sampler.cpp takeReading) and serialized by the
// same code (log row, status JSON, console line), so they always match.
struct Reading {
    uint32_t at_ms = 0;        // millis() when taken; 0 = none yet
    Climate climate;
    bool dustOk = false;       // dust sensor awake and sending frames
    PmsFrame dust = {};        // average of the last PMS_AVG_FRAMES frames
    uint8_t dustFrames = 0;    // frames in that average
    uint32_t dustAwakeS = 0;   // seconds the dust sensor had been awake
    float soundAvg = 0;        // RMS of all sound in the span (ADC counts)
    float soundPeak = 0;       // loudest SOUND_WINDOW_MS window (RMS)
    float light = 0;           // mean over the span, 0-4095
};
