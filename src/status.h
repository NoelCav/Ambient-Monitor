#pragma once
#include <Arduino.h>

// GET /status.json for the portal:
// {
//   "sample": Reading | null,   last logged sample
//   "live":   Reading | null,   latest test reading (every LIVE_INTERVAL_MS)
//                               plus "diag" (dust frame stats for that window)
//   "sys":  uptime, reset reason, heap, WiFi, mode, schedule, log, tasks
//   "hw":   climate sensor, dust sensor power, log storage
// }
// Reading = {"age_s", "climate":{ok,temp,hum,pres}, "dust":{ok,pm[3],cf1[3],
//            cnt[6],est,n,awake_s}, "sound":{avg,peak}, "light"}
String statusJson();
