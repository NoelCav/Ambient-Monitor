#pragma once

// Browser-based config portal. Serves an HTML form (over STA or AP WiFi) that
// reads/writes runtime params. Saving persists to NVS and reboots the device.

void configPortalSetup();

// Call from loop() — services pending HTTP clients (non-blocking).
void configPortalLoop();
