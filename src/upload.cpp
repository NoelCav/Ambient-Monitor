#include "upload.h"
#include "config.h"
#include "console.h"
#include "data_log.h"
#include "params.h"
#include "tasks.h"
#include "wifi_manager.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

static SemaphoreHandle_t mtx;
static UploadStatus st;
static volatile bool tryNow = false;
static uint32_t backoffMs = 0, nextTryMs = 0;

static void saveLastEpoch(long e) {
    Preferences p;
    p.begin("upload", false);
    p.putLong("last", e);
    p.end();
}

static String urlEncode(const String& s) {
    String out;
    char hex[4];
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((uint8_t)c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else {
            snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
            out += hex;
        }
    }
    return out;
}

// One POST. Returns "" on success, else a short reason.
static String post(const String& csv) {
    const Params& p = params();
    // r10 (the light calibration) lets the dashboard show the same lux as the device.
    String url = p.uploadUrl + (p.uploadUrl.indexOf('?') < 0 ? "?" : "&") + "key=" + urlEncode(p.uploadKey) +
                 "&r10=" + String(p.ldrR10, 0);
    WiFiClientSecure tls;
    tls.setInsecure();  // no certificate check: Google rotates certificates, and the data isn't secret
    HTTPClient http;
    // Apps Script answers a POST with a redirect to the response; following it
    // (as a GET) returns what doPost produced.
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(10000);
    http.setTimeout(20000);
    if (!http.begin(tls, url)) return "bad URL";
    http.addHeader("Content-Type", "text/csv");
    taskWatchdogPause();  // TLS + Google can take longer than WATCHDOG_S
    int code = http.POST(csv);
    String body = code > 0 ? http.getString() : "";
    taskWatchdogResume();
    http.end();
    if (code != 200) return "HTTP " + String(code);
    if (body.indexOf("\"ok\":true") < 0) {
        body.replace("\r", " ");
        body.replace("\n", " ");
        while (body.indexOf("  ") >= 0) body.replace("  ", " ");
        return "server: " + body.substring(0, 60);
    }
    return "";
}

static void pass() {
    const Params& p = params();
    bool configured = p.uploadOn && p.uploadUrl.length() && p.uploadKey.length();
    {
        Guard g(mtx);
        st.configured = configured;
    }
    if (!configured || !wifiConnected() || !timeSynced()) return;
    if (!tryNow && (int32_t)(millis() - nextTryMs) < 0) return;
    tryNow = false;

    long after, last;
    size_t pending;
    {
        Guard g(mtx);
        after = st.lastEpoch;
    }
    String csv = dataLogRowsAfter(after, UPLOAD_BATCH, last, pending);
    {
        Guard g(mtx);
        st.pending = pending;
    }
    if (!csv.length()) {
        nextTryMs = millis() + 30000;  // nothing new; look again soon
        return;
    }
    String err = post(csv);
    Guard g(mtx);
    if (err.length()) {
        backoffMs = backoffMs ? min(backoffMs * 2, 30UL * 60000) : 60000;
        nextTryMs = millis() + backoffMs;
        if (st.lastError != err) Console.printf("Upload: failed (%s); retrying in %lu min\n", err.c_str(), backoffMs / 60000);
        st.lastError = err;
        return;
    }
    size_t sent = pending < UPLOAD_BATCH ? pending : UPLOAD_BATCH;
    st.lastEpoch = last;
    st.pending = pending - sent;
    st.lastOkMs = millis();
    if (st.lastError.length()) Console.println("Upload: working again");
    st.lastError = "";
    backoffMs = 0;
    nextTryMs = st.pending ? millis() : millis() + 30000;  // more waiting: keep going
    saveLastEpoch(last);
}

static void uploadTask(void*) {
    for (;;) {
        pass();
        taskFeed();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void uploadBegin() {
    mtx = xSemaphoreCreateMutex();
    Preferences p;
    p.begin("upload", true);
    st.lastEpoch = p.getLong("last", 0);
    p.end();
    startTask("upload", uploadTask, 8192, 1);
}

void uploadNow() { tryNow = true; }

void uploadResend() {
    Guard g(mtx);
    st.lastEpoch = 0;
    saveLastEpoch(0);
    tryNow = true;
}

UploadStatus uploadStatus() {
    Guard g(mtx);
    return st;
}
