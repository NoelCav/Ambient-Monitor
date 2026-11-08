#pragma once
#include <Arduino.h>

// FreeRTOS helpers shared by the sensor tasks.
//
// Tasks (all on core 1; WiFi runs on core 0):
//   soundlight  prio 5  continuous ADC: sound RMS + light
//   dust        prio 4  PMS5003 UART + power pin
//   sampler     prio 3  schedule, readings, data log
//   led         prio 2  status LED colour and patterns
//   upload      prio 1  sends new log rows to the Google Sheet
//   loopTask    prio 1  Arduino loop(): WiFi, OTA, web portal, console
// Each sensor task owns its hardware and keeps its state behind one mutex; the
// others only call its functions. All tasks are watched by the task watchdog
// (WATCHDOG_S): a task that stops feeding it reboots the device ("task_wdt").

// Create a task pinned to core 1 and register it for the watchdog and the
// stack report. The task must call taskFeed() at least every WATCHDOG_S.
void startTask(const char* name, void (*fn)(void*), uint32_t stackBytes, UBaseType_t prio);
void taskFeed();
// Leave / rejoin the watchdog around a call that may legitimately block longer
// than WATCHDOG_S (an HTTPS upload). Only the calling task is affected.
void taskWatchdogPause();
void taskWatchdogResume();

// Configure the task watchdog and watch the Arduino loop task too.
void watchdogBegin();

// Free stack (bytes, low-water mark) per task, for `status` and status.json.
void tasksPrint(Print& out);
String tasksJson();

// Scoped mutex lock: { Guard g(mtx); ...shared state... }
struct Guard {
    explicit Guard(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
};
