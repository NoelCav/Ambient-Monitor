#include "wifi_manager.h"
#include "console.h"
#include "config.h"
#include "health.h"
#include "params.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiMulti.h>
#include <time.h>

static unsigned long lastReconnectAttempt = 0;
static const unsigned long RECONNECT_INTERVAL_MS = 120000;  // 2 min between retries
static bool wasDisconnected = false;
static bool ntpSynced = false;
static bool apActive = false;
static WiFiMulti multi;

static void checkNtpSync() {
    if (ntpSynced) return;
    if (timeSynced()) {
        ntpSynced = true;
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        Console.printf("NTP: time synced — %04d-%02d-%02d %02d:%02d:%02d\n",
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
        // OTA, found by scripts/ota.py and the Arduino IDE. The TXT records
        // say which monitor it is and what it runs.
        MDNS.addService("arduino", "tcp", 3232);
        MDNS.addServiceTxt("arduino", "tcp", "board", "esp32s3");
        MDNS.addServiceTxt("arduino", "tcp", "auth_upload", "yes");
        MDNS.addServiceTxt("arduino", "tcp", "device_id", params().deviceId.c_str());
        MDNS.addServiceTxt("arduino", "tcp", "name", deviceLabel(params()).c_str());
        MDNS.addServiceTxt("arduino", "tcp", "build", buildTime());
        MDNS.addService("telnet", "tcp", CONSOLE_TCP_PORT);
        Console.printf("mDNS: reachable at http://%s.local\n", host.c_str());
    } else {
        Console.println("mDNS: failed to start");
    }
}

// Start a SoftAP so the config portal stays reachable when we can't join WiFi.
static void startAp() {
    if (apActive) return;
    WiFi.mode(WIFI_AP_STA);  // keep STA so background reconnect can still succeed
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    apActive = true;
    Console.printf("WiFi: AP fallback active — SSID '%s', IP %s\n",
        AP_SSID, WiFi.softAPIP().toString().c_str());
}

// Over-the-air firmware updates on UDP/TCP 3232 (`pio run -e ota -t upload`).
// Listens on every interface, so it also works via the AP at 192.168.4.1.
// mDNS is ours (startMdns), so ArduinoOTA's own responder is disabled.
static void startOta() {
    ArduinoOTA.setHostname(mdnsHostname().c_str());
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setMdnsEnabled(false);
    // The upload blocks loop() for its whole duration, so the loop task leaves
    // the watchdog while it runs (and rejoins if it fails).
    ArduinoOTA.onStart([] {
        disableLoopWDT();
        Console.println("OTA: update starting");
    });
    ArduinoOTA.onProgress([](unsigned done, unsigned total) {
        static unsigned lastPct = 100;
        unsigned pct = total ? done * 100 / total : 0;
        if (pct / 10 != lastPct / 10) Console.printf("OTA: %u%%\n", pct);
        lastPct = pct;
    });
    ArduinoOTA.onEnd([] { Console.println("OTA: done, rebooting"); });
    ArduinoOTA.onError([](ota_error_t e) {
        Console.printf("OTA: error %u\n", (unsigned)e);
        enableLoopWDT();
    });
    ArduinoOTA.begin();
    Console.println("OTA: listening on port 3232");
}

// Saved networks: WiFiMulti scans and joins the strongest one in range.
static void loadNetworks() {
    multi.APlistClean();
    for (const WifiNet& w : params().wifi) multi.addAP(w.ssid.c_str(), w.pass.length() ? w.pass.c_str() : nullptr);
}

static void onConnected(const char* how) {
    Console.printf("WiFi: %s %s (%d dBm), IP %s\n", how, WiFi.SSID().c_str(), (int)WiFi.RSSI(),
        WiFi.localIP().toString().c_str());
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    MDNS.end();
    startMdns();  // (re)bind the responder to the new connection
    if (apActive) {  // back on a network: the setup AP is no longer needed
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        apActive = false;
        Console.println("WiFi: setup AP off");
    }
}

void wifiSetup() {
    // DHCP hostname must be set before the interface starts.
    WiFi.setHostname(mdnsHostname().c_str());
    WiFi.mode(WIFI_STA);
    loadNetworks();
    Console.printf("WiFi: looking for %u saved network(s)\n", (unsigned)params().wifi.size());
    if (multi.run(10000) == WL_CONNECTED) {
        onConnected("connected to");
    } else {
        Console.println("WiFi: no saved network in range; setup AP on, retrying every 2 min");
        startAp();
        wasDisconnected = true;
    }
    lastReconnectAttempt = millis();
    startOta();
}

void wifiLoop() {
    ArduinoOTA.handle();

    if (WiFi.status() == WL_CONNECTED) {
        if (wasDisconnected) {
            wasDisconnected = false;
            onConnected("reconnected to");
        }
        checkNtpSync();
        return;
    }

    if (!wasDisconnected) Console.println("WiFi: connection lost");
    wasDisconnected = true;
    if (!apActive) startAp();  // keep the portal reachable while offline

    // Rescan and join the strongest saved network. This blocks the loop task
    // for a few seconds; sensors and logging run in their own tasks.
    if (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
        multi.run(5000);
        lastReconnectAttempt = millis();
    }
}

bool wifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool timeSynced() {
    return time(nullptr) > 1704067200;  // after 2024-01-01: NTP has run
}

bool wifiAdd(const String& ssid, const String& pass, String& err) {
    if (!ssid.length() || ssid.length() > 32) { err = "SSID must be 1-32 characters"; return false; }
    if (pass.length() && (pass.length() < 8 || pass.length() > 63)) { err = "password must be 8-63 characters (or empty for an open network)"; return false; }
    Params p = params();
    bool updated = false;
    for (WifiNet& w : p.wifi) {
        if (w.ssid == ssid) {
            w.pass = pass;
            updated = true;
        }
    }
    if (!updated) {
        if (p.wifi.size() >= WIFI_MAX) { err = "list is full (" + String(WIFI_MAX) + "); remove one first"; return false; }
        p.wifi.push_back({ssid, pass});
    }
    paramsSave(p);
    loadNetworks();
    if (!wifiConnected()) lastReconnectAttempt = millis() - RECONNECT_INTERVAL_MS;  // try it now
    Console.printf("WiFi: %s '%s' (%u saved)\n", updated ? "updated" : "added", ssid.c_str(), (unsigned)p.wifi.size());
    return true;
}

bool wifiRemove(const String& ssid, String& err) {
    Params p = params();
    size_t before = p.wifi.size();
    for (size_t i = 0; i < p.wifi.size(); i++)
        if (p.wifi[i].ssid == ssid) p.wifi.erase(p.wifi.begin() + i--);
    if (p.wifi.size() == before) { err = "no saved network '" + ssid + "'"; return false; }
    if (p.wifi.empty()) { err = "it's the only saved network; add another first"; return false; }
    paramsSave(p);
    loadNetworks();
    Console.printf("WiFi: removed '%s' (%u saved)\n", ssid.c_str(), (unsigned)p.wifi.size());
    if (wifiConnected() && WiFi.SSID() == ssid) {  // was using it: switch to another
        WiFi.disconnect();
        lastReconnectAttempt = millis() - RECONNECT_INTERVAL_MS;
    }
    return true;
}
