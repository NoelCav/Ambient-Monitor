#pragma once
#include <Arduino.h>

// GET /status.json for the portal:
// {
//   "sample": Reading | null,   last logged sample
//   "live":   Reading | null,   latest test reading (every LIVE_INTERVAL_MS)
//                               plus "diag": {errors, dt_ms} (dust frames)
//   "sys":  uptime_s, reset, heap, rssi, test, sample_s, next_sample_s, tasks
//   "hw":   climate, climate_addr, dust_on, dust_s, warm_s
// }
// Reading = {"age_s", "climate":{ok,temp,hum,pres}, "dust":{ok,pm[3],cf1[3],
//            cnt[6],est,n,awake_s}, "sound":{avg,peak}, "light"}
String statusJson();
