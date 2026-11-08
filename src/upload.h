#pragma once
#include <Arduino.h>

// Upload task: sends log rows to the Google Sheet web app
// (cloud/apps_script/Code.gs). The on-device log is the queue: each pass
// sends the rows stamped after the last epoch the Sheet confirmed (saved in
// NVS), up to UPLOAD_BATCH rows per POST, so an outage of any length is caught
// up afterwards (as long as the rows are still in log.csv). Failures back off
// 1, 2, 4 ... 30 min. Rows without a clock time are not uploaded.
// Settings: `upload url|key|on|off` (Params::upload*).

void uploadBegin();
void uploadNow();      // try at the next pass instead of waiting for the backoff
void uploadResend();   // forget what was sent: re-send everything in log.csv (the Sheet skips duplicates)

struct UploadStatus {
    bool configured = false;  // on, with a URL and key
    uint32_t lastOkMs = 0;    // millis() of the last successful POST (0 = none yet)
    long lastEpoch = 0;       // newest row the Sheet has
    size_t pending = 0;       // rows waiting, as of the last pass
    String lastError;         // "" after a success
};
UploadStatus uploadStatus();
