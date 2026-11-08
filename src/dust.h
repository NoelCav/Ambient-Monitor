#pragma once
#include <Arduino.h>
#include "pms_parser.h"

// PMS5003 dust sensor task. Owns the UART (Serial1) and the SET power pin:
// decodes frames as they arrive and keeps the average of the last
// PMS_AVG_FRAMES. Power is requested by the sampler; the task applies it.

void dustBegin();

// Ask for the sensor on or off (takes effect within one task pass). Turning
// it on restarts the awake timer and clears the frame average.
void dustSetPower(bool on);

struct DustStatus {
    bool on = false;
    uint32_t awakeMs = 0;      // since it was powered on (0 when off)
    bool fresh = false;        // a frame arrived in the last 5 s
    uint8_t frames = 0;        // frames in the average
    PmsFrame avg = {};         // average of the last PMS_AVG_FRAMES frames
    PmsFrame last = {};        // most recent single frame
    uint32_t intervalMs = 0;   // time between the last two frames
    uint32_t totalFrames = 0, totalErrors = 0, totalBytes = 0;  // since boot
};
DustStatus dustStatus();
