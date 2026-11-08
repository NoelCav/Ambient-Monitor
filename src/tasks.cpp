#include "tasks.h"
#include "config.h"
#include "console.h"
#include <esp_task_wdt.h>

static const int MAX_TASKS = 8;
static TaskHandle_t handles[MAX_TASKS];
static int count = 0;

struct Start {
    void (*fn)(void*);
};

// Runs in the new task: subscribe it to the watchdog, then hand over.
static void trampoline(void* arg) {
    void (*fn)(void*) = ((Start*)arg)->fn;
    delete (Start*)arg;
    esp_task_wdt_add(nullptr);
    fn(nullptr);
}

void startTask(const char* name, void (*fn)(void*), uint32_t stackBytes, UBaseType_t prio) {
    TaskHandle_t h = nullptr;
    // ESP-IDF FreeRTOS takes the stack depth in bytes.
    if (xTaskCreatePinnedToCore(trampoline, name, stackBytes, new Start{fn}, prio, &h, 1) != pdPASS) {
        Console.printf("Tasks: could not start %s\n", name);
        return;
    }
    if (count < MAX_TASKS) handles[count++] = h;
}

void taskFeed() { esp_task_wdt_reset(); }
void taskWatchdogPause() { esp_task_wdt_delete(nullptr); }
void taskWatchdogResume() { esp_task_wdt_add(nullptr); }

void watchdogBegin() {
    esp_task_wdt_config_t cfg = {
        .timeout_ms = WATCHDOG_S * 1000,
        .idle_core_mask = 1 << 0,  // core 0 idle task: catches WiFi-core starvation
        .trigger_panic = true,     // reboot, and the reset reason says task_wdt
    };
    esp_task_wdt_reconfigure(&cfg);
    enableLoopWDT();  // loop() feeds it on every pass
    if (count < MAX_TASKS) handles[count++] = xTaskGetHandle("loopTask");
}

void tasksPrint(Print& out) {
    out.print("tasks (free stack):");
    for (int i = 0; i < count; i++) {
        if (handles[i]) out.printf(" %s %u", pcTaskGetName(handles[i]), (unsigned)uxTaskGetStackHighWaterMark(handles[i]));
    }
    out.println(" bytes");
}

String tasksJson() {
    String j = "[";
    for (int i = 0; i < count; i++) {
        if (!handles[i]) continue;
        if (j.length() > 1) j += ",";
        j += "{\"name\":\"" + String(pcTaskGetName(handles[i])) + "\",\"stack_free\":" +
             String((unsigned)uxTaskGetStackHighWaterMark(handles[i])) + "}";
    }
    return j + "]";
}
