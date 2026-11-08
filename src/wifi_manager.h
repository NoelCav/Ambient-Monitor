#pragma once
#include <Arduino.h>

// WiFi: joins the strongest saved network in range (Params::wifi, up to
// WIFI_MAX). If none is reachable it hosts the setup AP (AP_SSID, portal at
// 192.168.4.1) and rescans every 2 minutes; the AP turns off once connected.
// Also NTP, mDNS and ArduinoOTA.

void wifiSetup();
void wifiLoop();
bool wifiConnected();

// True once NTP has set the clock (epoch times before that are meaningless).
bool timeSynced();

// Add a network, or change the password of one already saved. Saved to NVS
// and used from the next scan (immediately if offline).
bool wifiAdd(const String& ssid, const String& pass, String& err);
// Remove a saved network (not the last one). If it is the current one, the
// device switches to another saved network.
bool wifiRemove(const String& ssid, String& err);
