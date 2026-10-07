#pragma once
#include "sensor_reading.h"

void firestoreSetup();

// Queue a reading for upload. Stored in a ring buffer if WiFi is down.
void queueReading(const SensorReading& r);

// Call from loop(). Flushes queued readings when WiFi is available.
// Uploads up to maxPerCall readings per invocation to avoid blocking.
void firestoreLoop(unsigned maxPerCall = 5);

unsigned firestoreQueueCount();
