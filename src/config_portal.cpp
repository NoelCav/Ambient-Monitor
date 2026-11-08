#include "config_portal.h"
#include "console.h"
#include "config.h"
#include "params.h"
#include "data_log.h"
#include <WebServer.h>
#include <LittleFS.h>

static WebServer server(80);

extern String statusJson();  // defined in main.cpp

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

// Portal home page: plain tables. Readings come from /status.json (polled
// every 3 s); only the settings form values are
// substituted server-side (%DEVICE%, %SSID%, %SAMPLE%, %LIVECHK%, %TCPPORT%).
static const char ROOT_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%DEVICE%</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;font-size:14px;margin:0;background:#fff;color:#000}
#top{background:#335;color:#fff;padding:8px 12px;font-size:16px}
#top span{font-size:12px;color:#ccd;margin-left:10px}
#wrap{max-width:720px;padding:4px 12px 20px}
h3{background:#e4e4ec;margin:16px 0 0;padding:4px 6px;font-size:14px}
table{border-collapse:collapse;width:100%}
td{padding:3px 6px;border-bottom:1px solid #ddd;vertical-align:top}
td.k{width:38%;color:#444}
td.n{text-align:right}
.note{color:#666;font-size:12px;margin:4px 6px}
input[type=text],input[type=password],input[type=number]{width:100%;max-width:260px;box-sizing:border-box}
</style></head><body>
<div id="top">%DEVICE% <span id="upd">loading...</span></div>
<div id="wrap">

<h3>Readings</h3>
<table>
<tr><td class="k">Temperature</td><td id="t">-</td></tr>
<tr><td class="k">Humidity</td><td id="h">-</td></tr>
<tr><td class="k">Pressure</td><td id="p">-</td></tr>
<tr><td class="k">PM1.0 / PM2.5 / PM10</td><td id="pm">-</td></tr>
<tr><td class="k">Sound (peak-to-peak)</td><td id="snd">-</td></tr>
<tr><td class="k">Light (0-4095)</td><td id="lt">-</td></tr>
</table>
<div class="note" id="src"></div>

<div id="test" style="display:none">
<h3>Test mode (live, updated every 5 s)</h3>
<table>
<tr><td class="k">PMS awake for</td><td id="pt">-</td></tr>
<tr><td class="k">PM atmospheric (1.0 / 2.5 / 10)</td><td id="pa">-</td></tr>
<tr><td class="k">PM CF=1 (1.0 / 2.5 / 10)</td><td id="pc">-</td></tr>
<tr><td class="k">Particles per 0.1 L &gt;0.3 / 0.5 / 1.0 &micro;m</td><td id="n1">-</td></tr>
<tr><td class="k">Particles per 0.1 L &gt;2.5 / 5 / 10 &micro;m</td><td id="n2">-</td></tr>
<tr><td class="k">Frame interval</td><td id="dt">-</td></tr>
<tr><td class="k">Frames / checksum errors (last 5 s)</td><td id="fr">-</td></tr>
<tr><td class="k">Loudest 1 s in last 5 s</td><td id="mx">-</td></tr>
</table>
</div>

<h3>System</h3>
<table>
<tr><td class="k">Uptime</td><td id="up">-</td></tr>
<tr><td class="k">Last reset reason</td><td id="rr">-</td></tr>
<tr><td class="k">WiFi signal</td><td id="ws">-</td></tr>
<tr><td class="k">Free memory</td><td id="hp">-</td></tr>
<tr><td class="k">Mode</td><td id="md">-</td></tr>
<tr><td class="k">Next sample</td><td id="nx">-</td></tr>
<tr><td class="k">Data log</td><td id="lg">-</td></tr>
</table>
<p class="note"><a href="/log.csv">Download log</a> | <a href="/log.old.csv">Download older log</a> |
<a href="/console">Console</a> (also TCP port %TCPPORT%)</p>
<form method="POST" action="/log/clear" onsubmit="return confirm('Delete the whole data log?')" style="margin:0 6px">
<input type="submit" value="Clear log"></form>

<h3>Settings</h3>
<form method="POST" action="/save">
<table>
<tr><td class="k">Device ID</td><td><input type="text" name="device_id" value="%DEVICE%"></td></tr>
<tr><td class="k">WiFi SSID</td><td><input type="text" name="wifi_ssid" value="%SSID%"></td></tr>
<tr><td class="k">WiFi password</td><td><input type="password" name="wifi_pass" placeholder="unchanged"></td></tr>
<tr><td class="k">Sample interval (s)</td><td><input type="number" name="sample_s" min="1" max="86400" value="%SAMPLE%"></td></tr>
<tr><td class="k">Test mode</td><td><input type="checkbox" name="live_dbg" %LIVECHK%>
 PMS always on, live diagnostics every 5 s</td></tr>
</table>
<p style="margin:6px"><input type="submit" value="Save and reboot"></p>
</form>

</div><script>
function $(i){return document.getElementById(i)}
function set(i,v){$(i).textContent=v}
function dur(s){return s<120?s+' s':s<7200?Math.round(s/60)+' min':(s/3600).toFixed(1)+' h'}
function pmLvl(v){return v<=12?'Good':v<=35?'Moderate':v<=55?'Unhealthy for sensitive groups':v<=150?'Unhealthy':v<=250?'Very unhealthy':'Hazardous'}
function lightLvl(v){return v>=4095?'saturated':v>3000?'bright':v>2000?'bright indoor':v>1000?'indoor lighting':v>400?'dim':'dark'}
function render(d){
  var s=d.sys,L=d.live,b=d.bme,p=d.pms;
  set('upd','updated '+new Date().toLocaleTimeString());
  // In test mode show the live snapshot (<=5 s old); otherwise the last sample.
  var e=L&&L.bme.ok?{t:L.bme.temp,h:L.bme.hum,p:L.bme.pres}:(b.valid?{t:b.temp,h:b.hum,p:b.pres}:null);
  if(e){set('t',e.t.toFixed(1)+' °C');set('h',e.h.toFixed(1)+' %');set('p',e.p.toFixed(1)+' hPa');}
  else{var m=L&&L.bme.found?'BME measurement failed':(L?'BME not found':'no sample yet');set('t',m);set('h','-');set('p','-');}
  var pm=L&&L.pms.ok?L.pms.atm:(p.valid?[p.pm1,p.pm25,p.pm10]:null);
  set('pm',pm?pm[0]+' / '+pm[1]+' / '+pm[2]+' µg/m³  ('+pmLvl(pm[1])+')'
    :(s.live_debug?'waiting for PMS frames':'PMS asleep between samples'));
  var have=L||d.have_sample;
  set('snd',have?(L?L.sound.p2p:d.sound.p2p)+'  (quiet ~10, loud clap 500+)':'-');
  var lt=L?L.light:d.light.adc;set('lt',have?lt+'  ('+lightLvl(lt)+')':'-');
  set('src',L?'Live values from test mode.':(d.have_sample?'From the last sample, '+dur(d.age_s)+' ago.':'No sample yet.'));
  $('test').style.display=L?'':'none';
  if(L){var P=L.pms;
    set('pt',dur(P.t_s)+(P.t_s<30?'  (warming up, stable after 30 s)':''));
    set('pa',P.atm.join(' / '));set('pc',P.cf1.join(' / '));
    set('n1',P.cnt.slice(0,3).join(' / '));set('n2',P.cnt.slice(3).join(' / '));
    set('dt',P.ok?P.dt_ms+' ms':'no frames ('+P.bytes+' bytes received)');
    set('fr',P.frames+' / '+P.bad);set('mx',L.sound.max5s);}
  set('up',dur(s.uptime_s));set('rr',s.reset);set('ws',s.rssi+' dBm');
  set('hp',Math.round(s.heap/1024)+' KB');
  set('md',s.live_debug?'Test mode':'Normal (PMS duty-cycled)');
  set('nx','in '+dur(s.next_sample_s)+' (every '+dur(s.sample_s)+')');
  set('lg',s.log_kb+' KB');
}
function tick(){fetch('/status.json').then(function(r){return r.json()}).then(render)
  .catch(function(){set('upd','not responding, retrying...')})}
tick();setInterval(tick,3000);
</script></body></html>)HTML";

static void handleRoot() {
    const Params& p = params();
    String html = FPSTR(ROOT_PAGE);
    html.replace("%DEVICE%", esc(p.deviceId));
    html.replace("%SSID%", esc(p.wifiSsid));
    html.replace("%SAMPLE%", String(p.sampleIntervalS));
    html.replace("%LIVECHK%", p.liveDebug ? "checked" : "");
    html.replace("%TCPPORT%", String(CONSOLE_TCP_PORT));
    server.send(200, "text/html", html);
}

static void handleSave() {
    Params p = params();  // start from current, override submitted fields

    if (server.hasArg("device_id")) p.deviceId        = server.arg("device_id");
    if (server.hasArg("wifi_ssid")) p.wifiSsid         = server.arg("wifi_ssid");
    // Only overwrite the password when a new one was provided.
    if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length() > 0)
        p.wifiPassword = server.arg("wifi_pass");
    p.liveDebug = server.hasArg("live_dbg");  // checkbox: present only when checked
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

// Stream a log file as a CSV download. The file name carries the device ID so
// downloads from several monitors don't overwrite each other.
static void sendLogFile(const char* path, const char* suffix) {
    if (!dataLogMounted() || !LittleFS.exists(path)) {
        server.send(404, "text/plain", "No log file yet");
        return;
    }
    File f = LittleFS.open(path, FILE_READ);
    server.sendHeader("Content-Disposition",
        "attachment; filename=\"" + params().deviceId + suffix + ".csv\"");
    server.streamFile(f, "text/csv");
    f.close();
}

// Console page: polls /console.txt and keeps the view pinned to the bottom
// unless the user has scrolled up to read.
static void handleConsolePage() {
    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Console</title><style>body{margin:0;font-family:sans-serif}"
        "header{padding:8px 12px;background:#222;color:#eee;font-size:14px}"
        "header a{color:#9cf}pre{margin:0;padding:8px 12px;font-size:12px;"
        "white-space:pre-wrap;word-break:break-all}</style></head><body>"
        "<header><a href='/'>&larr; back</a> &middot; console "
        "<span id='st'></span></header><pre id='c'></pre><script>"
        "var c=document.getElementById('c'),st=document.getElementById('st');"
        "function t(){fetch('/console.txt').then(function(r){return r.text();})"
        ".then(function(x){var b=(innerHeight+scrollY)>=document.body.scrollHeight-40;"
        "c.textContent=x;st.textContent='';if(b)scrollTo(0,document.body.scrollHeight);})"
        ".catch(function(){st.textContent='(reconnecting\\u2026)';});}"
        "t();setInterval(t,2000);</script></body></html>");
}

static void handleLogClear() {
    dataLogClear();
    server.sendHeader("Location", "/");
    server.send(303);
}

void configPortalSetup() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/status.json", HTTP_GET, handleStatus);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/console", HTTP_GET, handleConsolePage);
    server.on("/console.txt", HTTP_GET, [] { server.send(200, "text/plain", consoleBacklog()); });
    server.on("/log.csv", HTTP_GET, [] { sendLogFile(DATA_LOG_PATH, "-log"); });
    server.on("/log.old.csv", HTTP_GET, [] { sendLogFile(DATA_LOG_OLD_PATH, "-log-old"); });
    server.on("/log/clear", HTTP_POST, handleLogClear);
    server.onNotFound(handleRoot);  // simple captive-portal-style fallback
    server.begin();
    Console.println("Config portal: HTTP server started on port 80");
}

void configPortalLoop() {
    server.handleClient();
}
