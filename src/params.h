#pragma once
#include <Arduino.h>

// Runtime parameters, stored in NVS (Preferences) and editable via the web
// config portal. Loaded once at boot; changes apply on reboot.
struct Params {
    String   wifiSsid;
    String   wifiPassword;
    String   deviceId;
    bool     uploadEnabled;
    uint32_t sampleIntervalS;
    uint16_t batchSize;
};

// Global accessor for the loaded params.
Params& params();

// Load params from NVS, falling back to compile-time defaults for unset keys.
// Validates/clamps values before they are exposed.
void paramsLoad();

// Validate and persist params to NVS. Does not reboot.
void paramsSave(const Params& p);

// Convenience: sample interval expressed in milliseconds.
uint32_t sampleIntervalMs();
