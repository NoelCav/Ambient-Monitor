#include "params.h"
#include "config.h"
#include <Preferences.h>

static Params g_params;
static Preferences prefs;

static const char* NVS_NAMESPACE = "ambient";

// Clamp/validate in place. Out-of-range values fall back to safe defaults.
static void validate(Params& p) {
    if (p.wifiSsid.length() == 0)     p.wifiSsid = WIFI_SSID;
    if (p.deviceId.length() == 0)     p.deviceId = DEVICE_ID;
    if (p.sampleIntervalS < 1)        p.sampleIntervalS = DEFAULT_SAMPLE_INTERVAL_S;
    if (p.batchSize < 1)              p.batchSize = 1;
    if (p.batchSize > QUEUE_SIZE)     p.batchSize = QUEUE_SIZE;
}

Params& params() { return g_params; }

uint32_t sampleIntervalMs() { return g_params.sampleIntervalS * 1000UL; }

void paramsLoad() {
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    g_params.wifiSsid        = prefs.getString("wifi_ssid", WIFI_SSID);
    g_params.wifiPassword    = prefs.getString("wifi_pass", WIFI_PASSWORD);
    g_params.deviceId        = prefs.getString("device_id", DEVICE_ID);
    g_params.uploadEnabled   = prefs.getBool("upload_en", DEFAULT_UPLOAD_ENABLED);
    g_params.sampleIntervalS = prefs.getUInt("sample_s", DEFAULT_SAMPLE_INTERVAL_S);
    g_params.batchSize       = prefs.getUShort("batch", DEFAULT_BATCH_SIZE);
    prefs.end();

    validate(g_params);

    Serial.printf("Params: device=%s ssid=%s upload=%s sample=%us batch=%u\n",
        g_params.deviceId.c_str(), g_params.wifiSsid.c_str(),
        g_params.uploadEnabled ? "on" : "off",
        g_params.sampleIntervalS, g_params.batchSize);
}

void paramsSave(const Params& in) {
    Params p = in;
    validate(p);

    prefs.begin(NVS_NAMESPACE, false);  // read-write
    prefs.putString("wifi_ssid", p.wifiSsid);
    prefs.putString("wifi_pass", p.wifiPassword);
    prefs.putString("device_id", p.deviceId);
    prefs.putBool("upload_en", p.uploadEnabled);
    prefs.putUInt("sample_s", p.sampleIntervalS);
    prefs.putUShort("batch", p.batchSize);
    prefs.end();

    g_params = p;
    Serial.println("Params: saved to NVS");
}
