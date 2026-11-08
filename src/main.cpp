// Ambient Monitor: ESP32-S3 environmental logger. Wiring only; see tasks.h
// for the task layout and each module's header for what it owns.
#include <Arduino.h>
#include "climate.h"
#include "commands.h"
#include "config_portal.h"
#include "console.h"
#include "data_log.h"
#include "dust.h"
#include "health.h"
#include "led.h"
#include "params.h"
#include "sampler.h"
#include "soundlight.h"
#include "tasks.h"
#include "wifi_manager.h"

void setup() {
    Serial.begin(115200);
    delay(100);
    Console.printf("Ambient Monitor starting. Reset reason: %s (build %s %s)\n",
        resetReasonStr(), __DATE__, __TIME__);

    paramsLoad();
    dataLogSetup();
    climateBegin();
    soundLightBegin();
    dustBegin();
    samplerBegin();
    ledBegin();

    wifiSetup();
    configPortalSetup();
    commandsSetup();
    watchdogBegin();
}

// The Arduino loop task: network services only. Sensors run in their own tasks.
void loop() {
    wifiLoop();
    configPortalLoop();
    consoleLoop();
    commandsLoop();
    healthLoop();
    delay(2);
}
