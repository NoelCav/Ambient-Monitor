#include "wifi_manager.h"
#include "config.h"
#include "params.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>

static unsigned long lastReconnectAttempt = 0;
static const unsigned long RECONNECT_INTERVAL_MS = 120000;  // 2 min between retries
static bool wasDisconnected = false;
static bool ntpSynced = false;
static bool apActive = false;

static void checkNtpSync() {
    if (ntpSynced) return;
    time_t now;
    time(&now);
    if (now > 1704067200) {
        ntpSynced = true;
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        Serial.printf("NTP: time synced — %04d-%02d-%02d %02d:%02d:%02d\n",
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
}

// Sanitize device_id into a valid DNS label: lowercase [a-z0-9-], collapse and
// trim '-', fall back to "ambient-monitor" if empty.
static String mdnsHostname() {
    String src = params().deviceId;
    String out;
    for (unsigned i = 0; i < src.length(); ++i) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        char mapped = ok ? c : '-';
        // collapse consecutive '-'
        if (mapped == '-' && (out.length() == 0 || out[out.length() - 1] == '-')) continue;
        out += mapped;
    }
    while (out.length() > 0 && out[out.length() - 1] == '-') out.remove(out.length() - 1);
    if (out.length() == 0) out = "ambient-monitor";
    return out;
}

// (Re)start the mDNS responder bound to the device hostname.
static void startMdns() {
    String host = mdnsHostname();
    if (MDNS.begin(host.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS: reachable at http://%s.local\n", host.c_str());
    } else {
        Serial.println("mDNS: failed to start");
    }
}

// Start a SoftAP so the config portal stays reachable when we can't join WiFi.
static void startAp() {
    if (apActive) return;
    WiFi.mode(WIFI_AP_STA);  // keep STA so background reconnect can still succeed
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    apActive = true;
    Serial.printf("WiFi: AP fallback active — SSID '%s', IP %s\n",
        AP_SSID, WiFi.softAPIP().toString().c_str());
}

void wifiSetup() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(params().wifiSsid.c_str(), params().wifiPassword.c_str());
    Serial.printf("WiFi: connecting to %s", params().wifiSsid.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi: connected, IP %s\n", WiFi.localIP().toString().c_str());
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        Serial.println("NTP: time sync requested");
        startMdns();
    } else {
        Serial.println("WiFi: connection failed, starting AP fallback + background retry");
        startAp();
    }
}

void wifiLoop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (wasDisconnected) {
            wasDisconnected = false;
            Serial.printf("WiFi: reconnected, IP %s\n", WiFi.localIP().toString().c_str());
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            MDNS.end();
            startMdns();  // re-bind the responder after the drop
        }
        checkNtpSync();
        return;
    }

    wasDisconnected = true;
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
        lastReconnectAttempt = now;
        Serial.println("WiFi: attempting reconnection...");
        WiFi.reconnect();
    }

    // Ensure the config portal stays reachable while we're offline.
    if (!apActive) startAp();
}

bool wifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool wifiApMode() {
    return apActive;
}
