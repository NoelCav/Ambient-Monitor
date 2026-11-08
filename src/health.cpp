#include "health.h"
#include "build_info.h"
#include "config.h"
#include "console.h"
#include "sampler.h"
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

const char* resetReasonStr() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        case ESP_RST_USB:       return "usb";
        case ESP_RST_JTAG:      return "jtag";
        default:                return "unknown";
    }
}

// The Arduino core would otherwise mark new firmware good as soon as it
// starts, before setup() has run, so a crash at startup could never roll back.
extern "C" bool verifyRollbackLater() { return true; }

static esp_ota_img_states_t otaState() {
    esp_ota_img_states_t s = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(esp_ota_get_running_partition(), &s);
    return s;
}

const char* buildTime() { return BUILD_TIME; }

const char* firmwareState() {
    switch (otaState()) {
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending";
        case ESP_OTA_IMG_VALID: case ESP_OTA_IMG_UNDEFINED: return "ok";  // UNDEFINED: flashed over USB
        default: return "unknown";
    }
}

void healthLoop() {
    static bool done = false;
    if (done || millis() < HEALTHY_AFTER_S * 1000UL) return;
    if (otaState() != ESP_OTA_IMG_PENDING_VERIFY) {
        done = true;
        return;
    }
    bool reachable = WiFi.status() == WL_CONNECTED || (WiFi.getMode() & WIFI_AP);
    if (!reachable || !samplerState().live.at_ms) return;  // keep waiting
    esp_ota_mark_app_valid_cancel_rollback();
    Console.println("Firmware: healthy, marked good (no rollback)");
    done = true;
}
