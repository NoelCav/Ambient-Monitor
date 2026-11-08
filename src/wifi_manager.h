#pragma once

void wifiSetup();
void wifiLoop();
bool wifiConnected();

// True once NTP has set the clock (epoch times before that are meaningless).
bool timeSynced();
