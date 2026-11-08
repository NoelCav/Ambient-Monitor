#include "led.h"
#include "air_quality.h"
#include "config.h"
#include "dust.h"
#include "light.h"
#include "params.h"
#include "sampler.h"
#include "tasks.h"
#include <WiFi.h>

static volatile uint32_t sampleFlashAt = 0;  // millis() of the last logged sample

struct Rgb { float r, g, b; };

// The portal's PM colours (GREEN, YEL, ORG, RED, PUR), by band index 0-4.
static const Rgb SCALE[] = {{0x16, 0xa3, 0x4a}, {0xca, 0x8a, 0x04}, {0xea, 0x58, 0x0c}, {0xdc, 0x26, 0x26}, {0x93, 0x33, 0xea}};

static Rgb airColour(const PmsFrame& d) {
    float x = airIndex(d);
    if (x >= 4) return SCALE[4];
    int i = (int)x;
    float f = x - i;
    return {SCALE[i].r + (SCALE[i + 1].r - SCALE[i].r) * f, SCALE[i].g + (SCALE[i + 1].g - SCALE[i].g) * f,
            SCALE[i].b + (SCALE[i + 1].b - SCALE[i].b) * f};
}

// Full-saturation colour at hue h (0-1): red -> yellow -> green -> cyan -> blue -> magenta.
static Rgb hue(float h) {
    float x = 6 * (h - floorf(h));
    auto clamp01 = [](float v) { return fminf(1, fmaxf(0, v)); };
    return {255 * clamp01(fabsf(x - 3) - 1), 255 * clamp01(2 - fabsf(x - 2)), 255 * clamp01(2 - fabsf(x - 4))};
}

static void show(Rgb c, float level) {  // level 0-1
    rgbLedWrite(RGB_BUILTIN, c.r * level, c.g * level, c.b * level);
}

static void ledTask(void*) {
    const uint32_t TICK_MS = 40;
    for (;;) {
        uint32_t now = millis();
        taskFeed();
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        if (!params().ledOn) {
            show({0, 0, 0}, 0);
            continue;
        }
        SamplerState s = samplerState();
        const Reading& live = s.live;
        // Brightness from the room light: ~2% in the dark, up to ~35% when bright.
        float lux = live.at_ms ? lightLux(live.light, params().ldrR10) : 50;
        float bright = fminf(0.35f, 0.02f + 0.33f * fminf(1, log10f(1 + lux) / 3));
        float breathe = 0.25f + 0.75f * (0.5f + 0.5f * sinf(now / 600.0f));

        if (now - sampleFlashAt < 600) {  // two white flashes
            bool on = (now - sampleFlashAt) / 150 % 2 == 0;
            show({255, 255, 255}, on ? bright : 0);
            continue;
        }
        if (!live.at_ms) {  // starting up: rainbow, one cycle every 3 s
            show(hue(now / 3000.0f), bright);
            continue;
        }
        if (WiFi.status() != WL_CONNECTED && now / 2000 % 2 == 0 && now % 2000 < 200) {  // offline blink
            show({255, 0, 255}, bright);
            continue;
        }
        // Colour from the freshest dust data: the test reading if the sensor
        // is awake, else the last sample; grey before any dust data.
        const Reading& r = live.dustOk ? live : s.sample;
        Rgb c = r.dustOk ? airColour(r.dust) : Rgb{0x80, 0x80, 0x80};
        DustStatus d = dustStatus();
        bool warming = d.on && d.awakeMs < PMS_WARMUP_MS && !params().testMode;
        show(c, warming ? bright * breathe : bright);
    }
}

void ledBegin() { startTask("led", ledTask, 3072, 2); }

void ledSampleTaken() { sampleFlashAt = millis(); }
