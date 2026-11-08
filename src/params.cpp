#include "params.h"
#include "config.h"
#include "console.h"
#include <Preferences.h>

static Params g_params;
static Preferences prefs;

static const char* NVS_NAMESPACE = "ambient";

// Clamp/validate in place. Out-of-range values fall back to safe defaults.
static void validate(Params& p) {
    if (p.deviceId.length() == 0)     p.deviceId = DEVICE_ID;
    if (p.sampleIntervalS < 1)        p.sampleIntervalS = DEFAULT_SAMPLE_INTERVAL_S;
    if (p.sampleIntervalS > 86400)    p.sampleIntervalS = 86400;  // keeps ms math in range
    if (p.wifi.size() > WIFI_MAX)     p.wifi.resize(WIFI_MAX);
}

static String key(const char* prefix, size_t i) { return String(prefix) + i; }

Params& params() { return g_params; }

uint32_t sampleIntervalMs() { return g_params.sampleIntervalS * 1000UL; }

void paramsLoad() {
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    // WiFi networks: wifi_n entries as wifi_s<i> / wifi_p<i>. Before the list
    // existed there was one network (wifi_ssid / wifi_pass); a first boot has
    // neither and starts from secrets.h.
    g_params.wifi.clear();
    if (prefs.isKey("wifi_n")) {
        uint8_t n = prefs.getUChar("wifi_n", 0);
        for (uint8_t i = 0; i < n && i < WIFI_MAX; i++)
            g_params.wifi.push_back({prefs.getString(key("wifi_s", i).c_str(), ""), prefs.getString(key("wifi_p", i).c_str(), "")});
    } else {
        WifiNet w = {prefs.getString("wifi_ssid", WIFI_SSID), prefs.getString("wifi_pass", WIFI_PASSWORD)};
        if (w.ssid.length()) g_params.wifi.push_back(w);
    }
    g_params.deviceId        = prefs.getString("device_id", DEVICE_ID);
    g_params.sampleIntervalS = prefs.getUInt("sample_s", DEFAULT_SAMPLE_INTERVAL_S);
    g_params.testMode        = prefs.getBool("live_dbg", DEFAULT_TEST_MODE);  // key predates the name
    prefs.end();

    validate(g_params);

    String nets;
    for (const WifiNet& w : g_params.wifi) nets += (nets.length() ? ", " : "") + w.ssid;
    Console.printf("Params: device=%s wifi=[%s] sample=%us test=%s\n", g_params.deviceId.c_str(),
        nets.c_str(), (unsigned)g_params.sampleIntervalS, g_params.testMode ? "on" : "off");
}

void paramsSave(const Params& in) {
    Params p = in;
    validate(p);

    prefs.begin(NVS_NAMESPACE, false);  // read-write
    prefs.putUChar("wifi_n", p.wifi.size());
    for (size_t i = 0; i < WIFI_MAX; i++) {
        if (i < p.wifi.size()) {
            prefs.putString(key("wifi_s", i).c_str(), p.wifi[i].ssid);
            prefs.putString(key("wifi_p", i).c_str(), p.wifi[i].pass);
        } else {
            prefs.remove(key("wifi_s", i).c_str());
            prefs.remove(key("wifi_p", i).c_str());
        }
    }
    prefs.putString("device_id", p.deviceId);
    prefs.putUInt("sample_s", p.sampleIntervalS);
    prefs.putBool("live_dbg", p.testMode);
    // Keys from older firmware: the single WiFi network and the Firestore upload.
    for (const char* old : {"wifi_ssid", "wifi_pass", "upload_en", "batch"}) prefs.remove(old);
    prefs.end();

    g_params = p;
    Console.println("Params: saved to NVS");
}
