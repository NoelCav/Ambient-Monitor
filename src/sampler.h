#pragma once
#include <Arduino.h>
#include "reading.h"

// Sampler task: decides when the dust sensor is powered, takes the logged
// sample every sample interval and a test reading every LIVE_INTERVAL_MS.
//
// Schedule: the dust sensor wakes PMS_WARMUP_MS before each sample and sleeps
// after it; in test mode, or while a requested sample waits, it stays on.
// The first sample is taken as soon as the sensor has warmed up after boot.

void samplerBegin();

// Take a logged sample as soon as the dust sensor is warm (waking it if
// needed). The regular interval restarts from that sample.
void samplerRequestSample();

// Watchdog self-test: the sampler stops feeding the task watchdog, which
// must reboot the device (reset reason task_wdt) within WATCHDOG_S.
void samplerHang();

// Per-5 s dust diagnostics that go with the test reading.
struct DustDiag {
    uint32_t frames = 0, errors = 0, bytes = 0;  // in the last LIVE_INTERVAL_MS
    uint32_t intervalMs = 0;                     // between the last two frames
};

struct SamplerState {
    Reading sample;           // last logged sample (at_ms 0 = none yet)
    Reading live;             // latest test reading (at_ms 0 = none yet)
    DustDiag diag;
    uint32_t nextSampleS = 0;
    bool requested = false;
};
SamplerState samplerState();

// A reading as text: what = "all", "climate", "dust", "sound" or "light".
// Used for the [SAMPLE]/[TEST] console lines and the `read` command.
String readingText(const Reading& r, const char* what);
