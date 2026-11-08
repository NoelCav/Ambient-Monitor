#include "config_portal.h"
#include "console.h"
#include "config.h"
#include "params.h"
#include "data_log.h"
#include "commands.h"
#include "health.h"
#include "status.h"
#include "web_assets.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

static WebServer server(80);

// HTML escape for safe insertion into text and (either-quoted) attribute values.
static String esc(const String& s) {
    String out;
    out.reserve(s.length());
    for (unsigned i = 0; i < s.length(); ++i) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

// The portal page is web/device.html (with web/common.css and common.js, shared
// with the online dashboard). scripts/embed_web.py turns them into
// src/web_assets.h at build time: DEVICE_HTML, COMMON_CSS, COMMON_JS.

static void handleRoot() {
    const Params& p = params();
    String html = FPSTR(DEVICE_HTML);
    html.replace("%DEVICE%", esc(p.deviceId));
    html.replace("%SAMPLE%", String(p.sampleIntervalS));
    html.replace("%BUILD%", String(buildTime()).substring(0, 16));
    server.send(200, "text/html", html);
}

static void handleSave() {
    Params p = params();  // start from current, override submitted fields

    if (server.hasArg("device_id")) p.deviceId        = server.arg("device_id");
    if (server.hasArg("sample_s")) p.sampleIntervalS = (uint32_t)server.arg("sample_s").toInt();

    paramsSave(p);

    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta http-equiv='refresh' content='5;url=/'></head><body>"
        "<h3>Saved. Rebooting&hellip;</h3><p>This page will return in a few seconds.</p>"
        "</body></html>");

    delay(500);     // let the response flush before reset
    ESP.restart();
}

static void handleStatus() {
    server.send(200, "application/json", statusJson());
}

// Stream a log file as a CSV download, named <device_id>-<name>.csv so
// downloads from several monitors don't overwrite each other.
static void sendCsv(const String& path, const String& name) {
    if (!dataLogMounted() || !LittleFS.exists(path)) {
        server.send(404, "text/plain", "No such log");
        return;
    }
    File f = LittleFS.open(path, FILE_READ);
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + params().deviceId + "-" + name + ".csv\"");
    server.streamFile(f, "text/csv");
    f.close();
}

// Run one console command and return only the output it produced.
static void handleCmd() {
    String c = server.arg("c");
    if (!c.length()) { server.send(400, "text/plain", "usage: /cmd?c=<command>\n"); return; }
    uint32_t pos = consolePos();
    commandRun(c);
    server.send(200, "text/plain", consoleSince(pos));
}

static void appendSaved(const String& name, size_t bytes, void* ctx) {
    String& j = *(String*)ctx;
    if (j.length() > 1) j += ",";
    j += "{\"name\":\"" + name + "\",\"kb\":" + String((bytes + 1023) / 1024) + "}";
}

// Saved WiFi networks (names only) and the current connection, for Settings.
static void handleWifiJson() {
    String j = "[";
    for (const WifiNet& w : params().wifi) j += (j.length() > 1 ? "," : "") + jsonStr(w.ssid);
    bool on = wifiConnected();
    server.send(200, "application/json", "{\"saved\":" + j + "],\"max\":" + String(WIFI_MAX) +
        ",\"connected\":" + jsonStr(on ? WiFi.SSID() : "") + ",\"rssi\":" + String(on ? WiFi.RSSI() : 0) + "}");
}

// Current log stats and the saved-log list, for the Data log section.
static void handleLogsJson() {
    String j = "[";
    dataLogForEachSaved(appendSaved, &j);
    j += "]";
    DataLogStats s = dataLogStats();
    server.send(200, "application/json",
        "{\"live_kb\":" + String((s.bytes + 1023) / 1024) + ",\"rows\":" + String(s.rows) +
        ",\"rows_left\":" + String(s.rowsLeft) + ",\"max_kb\":" + String(LOG_MAX_BYTES / 1024) +
        ",\"free_kb\":" + String(s.freeBytes / 1024) + ",\"saved\":" + j + "}");
}

static void handleSavedDownload() {
    String name = dataLogSanitizeName(server.arg("name"));
    sendCsv(dataLogSavedPath(name), name);
}

void configPortalSetup() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/status.json", HTTP_GET, handleStatus);
    // Shared with the dashboard. The page links them with ?v=<build>, so a
    // day's caching never serves a copy older than the firmware.
    server.on("/common.css", HTTP_GET, [] {
        server.sendHeader("Cache-Control", "max-age=86400");
        server.send_P(200, "text/css", COMMON_CSS);
    });
    server.on("/common.js", HTTP_GET, [] {
        server.sendHeader("Cache-Control", "max-age=86400");
        server.send_P(200, "application/javascript", COMMON_JS);
    });
    server.on("/save", HTTP_POST, handleSave);
    server.on("/console.txt", HTTP_GET, [] { server.send(200, "text/plain", consoleBacklog()); });
    server.on("/log.csv", HTTP_GET, [] { sendCsv(DATA_LOG_PATH, "log"); });
    server.on("/cmd", handleCmd);  // GET or POST
    server.on("/logs.json", HTTP_GET, handleLogsJson);
    server.on("/wifi.json", HTTP_GET, handleWifiJson);
    server.on("/saved.csv", HTTP_GET, handleSavedDownload);
    server.onNotFound(handleRoot);  // simple captive-portal-style fallback
    server.begin();
    Console.println("Config portal: HTTP server started on port 80");
}

void configPortalLoop() {
    server.handleClient();
}
