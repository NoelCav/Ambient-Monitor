#pragma once
#include <Arduino.h>

// Actions on sensor and scheduler state owned by main.cpp, exposed for the
// console commands. They request things of the scheduler rather than
// overriding it, so nothing ever has to leave "auto" mode.

void devPrintStatus();

// Take a full (logged) sample as soon as the PMS is ready: immediately if it
// has been awake >= PMS_STABLE_MS, otherwise after it warms up. The regular
// interval then resumes from that sample.
void devRequestSample();

// Print current values without logging. what: "" / "all", "climate", "dust",
// "sound", "light". Returns false for an unknown name.
bool devRead(const String& what);

void devDiagClimate();   // BME chip ID, calibration, forced test, re-init
void devI2cScan();

// Apply immediately (and persist). Turning test mode off doesn't cut the PMS
// mid-run; it goes back to duty-cycling after the next sample.
void devSetTestMode(bool on);
void devSetSampleInterval(uint32_t seconds);
