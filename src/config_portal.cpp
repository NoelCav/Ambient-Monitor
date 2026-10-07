#include "config_portal.h"
#include "params.h"
#include <WebServer.h>

static WebServer server(80);

extern String statusJson();  // defined in main.cpp

// HTML escape for safe insertion into attribute values.
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
            default:   out += c;        break;
        }
    }
    return out;
}

static void handleRoot() {
    const Params& p = params();
    String checked = p.uploadEnabled ? "checked" : "";

    String html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Ambient Monitor Config</title>"
        "<style>body{font-family:sans-serif;max-width:480px;margin:24px auto;padding:0 16px}"
        "label{display:block;margin:14px 0 4px;font-weight:600}"
        "input[type=text],input[type=password],input[type=number]{width:100%;padding:8px;"
        "box-sizing:border-box;font-size:16px}"
        "button{margin-top:20px;padding:12px 20px;font-size:16px}"
        ".hint{color:#666;font-size:13px;font-weight:400}"
        ".card{border:1px solid #ddd;border-radius:8px;padding:12px 16px;margin:12px 0;"
        "background:#fafafa}.card h3{margin:0 0 8px}.card .row{margin:4px 0}"
        ".card .val{font-weight:600}.muted{color:#888;font-size:13px}</style></head><body>"
        "<h2>Ambient Monitor</h2>"

        "<div id='status' class='card'>Loading current reading&hellip;</div>"

        "<form method='POST' action='/save'>"

        "<label>Device ID</label>"
        "<input type='text' name='device_id' value='" + esc(p.deviceId) + "'>"

        "<label>WiFi SSID</label>"
        "<input type='text' name='wifi_ssid' value='" + esc(p.wifiSsid) + "'>"

        "<label>WiFi Password <span class='hint'>(leave blank to keep current)</span></label>"
        "<input type='password' name='wifi_pass' value='' placeholder='unchanged'>"

        "<label><input type='checkbox' name='upload_en' " + checked + "> Upload to Firestore</label>"
        "<span class='hint'>Unchecked = serial-only; WiFi and offline buffer stay active.</span>"

        "<label>Sample interval (seconds)</label>"
        "<input type='number' name='sample_s' min='1' value='" + String(p.sampleIntervalS) + "'>"

        "<label>Upload batch size (samples per batch)</label>"
        "<input type='number' name='batch' min='1' value='" + String(p.batchSize) + "'>"

        "<button type='submit'>Save &amp; Reboot</button></form>"

        "<script>"
        "function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML;}"
        "function row(label,val,lvl){"
        "var l=lvl?(' <span class=\"muted\">'+esc(lvl)+'</span>'):'';"
        "return '<div class=\"row\">'+label+': <span class=\"val\">'+esc(val)+'</span>'+l+'</div>';}"
        "function render(d){"
        "var h='<h3>Current reading</h3>';"
        "if(!d.have_sample){h+='<div class=\"muted\">Waiting for first sample&hellip;</div>';"
        "document.getElementById('status').innerHTML=h;return;}"
        "var b=d.bme;"
        "if(b.valid){h+=row('Temp',b.temp.toFixed(1)+' &deg;C',b.tempLvl);"
        "h+=row('Humidity',b.hum.toFixed(1)+' %',b.humLvl);"
        "h+=row('Pressure',b.pres.toFixed(1)+' hPa',b.presLvl);}"
        "else{h+='<div class=\"row muted\">BME: no valid reading</div>';}"
        "h+=row('Sound',d.sound.avg+' (peak '+d.sound.peak+')',d.sound.lvl);"
        "h+=row('Light',d.light.adc,d.light.lvl);"
        "var p=d.pms;"
        "if(p.valid){h+=row('PM1.0',p.pm1+' &micro;g/m&sup3;',p.pm1Lvl);"
        "h+=row('PM2.5',p.pm25+' &micro;g/m&sup3;',p.pm25Lvl);"
        "h+=row('PM10',p.pm10+' &micro;g/m&sup3;',p.pm10Lvl);}"
        "else{h+='<div class=\"row muted\">PMS: no data</div>';}"
        "h+='<div class=\"muted\" id=\"age\">updated '+d.age_s+'s ago</div>';"
        "document.getElementById('status').innerHTML=h;}"
        "function tick(){fetch('/status.json').then(function(r){return r.json();})"
        ".then(render).catch(function(){var a=document.getElementById('age');"
        "if(a)a.textContent='reconnecting\\u2026';});}"
        "tick();setInterval(tick,3000);"
        "</script>"

        "</body></html>";

    server.send(200, "text/html", html);
}

static void handleSave() {
    Params p = params();  // start from current, override submitted fields

    if (server.hasArg("device_id")) p.deviceId        = server.arg("device_id");
    if (server.hasArg("wifi_ssid")) p.wifiSsid         = server.arg("wifi_ssid");
    // Only overwrite the password when a new one was provided.
    if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length() > 0)
        p.wifiPassword = server.arg("wifi_pass");
    p.uploadEnabled = server.hasArg("upload_en");  // checkbox: present only when checked
    if (server.hasArg("sample_s")) p.sampleIntervalS = (uint32_t)server.arg("sample_s").toInt();
    if (server.hasArg("batch"))    p.batchSize        = (uint16_t)server.arg("batch").toInt();

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

void configPortalSetup() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/status.json", HTTP_GET, handleStatus);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleRoot);  // simple captive-portal-style fallback
    server.begin();
    Serial.println("Config portal: HTTP server started on port 80");
}

void configPortalLoop() {
    server.handleClient();
}
