#pragma once
#include "sensor_reading.h"

// Persistent CSV log on the LittleFS data partition. Firmware uploads only
// rewrite the app partition, so the log survives reflashing; it is lost only
// on `pio run -t uploadfs`, a full chip erase, or a partition-table change.
//
// One row per sample in /log.csv. When it passes LOG_MAX_BYTES, or at boot if
// the stored header differs from the current columns, it is archived to
// saved/archive-<full|columns|migrated>-<start time>.csv and a new log starts, so each file has exactly
// one layout. Archives live alongside user-saved logs; when storage runs short
// the oldest archives (never user-named saves) are deleted.

void dataLogSetup();

void dataLogAppend(const SensorReading& r);

// Bytes used by the current log file (0 if not mounted).
size_t dataLogSize();

bool dataLogMounted();

// Delete the current log and start a new one with a header row (saved logs are kept).
void dataLogClear();

extern const char* const DATA_LOG_PATH;

// --- Saved logs: snapshots of log.csv kept under /saved for specific events ---
extern const char* const DATA_LOG_SAVED_DIR;
String dataLogSanitizeName(const String& name);   // [a-z0-9_-], max 31; "" if unusable
String dataLogSavedPath(const String& name);      // "/saved/<name>.csv"
// Copy log.csv (or the last N rows / rows since an epoch time) to saved/<name>.csv.
bool dataLogSave(const String& name, size_t lastRows, long sinceEpoch, String& err);
bool dataLogRemoveSaved(const String& name);
bool dataLogRenameSaved(const String& from, const String& to, String& err);
void dataLogForEachSaved(void (*fn)(const String& name, size_t bytes, void* ctx), void* ctx);
size_t dataLogFreeBytes();
size_t dataLogRowCount();
void dataLogPrintTail(size_t n);
