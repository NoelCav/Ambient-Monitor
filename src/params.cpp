#include "params.h"
#include "config.h"
#include "console.h"
#include <Preferences.h>

static Params g_params;
static Preferences prefs;

static const char* NVS_NAMESPACE = "ambient";

// Clamp/validate in place. Out-of-range values fall back to safe defaults.
static void validate(Params& p) {
    if (p.wifiSsid.length() == 0)     p.wifiSsid = WIFI_SSID;
    if (p.deviceId.length() == 0)     p.deviceId = DEVICE_ID;
    if (p.sampleIntervalS < 1)        p.sampleIntervalS = DEFAULT_SAMPLE_INTERVAL_S;
    if (p.sampleIntervalS > 86400)    p.sampleIntervalS = 86400;  // keeps ms math in range
}

Params& params() { return g_params; }

uint32_t sampleIntervalMs() { return g_params.sampleIntervalS * 1000UL; }

void paramsLoad() {
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    g_params.wifiSsid        = prefs.getString("wifi_ssid", WIFI_SSID);
    g_params.wifiPassword    = prefs.getString("wifi_pass", WIFI_PASSWORD);
    g_params.deviceId        = prefs.getString("device_id", DEVICE_ID);
    g_params.sampleIntervalS = prefs.getUInt("sample_s", DEFAULT_SAMPLE_INTERVAL_S);
    g_params.testMode       = prefs.getBool("live_dbg", DEFAULT_TEST_MODE);  // key predates the name
    prefs.end();

    validate(g_params);

    Console.printf("Params: device=%s ssid=%s sample=%us test=%s\n",
        g_params.deviceId.c_str(), g_params.wifiSsid.c_str(),
        (unsigned)g_params.sampleIntervalS, g_params.testMode ? "on" : "off");
}

void paramsSave(const Params& in) {
    Params p = in;
    validate(p);

    prefs.begin(NVS_NAMESPACE, false);  // read-write
    prefs.putString("wifi_ssid", p.wifiSsid);
    prefs.putString("wifi_pass", p.wifiPassword);
    prefs.putString("device_id", p.deviceId);
    prefs.putUInt("sample_s", p.sampleIntervalS);
    prefs.putBool("live_dbg", p.testMode);
    // Keys from the removed Firestore upload path.
    prefs.remove("upload_en");
    prefs.remove("batch");
    prefs.end();

    g_params = p;
    Console.println("Params: saved to NVS");
}
