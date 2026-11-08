#include "status.h"
#include "air_quality.h"
#include "climate.h"
#include "config.h"
#include "dust.h"
#include "health.h"
#include "light.h"
#include "params.h"
#include "sampler.h"
#include "tasks.h"
#include <WiFi.h>

static const char* tf(bool b) { return b ? "true" : "false"; }

// The one serializer for a Reading (logged sample and test reading alike).
// `extra` is appended inside the object (e.g. the test reading's diagnostics).
static String readingJson(const Reading& r, const String& extra = "") {
    if (!r.at_ms) return "null";
    char buf[420];
    const PmsFrame& d = r.dust;
    snprintf(buf, sizeof(buf),
        "{\"age_s\":%lu,\"climate\":{\"ok\":%s,\"temp\":%.2f,\"hum\":%.1f,\"pres\":%.1f},"
        "\"dust\":{\"ok\":%s,\"pm\":[%u,%u,%u],\"cf1\":[%u,%u,%u],\"cnt\":[%u,%u,%u,%u,%u,%u],"
        "\"est\":%.1f,\"n\":%u,\"awake_s\":%lu},\"sound\":{\"avg\":%.1f,\"peak\":%.1f},\"light\":%.0f,\"lux\":%.1f",
        (unsigned long)((millis() - r.at_ms) / 1000), tf(r.climate.ok), r.climate.temp, r.climate.hum, r.climate.pres,
        tf(r.dustOk), d.pm[0], d.pm[1], d.pm[2], d.cf1[0], d.cf1[1], d.cf1[2],
        d.cnt[0], d.cnt[1], d.cnt[2], d.cnt[3], d.cnt[4], d.cnt[5],
        pm25AltCf3(d), r.dustFrames, (unsigned long)r.dustAwakeS, r.soundAvg, r.soundPeak, r.light,
        lightLux(r.light, params().ldrR10));
    return String(buf) + extra + "}";
}

String statusJson() {
    SamplerState s = samplerState();
    DustStatus dust = dustStatus();
    String diag = ",\"diag\":{\"errors\":" + String(s.diag.errors) + ",\"dt_ms\":" + String(s.diag.intervalMs) + "}";
    String j = "{\"sample\":" + readingJson(s.sample);
    j += ",\"live\":" + readingJson(s.live, diag);

    j += ",\"sys\":{\"uptime_s\":" + String(millis() / 1000);
    j += ",\"reset\":\"" + String(resetReasonStr()) + "\"";
    j += ",\"heap\":" + String(ESP.getFreeHeap());
    j += ",\"fw\":\"" + String(firmwareState()) + "\",\"build\":\"" + String(buildTime()) + "\"";
    j += ",\"rssi\":" + String(WiFi.RSSI());
    j += ",\"test\":" + String(tf(params().testMode));
    j += ",\"sample_s\":" + String(params().sampleIntervalS);
    j += ",\"next_sample_s\":" + String(s.nextSampleS);
    j += ",\"tasks\":" + tasksJson() + "}";

    j += ",\"hw\":{\"climate\":" + String(tf(climateFound())) + ",\"climate_addr\":" + String(climateAddr());
    j += ",\"dust_on\":" + String(tf(dust.on)) + ",\"dust_s\":" + String(dust.awakeMs / 1000);
    j += ",\"warm_s\":" + String(PMS_WARMUP_MS / 1000);
    j += ",\"ldr_r10\":" + String(params().ldrR10, 0) + "}";
    return j + "}";
}
