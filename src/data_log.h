#pragma once
#include "sensor_reading.h"

// Persistent CSV log on the LittleFS data partition. Firmware uploads only
// rewrite the app partition, so the log survives reflashing; it is lost only
// on `pio run -t uploadfs`, a full chip erase, or a partition-table change.
//
// One row per sample in /log.csv. When it passes LOG_MAX_BYTES it is rotated
// to /log.old.csv (replacing the previous one), so at most ~2x that is kept.
// It also rotates at boot if the stored header differs from the current
// columns, so each file has exactly one layout and opens cleanly anywhere.

void dataLogSetup();

void dataLogAppend(const SensorReading& r);

// Bytes used by the current and rotated log files (0 if not mounted).
size_t dataLogSize();

bool dataLogMounted();

// Delete both log files and start a new one with a header row.
void dataLogClear();

extern const char* const DATA_LOG_PATH;
extern const char* const DATA_LOG_OLD_PATH;
