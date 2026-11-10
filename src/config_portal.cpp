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
#include <Update.h>

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
    html.replace("%NAME%", esc(deviceLabel(p)));
    html.replace("%NAMEVAL%", esc(p.name));
    html.replace("%SAMPLE%", String(p.sampleIntervalS));
    html.replace("%BUILD%", String(buildTime()).substring(0, 16));
    server.send(200, "text/html", html);
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

// What a save range would select, for the Save copy form's preview:
// /range.json?r=<range> (same syntax as `log save`) -> {rows, first, last}.
static void handleRangeJson() {
    LogRange r;
    String err;
    if (!dataLogParseRange(server.arg("r"), r, err)) {
        server.send(200, "application/json", "{\"error\":" + jsonStr(err) + "}");
        return;
    }
    LogSpan s = dataLogPreview(r);
    server.send(200, "application/json", "{\"rows\":" + String(s.rows) + ",\"first\":" + String(s.first) +
        ",\"last\":" + String(s.last) + "}");
}

// Firmware upload from the portal: POST /update?pw=<OTA password> with the
// .bin as multipart file. Same as an ArduinoOTA update: the new build boots
// on trial and rolls back if it isn't healthy (health.cpp). The request blocks
// loop() while the file streams in, so the loop task leaves the watchdog.
static String updateErr;

static void handleUpdateUpload() {
    HTTPUpload& u = server.upload();
    if (u.status == UPLOAD_FILE_START) {
        disableLoopWDT();  // re-enabled at END / ABORTED, which always follow
        updateErr = "";
        if (server.arg("pw") != OTA_PASSWORD) updateErr = "wrong password";
        else if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) updateErr = Update.errorString();
        else Console.printf("Update: receiving %s\n", u.filename.c_str());
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (!updateErr.length() && Update.write(u.buf, u.currentSize) != u.currentSize) updateErr = Update.errorString();
    } else if (u.status == UPLOAD_FILE_END) {
        if (!updateErr.length() && !Update.end(true)) updateErr = Update.errorString();
        enableLoopWDT();
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        if (!updateErr.length()) updateErr = "upload interrupted";
        enableLoopWDT();
    }
}

static void handleUpdateDone() {
    if (updateErr.length() || !Update.isFinished()) {
        if (Update.isRunning()) Update.abort();
        String e = updateErr.length() ? updateErr : String("no file received");
        Console.printf("Update failed: %s\n", e.c_str());
        server.send(updateErr == "wrong password" ? 403 : 400, "text/plain", "Update failed: " + e);
        return;
    }
    server.send(200, "text/plain", "Update written. Rebooting into the new build; it rolls back if it isn't healthy within a minute.");
    commandRun("reboot");
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
    server.on("/console.txt", HTTP_GET, [] { server.send(200, "text/plain", consoleBacklog()); });
    server.on("/log.csv", HTTP_GET, [] { sendCsv(DATA_LOG_PATH, "log"); });
    server.on("/cmd", handleCmd);  // GET or POST
    server.on("/logs.json", HTTP_GET, handleLogsJson);
    server.on("/wifi.json", HTTP_GET, handleWifiJson);
    server.on("/saved.csv", HTTP_GET, handleSavedDownload);
    server.on("/range.json", HTTP_GET, handleRangeJson);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.onNotFound(handleRoot);  // simple captive-portal-style fallback
    server.begin();
    Console.println("Config portal: HTTP server started on port 80");
}

void configPortalLoop() {
    server.handleClient();
}
