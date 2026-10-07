#pragma once

void wifiSetup();
void wifiLoop();
bool wifiConnected();

// True when the device fell back to hosting its own AP (config still reachable).
bool wifiApMode();
