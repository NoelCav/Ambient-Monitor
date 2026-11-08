#pragma once
#include <Arduino.h>
#include <vector>

struct WifiNet {
    String ssid;
    String pass;  // empty for an open network
};

// Runtime parameters, stored in NVS (Preferences) and editable from the portal
// and console. The device ID applies on reboot; the rest apply immediately.
struct Params {
    std::vector<WifiNet> wifi;  // saved networks (up to WIFI_MAX); joins the strongest in range
    String   deviceId;
    uint32_t sampleIntervalS;
    // Test mode: the dust sensor stays on and [TEST]/[DUST] lines print every 5 s.
    bool     testMode;
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
