#include "dust.h"
#include "config.h"
#include "stats.h"
#include "tasks.h"

static SemaphoreHandle_t mtx;
static PmsParser parser;
static FrameAverage<PMS_AVG_FRAMES> avg;
static PmsFrame last = {};
static volatile bool wantOn = false;
static bool on = false;
static uint32_t onSince = 0, lastFrameMs = 0, intervalMs = 0;

// Apply a power request, then decode whatever the UART has received. Frames
// come about once a second and the UART buffer holds several, so a 50 ms
// poll never loses bytes.
static void dustPass() {
    uint32_t now = millis();
    Guard g(mtx);
    if (wantOn != on) {
        on = wantOn;
        digitalWrite(PIN_PMS_SET, on ? HIGH : LOW);
        onSince = now;
        avg.clear();
        lastFrameMs = intervalMs = 0;
    }
    while (Serial1.available()) {
        PmsFrame f;
        if (!parser.push((uint8_t)Serial1.read(), f) || !on) continue;
        avg.add(f);
        last = f;
        intervalMs = lastFrameMs ? now - lastFrameMs : 0;
        lastFrameMs = now;
    }
}

static void dustTask(void*) {
    for (;;) {
        dustPass();
        taskFeed();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void dustBegin() {
    mtx = xSemaphoreCreateMutex();
    pinMode(PIN_PMS_SET, OUTPUT);
    digitalWrite(PIN_PMS_SET, LOW);  // asleep until the sampler asks
    Serial1.setRxBufferSize(512);
    Serial1.begin(9600, SERIAL_8N1, PIN_PMS_RX, PIN_PMS_TX);
    startTask("dust", dustTask, 3072, 4);
}

void dustSetPower(bool want) { wantOn = want; }

DustStatus dustStatus() {
    DustStatus s;
    uint32_t now = millis();
    Guard g(mtx);
    s.on = on;
    s.awakeMs = on ? now - onSince : 0;
    s.fresh = on && lastFrameMs && now - lastFrameMs < 5000;
    s.frames = avg.count;
    s.avg = avg.average();
    s.last = last;
    s.intervalMs = intervalMs;
    s.totalFrames = parser.frames;
    s.totalErrors = parser.errors;
    s.totalBytes = parser.bytes;
    return s;
}
