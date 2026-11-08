#include "sampler.h"
#include "air_quality.h"
#include "climate.h"
#include "config.h"
#include "console.h"
#include "data_log.h"
#include "dust.h"
#include "params.h"
#include "soundlight.h"
#include "tasks.h"

static SemaphoreHandle_t mtx;
static SamplerState state;
static uint32_t lastSampleMs = 0;

// The one place a Reading is built, for logged samples and test readings.
static Reading takeReading(Span span) {
    Reading r;
    r.at_ms = millis();
    r.climate = climateRead();
    DustStatus d = dustStatus();
    r.dustOk = d.fresh && d.frames > 0;
    r.dust = d.avg;
    r.dustFrames = d.frames;
    r.dustAwakeS = d.awakeMs / 1000;
    SoundLightStats s = soundLightTake(span);
    r.soundAvg = s.soundAvg();
    r.soundPeak = s.soundPeak();
    r.light = s.light.mean();
    return r;
}

String readingText(const Reading& r, const char* what) {
    bool all = !strcmp(what, "all");
    char buf[112];
    String out;
    auto part = [&](const char* name) {
        if (!all && strcmp(what, name)) return false;
        if (out.length()) out += " | ";
        return true;
    };
    if (part("climate")) {
        if (r.climate.ok) snprintf(buf, sizeof(buf), "climate %.2f C, %.1f %% RH, %.1f hPa", r.climate.temp, r.climate.hum, r.climate.pres);
        else snprintf(buf, sizeof(buf), "climate: no reading (try diag climate)");
        out += buf;
    }
    if (part("dust")) {
        if (r.dustOk) snprintf(buf, sizeof(buf), "dust PM1/2.5/10 %u/%u/%u ug/m3 (PM2.5 est %.0f; %u frames, awake %lu s)",
            r.dust.pm[0], r.dust.pm[1], r.dust.pm[2], pm25AltCf3(r.dust), r.dustFrames, (unsigned long)r.dustAwakeS);
        else if (r.dustAwakeS) snprintf(buf, sizeof(buf), "dust: no frames yet (awake %lu s)", (unsigned long)r.dustAwakeS);
        else snprintf(buf, sizeof(buf), "dust: asleep ('sample' or test mode wakes it)");
        out += buf;
    }
    if (part("sound")) {
        snprintf(buf, sizeof(buf), "sound %.1f avg / %.1f peak", r.soundAvg, r.soundPeak);
        out += buf;
    }
    if (part("light")) {
        snprintf(buf, sizeof(buf), "light %.0f", r.light);
        out += buf;
    }
    return out;
}

static void samplerTask(void*) {
    DustStatus prev = dustStatus();
    uint32_t lastLiveMs = millis();
    for (;;) {
        uint32_t now = millis(), interval = sampleIntervalMs();
        bool test = params().testMode;
        bool requested;
        {
            Guard g(mtx);
            requested = state.requested;
        }
        dustSetPower(test || requested || now - lastSampleMs + PMS_WARMUP_MS >= interval);

        if (now - lastLiveMs >= LIVE_INTERVAL_MS) {
            lastLiveMs = now;
            DustStatus d = dustStatus();
            DustDiag diag;
            diag.frames = d.totalFrames - prev.totalFrames;
            diag.errors = d.totalErrors - prev.totalErrors;
            diag.bytes = d.totalBytes - prev.totalBytes;
            diag.intervalMs = d.intervalMs;
            prev = d;
            Reading r = takeReading(SPAN_LIVE);
            {
                Guard g(mtx);
                state.live = r;
                state.diag = diag;
            }
            if (test) {
                Console.printf("[TEST] %s\n", readingText(r, "all").c_str());
                Console.printf("[DUST] cf1 %u/%u/%u | >0.3 %u >0.5 %u >1 %u >2.5 %u >5 %u >10 %u | %lu frames, %lu errors, %lu bytes, dt %lu ms\n",
                    r.dust.cf1[0], r.dust.cf1[1], r.dust.cf1[2], r.dust.cnt[0], r.dust.cnt[1], r.dust.cnt[2],
                    r.dust.cnt[3], r.dust.cnt[4], r.dust.cnt[5], (unsigned long)diag.frames,
                    (unsigned long)diag.errors, (unsigned long)diag.bytes, (unsigned long)diag.intervalMs);
            }
        }

        DustStatus d = dustStatus();
        bool warm = d.on && d.awakeMs >= PMS_WARMUP_MS;
        if (now - lastSampleMs >= interval || (requested && warm)) {
            Reading r = takeReading(SPAN_SAMPLE);
            Console.printf("[SAMPLE] %s\n", readingText(r, "all").c_str());
            dataLogAppend(r);
            Guard g(mtx);
            state.sample = r;
            state.requested = false;
            lastSampleMs = now;
        }
        {
            Guard g(mtx);
            uint32_t since = millis() - lastSampleMs;
            state.nextSampleS = since >= interval ? 0 : (interval - since) / 1000;
        }
        taskFeed();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void samplerBegin() {
    mtx = xSemaphoreCreateMutex();
    // Pretend the last sample was one warm-up short of an interval ago: the
    // dust sensor wakes now and the first sample follows once it is warm.
    uint32_t interval = sampleIntervalMs();
    lastSampleMs = millis() - (interval > PMS_WARMUP_MS ? interval - PMS_WARMUP_MS : 0);
    startTask("sampler", samplerTask, 6144, 3);
}

void samplerRequestSample() {
    Guard g(mtx);
    state.requested = true;
}

SamplerState samplerState() {
    Guard g(mtx);
    return state;
}
