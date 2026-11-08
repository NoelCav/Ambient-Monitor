#include "config_portal.h"
#include "console.h"
#include "config.h"
#include "params.h"
#include "data_log.h"
#include "commands.h"
#include "status.h"
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

// Portal home page: one centered column, large text. Each section has a grey
// header bar that collapses it (+/-); the Test readings bar also carries the
// test-mode switch, which works independently of collapsing. Readings are
// tiles, three per row, outlined with a colour gradient when the value has a
// good/bad range. "Last sample" is what was written to the log (before the
// first one: the current readings, under a warning); "Test readings" is the
// same tiles over the last 5 s. Values come from /status.json (polled every
// 3 s); every action goes through /cmd (the console commands), so the page
// and the console never diverge. Only the settings form is filled in
// server-side.
static const char ROOT_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ambient Monitor</title>
<style>
:root{--fg:#1a1a1a;--bg:#fff;--mut:#666;--bar:#eef0f3;--line:#ddd;--in:#fff;--pre:#f7f7f7}
@media (prefers-color-scheme:dark){:root{--fg:#e6e6e6;--bg:#16181b;--mut:#9aa0a6;--bar:#262a2f;--line:#3a3f45;--in:#1e2125;--pre:#1e2125}}
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;font-size:17px;max-width:520px;margin:20px auto;padding:0 16px;color:var(--fg);background:var(--bg)}
h1{font-size:26px;margin:0;text-align:center}
.sub{text-align:center;color:var(--mut);font-size:14px;margin:2px 0 16px}
.sec{display:flex;justify-content:space-between;align-items:center;background:var(--bar);padding:12px 16px;margin:18px 0 0;font-size:20px;font-weight:700;border-radius:6px;cursor:pointer;user-select:none}
.sec .pm{font-size:24px;font-weight:400;color:var(--mut);width:18px;text-align:center;margin-left:12px}
.r{display:flex;align-items:center}
.lb{border:1px solid var(--line);border-radius:6px;padding:10px 12px;margin:10px 0 0}
.lh{display:flex;justify-content:space-between;align-items:baseline;gap:8px;flex-wrap:wrap}
.lbt{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}.lbt button,.lbt a.b{margin:0;padding:7px 12px;font-size:14px}
a.b{display:inline-block;border:1px solid var(--line);border-radius:6px;background:var(--bar);color:var(--fg);text-decoration:none}
.ch{margin-top:14px}.chh{display:flex;justify-content:space-between;align-items:baseline;gap:8px;font-size:15px}.chh span{font-size:13px;color:var(--mut)}
.chr{display:flex;justify-content:space-between;gap:8px;font-size:12px;color:var(--mut)}
.ch svg{width:100%;height:90px;display:block;border:1px solid var(--line);border-radius:4px;background:var(--in);touch-action:pan-y}
.hl{stroke:var(--mut);stroke-width:1}.chx{display:flex;justify-content:space-between;font-size:12px;color:var(--mut);margin-top:4px}
.muted{color:var(--mut);font-size:14px;margin:8px 4px}
h3{font-size:15px;font-weight:600;color:var(--mut);margin:14px 4px 0}
.g{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:8px}
.t{border:2px solid var(--line);border-radius:6px;padding:9px 5px;text-align:center}
.t .l{font-size:13px;color:var(--mut)}.t .v{font-size:22px;font-weight:650;margin:2px 0}.t .v small{font-size:13px;font-weight:400;color:var(--mut)}
.t .m{font-size:12px;color:var(--mut)}
.swi{position:relative;width:52px;height:30px;border-radius:15px;display:inline-block;background:#b0b4ba;cursor:pointer}
.swi:after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#fff;transition:left .15s}
.swi.on{background:#2e7d32}.swi.on:after{left:25px}
.lab{font-size:15px;font-weight:600;color:var(--mut);margin-right:8px}
.row{display:flex;justify-content:space-between;align-items:center;padding:8px 4px;border-bottom:1px solid var(--line)}
.msg{background:var(--bar);border-radius:6px;padding:10px 12px;margin:10px 0 0;font-size:15px}
label{display:block;margin:16px 0 4px;font-weight:600}
input,select{width:100%;padding:9px;box-sizing:border-box;font-size:16px;background:var(--in);color:var(--fg);border:1px solid var(--line);border-radius:4px}
button{padding:10px 18px;font-size:16px;margin:12px 8px 0 0;border-radius:6px;border:1px solid var(--line);background:var(--bar);color:var(--fg);cursor:pointer}
button.pri{background:#2563eb;border-color:#2563eb;color:#fff}
button.del{color:#b91c1c}
a{color:#2563eb}
pre{background:var(--pre);border:1px solid var(--line);border-radius:6px;padding:8px;height:45vh;overflow:auto;font-size:13px;white-space:pre-wrap;word-break:break-all;margin:10px 0 0}
.line{display:flex;gap:8px;align-items:center}.line input{flex:1}.line button{margin:0}
.hide{display:none}
.warn{background:#fef3c7;color:#713f12;border:1px solid #ca8a04;border-radius:6px;padding:10px 12px;margin:10px 0 0;font-size:15px}
</style></head><body>
<h1>Ambient Monitor</h1><div class="sub">%DEVICE% &middot; <span id="upd">connecting&hellip;</span></div>

<div class="sec" onclick="tog('cur')">Last sample<span class="pm" id="pm-cur"></span></div>
<div id="cur"><div class="warn hide" id="cwarn"></div><div id="curb"></div><div class="muted" id="src"></div></div>

<div class="sec" onclick="tog('tst')">Test readings<span class="r"><span class="lab" id="tlab">Off</span>
<i class="swi" id="tsw" onclick="event.stopPropagation();setTest(!testOn)"></i><span class="pm" id="pm-tst"></span></span></div>
<div id="tst"><div class="msg hide" id="tmsg"></div>
<div id="tstb"></div></div>

<div class="sec" id="gr-sec" onclick="tog('gr')">Graphs<span class="pm" id="pm-gr"></span></div>
<div id="gr-b"><div style="margin-top:10px"><select id="gsrc" onchange="graph()"></select></div>
<div class="muted" id="gmsg"></div><div id="charts"></div></div>

<div class="sec" onclick="tog('dia')">Sensor diagnostics<span class="pm" id="pm-dia"></span></div>
<div id="dia-b"></div>

<div class="sec" onclick="tog('dev')">Device details<span class="pm" id="pm-dev"></span></div>
<div id="dev-b"><div id="devb"></div>
<button type="button" class="pri" onclick="sampleNow()">Sample now</button><button type="button" onclick="reboot()">Reboot</button>
<div class="msg hide" id="dmsg"></div></div>

<div class="sec" onclick="tog('con')">Console<span class="pm" id="pm-con"></span></div>
<div id="con-b"><pre id="cout"></pre>
<form class="line" style="margin-top:8px" onsubmit="runCmd();return false">
<input type="text" id="cin" autocomplete="off" autocapitalize="off" placeholder="command, e.g. help"><button type="submit" class="pri">Run</button></form></div>

<div class="sec" onclick="tog('log')">Data log<span class="pm" id="pm-log"></span></div>
<div id="log-b"><div id="logsum"></div><div class="msg hide" id="lmsg"></div><div id="loglist"></div></div>

<div class="sec" onclick="tog('set')">Settings<span class="pm" id="pm-set"></span></div>
<div id="set-b"><h3>WiFi networks <span style="font-weight:400" id="wnote"></span></h3><div id="wlist"></div>
<form onsubmit="wifiAdd();return false"><label>Add a network</label>
<div class="line"><input type="text" id="wssid" placeholder="network name" autocomplete="off">
<input type="password" id="wpass" placeholder="password (blank if open)" autocomplete="new-password"></div>
<button type="submit" class="pri">Add</button></form><div class="msg hide" id="wmsg"></div>
<h3>Device</h3><form method="POST" action="/save">
<label>Device ID <span class="muted">(also the .local name; applies on reboot)</span></label><input type="text" name="device_id" value="%DEVICE%">
<label>Sample every (seconds)</label>
<input type="number" name="sample_s" min="1" max="86400" value="%SAMPLE%">
<button type="submit" class="pri">Save &amp; reboot</button></form></div>

<script>
var $=function(i){return document.getElementById(i)},testOn=false,last=null;
function set(i,v){$(i).textContent=v}
function esc(s){var d=document.createElement('div');d.textContent=s==null?'':String(s);return d.innerHTML}
function dur(s){return s<120?s+' s':s<7200?Math.round(s/60)+' min':s<172800?(s/3600).toFixed(1)+' h':(s/86400).toFixed(1)+' days'}
function msg(i,t){var e=$(i);e.textContent=t;e.classList.toggle('hide',!t)}
// Collapsible sections; open/closed state is remembered per browser.
var open={cur:1,tst:1,gr:0,dia:0,dev:0,con:0,log:0,set:0};
try{var o=JSON.parse(localStorage.getItem('open'));if(o)for(var k in o)open[k]=o[k]}catch(e){}
function body(k){return $(k=='cur'||k=='tst'?k:k+'-b')}
function show(k){body(k).classList.toggle('hide',!open[k]);set('pm-'+k,open[k]?'−':'+')}
function tog(k){open[k]=open[k]?0:1;show(k);
 if(open[k])setTimeout(function(){var b=body(k),h=b.previousElementSibling;
  if(b.getBoundingClientRect().bottom>innerHeight)h.scrollIntoView({behavior:'smooth',block:'start'})},60);try{localStorage.setItem('open',JSON.stringify(open))}catch(e){}
 if(open[k]){if(k=='con')con(true);if(k=='log')logs();if(k=='set')wifi();if(k=='gr')(LOGS?Promise.resolve():logs()).then(graph)}}
for(var k in open)show(k);
// A tile: label, value + unit, and q = [meaning, outline colour, plain text].
// The meaning text takes the colour too unless q[2] (pale colours like light).
function tile(l,v,u,q){var m=q?q[0]:'',c=q?q[1]:null,tc=c&&!q[2];
 return '<div class="t"'+(c?' style="border-color:'+c+'"':'')+'><div class="l">'+esc(l)+'</div><div class="v">'+esc(v)+
 (u?' <small>'+esc(u)+'</small>':'')+'</div><div class="m"'+(tc?' style="color:'+c+'"':'')+'>'+(m?esc(m):'&nbsp;')+'</div></div>'}
function grid(t){return '<div class="g">'+t+'</div>'}
function none(n,m){var t='';n.forEach(function(l){t+=tile(l,'–','',[m,null])});return t}
// Colour scales: smooth gradients between [value, colour] stops. The level
// words and thresholds match lib/ambient/levels.h (console): keep them in step.
var STABLE_S=30;  // PMS5003 datasheet: data stable 30 s after waking
var GREEN='#16a34a',YEL='#ca8a04',ORG='#ea580c',RED='#dc2626',PUR='#9333ea',BLUE='#2563eb',UG='µg/m³';
function hex(c){return [1,3,5].map(function(i){return parseInt(c.substr(i,2),16)})}
function grad(st,v){if(v<=st[0][0])return st[0][1];for(var i=1;i<st.length;i++)if(v<=st[i][0]){
 var a=hex(st[i-1][1]),b=hex(st[i][1]),f=(v-st[i-1][0])/(st[i][0]-st[i-1][0]);
 return 'rgb('+a.map(function(x,j){return Math.round(x+(b[j]-x)*f)}).join(',')+')'}return st[st.length-1][1]}
function band(B,v){for(var i=0;i<B.length;i++)if(v<=B[i][0])return B[i][1];return B[B.length-1][1]}
// Temperature: blue (cold) -> green (comfortable 19-24 °C) -> red (hot).
var T_ST=[[12,BLUE],[19,GREEN],[24,GREEN],[27,ORG],[31,RED]];
function tempQ(c){return [band([[0,'Freezing'],[10,'Very cold'],[18,'Cold'],[22,'Cool'],[26,'Comfortable'],[30,'Warm'],[35,'Hot'],[1e9,'Very hot']],c),grad(T_ST,c)]}
// Humidity: green 35-45 %, yellow around 25 / 55 %, red below 15 or above 70 %.
function humQ(h){return [band([[20,'Very dry'],[30,'Dry'],[50,'Comfortable'],[60,'Slightly humid'],[70,'Humid'],[1e9,'Very humid']],h),
 grad([[15,RED],[25,YEL],[35,GREEN],[45,GREEN],[55,YEL],[70,RED]],h)]}
// Pressure: blue (low, stormy) -> green (normal) -> amber (high, fair).
function presQ(p){return [band([[980,'Storm likely'],[1000,'Low, unsettled'],[1020,'Normal'],[1040,'High, fair'],[1e9,'Very high']],p),
 grad([[980,'#4f46e5'],[1000,'#0ea5e9'],[1010,GREEN],[1022,GREEN],[1040,YEL]],p)]}
// PM: US EPA AQI bands (2024). PM1.0 has no standard; it uses the PM2.5 scale.
var PM25=[[9,'Good'],[35.4,'Moderate'],[55.4,'Unhealthy for sensitive'],[125.4,'Unhealthy'],[225.4,'Very unhealthy'],[1e9,'Hazardous']];
var PM10=[[54,'Good'],[154,'Moderate'],[254,'Unhealthy for sensitive'],[354,'Unhealthy'],[424,'Very unhealthy'],[1e9,'Hazardous']];
function pmQ(B,v){return [band(B,v),grad([[B[0][0],GREEN],[B[1][0],YEL],[B[2][0],ORG],[B[3][0],RED],[B[4][0],PUR]],v)]}
// Sound (RMS of the mic signal, ADC counts): quiet room green -> loud red.
// SQ thresholds are provisional; see the KY-037 notes in CLAUDE.md.
function sndQ(v){return [band([[SQ[0],'Quiet'],[SQ[1],'Some noise'],[SQ[2],'Loud'],[1e9,'Very loud']],v),grad([[SQ[0],GREEN],[SQ[1],YEL],[SQ[2],RED]],v)]}
var SQ=[4,15,60];
// Light in lux: dark grey (dark) -> yellow (bright), on a log scale. Not good or bad.
function lightQ(lux){return [band([[10,'Dark'],[50,'Dim'],[200,'Indoor lighting'],[500,'Bright indoor'],[5000,'Very bright'],[1e9,'Daylight']],lux),grad([[0,'#4b5563'],[3,'#eab308']],Math.log10(1+lux)),1]}
// Approximate lux from the raw photoresistor reading. Same formula as
// lightLux() in lib/ambient/light.h; r10 is the calibration (`light cal`).
var SAT=4090;
function lux(raw,r10){var v=Math.max(raw,1)*3.1/4095,ohm=10000*(3.3-v)/v;return 10*Math.pow(r10/ohm,1/0.6)}
// The nine reading tiles for one Reading (status.json "sample" or "live").
// dustMsg is shown in the PM tiles when the dust sensor has no data.
function readings(r,dustMsg){
 var c=r.climate,u=r.dust,t;
 t=c.ok?tile('Temperature',(c.temp*9/5+32).toFixed(1),'°F',[c.temp.toFixed(1)+' °C · '+tempQ(c.temp)[0],tempQ(c.temp)[1]])+
  tile('Humidity',c.hum.toFixed(0),'%',humQ(c.hum))+tile('Pressure',c.pres.toFixed(0),'hPa',presQ(c.pres))
  :none(['Temperature','Humidity','Pressure'],'No reading');
 t+=u.ok?tile('PM2.5 est. (raw '+u.pm[1]+')',Math.round(u.est),UG,pmQ(PM25,u.est))+tile('PM1.0',u.pm[0],UG,pmQ(PM25,u.pm[0]))+tile('PM10',u.pm[2],UG,pmQ(PM10,u.pm[2]))
  :none(['PM2.5','PM1.0','PM10'],dustMsg);
 t+=tile('Sound avg',r.sound.avg.toFixed(1),'',sndQ(r.sound.avg))+tile('Sound peak',r.sound.peak.toFixed(1),'',sndQ(r.sound.peak))+
  tile('Light',(r.light>=SAT?'≥':'')+Math.round(r.lux),'lux',r.light>=SAT?['Sensor maxed out',lightQ(r.lux)[1],1]:lightQ(r.lux));
 return grid(t)}
// Device-detail tiles are plain; only real problems get an outline.
var RESET={poweron:'Power connected',software:'Restart or update',panic:'Crash (see console)',
 brownout:'Power dipped',task_wdt:'Watchdog: code hung',int_wdt:'Watchdog: code hung',wdt:'Watchdog: code hung',
 external:'Reset button',usb:'Reset over USB',deepsleep:'Woke from sleep'};
function render(d){
 var s=d.sys,h=d.hw,S=d.sample,L=d.live,t;last=d;
 testOn=s.test;$('tsw').classList.toggle('on',testOn);set('tlab',testOn?'On':'Off');
 set('upd','updated '+new Date().toLocaleTimeString([],{hour:'numeric',minute:'2-digit',second:'2-digit'}));
 var dustMsg=h.dust_on?(h.dust_s<h.warm_s?'Warming up':'No frames'):'Sensor asleep';
 // Last sample: always what was written to the log. Before the first one,
 // the current readings stand in, under a warning.
 if(S){$('cwarn').classList.add('hide');
  $('curb').innerHTML=readings(S,'No reading');
  set('src','Written to the log '+dur(S.age_s)+' ago · next in '+dur(s.next_sample_s)+'. Sound and light cover the whole interval. PM2.5 est. is an indoor correction (ALT-CF3); the log and graphs keep raw values.')}
 else{msg('cwarn','Not saved yet. These are the current readings. First sample in '+dur(s.next_sample_s)+', once the dust sensor has warmed up.');
  $('curb').innerHTML=L?readings(L,dustMsg):'<div class="muted">Starting…</div>';
  set('src','')}
 // Test readings: the same tiles, measured the same way over the last 5 s.
 if(testOn&&L){var u=L.dust,g=L.diag,c=u.cnt;
  $('tstb').innerHTML=readings(L,dustMsg)+'<div class="muted">Last 5 s, measured the same way as a sample. Updated '+L.age_s+' s ago.</div>';
  t=tile('Dust sensor',dur(u.awake_s),'awake',u.awake_s<STABLE_S?['Warming up',YEL]:['Stable',GREEN]);
  t+=tile('Frame interval',u.ok?g.dt_ms:'–','ms',!u.ok?['No frames',RED]:g.dt_ms<600||g.dt_ms>2500?['Unusual',YEL]:['Normal',null]);
  t+=tile('Frame errors',g.errors,'in 5 s',g.errors?['Check wiring',RED]:['None',null]);
  t+=tile('> 0.3 µm',c[0],'per 0.1 L',['Smoke, cooking',null])+tile('> 1.0 µm',c[2],'per 0.1 L',['Fine dust',null]);
  t+=tile('> 2.5 µm',c[3],'per 0.1 L',['Dust, pollen',null])+tile('> 5 µm',c[4],'per 0.1 L',['Large dust',null]);
  t+=tile('> 10 µm',c[5],'per 0.1 L',['Sensor sees few',null])+tile('PM2.5 (CF=1)',u.cf1[1],UG,['Factory calibration',null]);
  $('dia-b').innerHTML=grid(t)+'<div class="muted">Counts and CF=1 averaged over the last '+u.n+' frames. Updated '+L.age_s+' s ago.</div>'}
 else{$('tstb').innerHTML='<div class="muted">'+(testOn?'Starting: values appear within 5 s.':'Off. Turn on to keep the dust sensor awake and see readings every 5 s.')+'</div>';
  $('dia-b').innerHTML=grid(tile('Dust sensor',h.dust_on?dur(h.dust_s):'Asleep',h.dust_on?'awake':'',
   [h.dust_on?(h.dust_s<STABLE_S?'Warming up':'Stable'):'Wakes '+h.warm_s+' s before a sample',null]))+
   '<div class="muted">Frame and particle details need test mode.</div>'}
 // Device details: timing, then status, then system.
 t=tile('Uptime',dur(s.uptime_s),'',null)+tile('Next sample','in '+dur(s.next_sample_s),'',['every '+dur(s.sample_s),null]);
 t+=tile('Firmware',s.fw=='pending'?'On trial':'OK','',[s.fw=='pending'?'Rolls back if it fails':s.build,s.fw=='pending'?YEL:null]);
 t+=tile('Mode',testOn?'Test':'Normal','',[testOn?'Dust sensor always on':'Dust sensor sleeps',null]);
 t+=tile('Climate sensor',h.climate?'OK':'Missing','',h.climate?['BME280 at 0x'+h.climate_addr.toString(16),null]:['Check wiring',RED]);
 t+=tile('WiFi',s.rssi,'dBm',s.rssi>=-70?[s.rssi>=-60?'Strong':'Good',null]:s.rssi>=-80?['Weak',YEL]:['Very weak',RED]);
 t+=tile('Last reset',s.reset,'',[RESET[s.reset]||'',/brownout|panic|wdt/.test(s.reset)?RED:null]);
 t+=tile('Free memory',Math.round(s.heap/1024),'KB',s.heap<40000?['Low',YEL]:null);
 var low=s.tasks.filter(function(k){return k.stack_free<512});
 t+=tile('Task stacks',low.length?low.length+' low':'OK','',low.length?[low.map(function(k){return k.name}).join(', '),YEL]:[s.tasks.length+' tasks',null]);
 var up=s.upload;
 t+=tile('Upload',!up.on?'Off':up.error?'Failing':up.ok_s<0?'Waiting':dur(up.ok_s)+' ago','',
  !up.on?['Not set up',null]:up.error?[up.error,RED]:[up.pending?up.pending+' rows waiting':'Up to date',null]);
 $('devb').innerHTML=grid(t);
}
function tick(){fetch('/status.json').then(function(r){return r.json()}).then(render)
 .catch(function(){set('upd','reconnecting…')})}
// Run a console command (POST, so a WiFi password never sits in a URL);
// resolves to its output without the "> command" echo.
function cmd(c){return fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'c='+encodeURIComponent(c)}).then(function(r){return r.text()}).then(function(x){return x.replace(/^>.*\n/,'').trim()})}
// Test switch: takes effect immediately; the message says what the scheduler does next.
function setTest(on){
 if(!open.tst)tog('tst');
 cmd('test '+(on?'on':'off')).then(function(){
  msg('tmsg',on?'Test mode on. Dust sensor waking; readings settle in about 30 s.':
   'Test mode off. Dust sensor sleeps until '+(last?last.hw.warm_s:90)+' s before the next sample'+(last?' (in '+dur(last.sys.next_sample_s)+')':'')+'.');
  setTimeout(function(){msg('tmsg','')},15000);tick()})}
function sampleNow(){msg('dmsg','Requesting a sample…');
 cmd('sample').then(function(x){msg('dmsg',x||'Sample requested.');tick()})}
function reboot(){if(!confirm('Reboot the monitor now?'))return;
 cmd('reboot').then(function(){msg('dmsg','Rebooting. This page reconnects by itself.')})}
function con(pin){fetch('/console.txt').then(function(r){return r.text()}).then(function(x){var p=$('cout'),
 b=pin||p.scrollTop+p.clientHeight>=p.scrollHeight-30;p.textContent=x;if(b)p.scrollTop=p.scrollHeight})}
function runCmd(){var c=$('cin').value.trim();if(!c)return;$('cin').value='';
 cmd(c).then(function(){con(true);tick();if(open.log)logs()})}
// WiFi networks (Settings): saved list with Remove, and an Add form.
function wifi(){return fetch('/wifi.json').then(function(r){return r.json()}).then(function(j){
 set('wnote','('+j.saved.length+' of '+j.max+'; the strongest in range is used)');
 var t='';j.saved.forEach(function(s){var cur=s==j.connected;
  t+='<div class="row"><span><b>'+esc(s)+'</b>'+(cur?' <span class="muted">connected, '+j.rssi+' dBm</span>':'')+'</span>'+
   (j.saved.length>1?'<a href="#" data-s="'+esc(s).replace(/"/g,'&quot;')+'" onclick="wifiRm(this.dataset.s);return false" style="color:#dc2626">Remove</a>':'')+'</div>'});
 if(!j.connected)t+='<div class="muted">Not connected: the setup network is on.</div>';
 $('wlist').innerHTML=t})}
// SSIDs are quoted for the command; a password is the rest of the line.
function wifiAdd(){var s=$('wssid').value.trim(),p=$('wpass').value;
 if(!s||s.indexOf('"')>=0){msg('wmsg','Enter the network name (without double quotes).');return}
 cmd('wifi add "'+s+'" '+p).then(function(x){msg('wmsg',x||'Added.');$('wssid').value='';$('wpass').value='';wifi()})}
function wifiRm(s){if(!confirm('Remove '+s+'?'))return;cmd('wifi rm "'+s+'"').then(function(x){msg('wmsg',x);wifi()})}
// Data log: the current log on top, then saved logs (user saves and automatic
// archives of full logs), each a box with its actions.
var LOGS=null;
function btn(label,fn,cls){return '<button type="button"'+(cls?' class="'+cls+'"':'')+' onclick="'+fn+'">'+label+'</button>'}
function logs(){return fetch('/logs.json').then(function(r){return r.json()}).then(function(j){LOGS=j;
 var cap=j.rows+j.rows_left;
 $('logsum').innerHTML=grid(tile('Rows',j.rows,'/ ~'+cap.toLocaleString(),null)+
  tile('Size',j.live_kb,'/ '+j.max_kb+' KB',null)+
  tile('Log fills in',last?dur(j.rows_left*last.sys.sample_s):'–','',[last?'then archived below':'',null]));
 var t='<div class="lb"><div class="lh"><b>Current log</b><span class="muted">'+j.rows+(j.rows==1?' row · ':' rows · ')+j.live_kb+' KB</span></div><div class="lbt">'+
  '<a class="b" href="/log.csv">&#8595; Download</a>'+btn('&#43; Save copy','showSave()')+btn('&#9776; Graph',"graphOf('')")+btn('&#10005; Clear','clearLog()','del')+
  '</div><form id="savef" class="hide" onsubmit="saveLog();return false"><div class="line" style="margin-top:10px">'+
  '<input type="text" id="sname" placeholder="name, e.g. garage-sweep" autocomplete="off">'+
  '<select id="srange" style="width:auto"><option value="">All rows</option><option value="20">Last 20 rows</option>'+
  '<option value="100">Last 100 rows</option><option value="30m">Last 30 min</option><option value="1h">Last hour</option>'+
  '<option value="8h">Last 8 hours</option><option value="24h">Last 24 hours</option></select></div>'+
  '<button type="submit" class="pri">Save</button></form></div>';
 var sv=j.saved.slice().sort(function(a,b){return a.name<b.name?1:-1});
 t+='<h3>Saved logs <span style="font-weight:400">('+sv.length+' · '+j.free_kb+' KB free)</span></h3>';
 sv.forEach(function(s){var n=esc(s.name),q="'"+n+"'";
  t+='<div class="lb"><div class="lh"><b>'+n+'</b><span class="muted">'+s.kb+' KB'+archWhy(s.name)+'</span></div><div class="lbt">'+
   '<a class="b" href="/saved.csv?name='+encodeURIComponent(s.name)+'">&#8595; Download</a>'+btn('&#9998; Rename','showRen('+q+')')+
   btn('&#9776; Graph','graphOf('+q+')')+btn('&#10005; Delete','rmLog('+q+')','del')+'</div>'+
   '<form id="ren-'+n+'" class="hide" onsubmit="renLog('+q+');return false"><div class="line" style="margin-top:10px">'+
   '<input type="text" id="rn-'+n+'" value="'+n+'" autocomplete="off"><button type="submit" class="pri">Rename</button></div></form></div>'});
 if(!sv.length)t+='<div class="muted">None yet. Full logs are archived here automatically.</div>';
 $('loglist').innerHTML=t;fillSources()})}
// Automatic archives carry the reason in their name: archive-<reason>-<time>.
function archWhy(n){var m=/^archive-([a-z]+)-/.exec(n);if(!m)return '';
 return ' · archived: '+({full:'log was full',columns:'columns changed (firmware update)',migrated:'moved from log.old.csv'}[m[1]]||'automatically')}
function showSave(){$('savef').classList.toggle('hide');$('sname').focus()}
function showRen(n){$('ren-'+n).classList.toggle('hide')}
function saveLog(){var n=$('sname').value.trim(),r=$('srange').value;
 if(!n){msg('lmsg','Enter a name first.');return}
 cmd('log save '+n+(r?' '+r:'')).then(function(x){msg('lmsg',x);logs()})}
function renLog(n){var to=$('rn-'+n).value.trim();if(!to||to==n)return;
 cmd('log mv '+n+' '+to).then(function(x){msg('lmsg',x||'Renamed.');logs()})}
function rmLog(n){if(!confirm('Delete saved log '+n+'?'))return;cmd('log rm '+n).then(function(x){msg('lmsg',x);logs()})}
function clearLog(){if(!confirm('Delete the current log? Saved logs are kept.'))return;
 cmd('log clear').then(function(x){msg('lmsg',x);logs()})}

// Graphs: one small chart per reading on a shared time axis, drawn as SVG from
// the CSV in the browser. Raw values (the trend data); PM2.5 also shows the
// ALT-CF3 indoor estimate, dashed. Rows without a clock time are skipped.
var G=null;
function fillSources(){var s=$('gsrc'),cur=s.value,o='<option value="">Current log</option><option value="*">All logs combined</option>';
 (LOGS?LOGS.saved:[]).map(function(x){return x.name}).sort().reverse().forEach(function(n){o+='<option value="'+esc(n)+'">'+esc(n)+'</option>'});
 s.innerHTML=o;s.value=cur||''}
function graphOf(n){if(!open.gr)tog('gr');fillSources();$('gsrc').value=n;graph();$('gr-sec').scrollIntoView({behavior:'smooth'})}
function csvUrl(n){return n?'/saved.csv?name='+encodeURIComponent(n):'/log.csv'}
function parse(txt){var L=txt.trim().split('\n'),h=L.shift().trim().split(','),out=[];
 L.forEach(function(l){var c=l.trim().split(','),r={};if(c.length<h.length)return;
  h.forEach(function(k,i){r[k]=c[i]===''?NaN:+c[i]});out.push(r)});return out}
function graph(){var src=$('gsrc').value;set('gmsg','Loading…');$('charts').innerHTML='';
 var names=src=='*'?[''].concat((LOGS?LOGS.saved:[]).map(function(x){return x.name})):[src];
 Promise.all(names.map(function(n){return fetch(csvUrl(n)).then(function(r){return r.ok?r.text():''})})).then(function(ts){
  var byT={},skipped=0;
  ts.forEach(function(t){if(t)parse(t).forEach(function(r){if(!(r.epoch>0)){skipped++;return}byT[r.epoch]=r})});
  var rows=Object.keys(byT).map(function(k){return byT[k]}).sort(function(a,b){return a.epoch-b.epoch});
  G={rows:rows,skipped:skipped};draw()}).catch(function(){set('gmsg','Could not load the log.')})}
// ALT-CF3 from a log row's particle counts. Same formula as pm25AltCf3() in
// lib/ambient/air_quality.h: computed here rather than logged, since a
// derived column would cost log space on every row.
function est(r){var n1=Math.max(0,r.cnt_0_3-r.cnt_0_5),n2=Math.max(0,r.cnt_0_5-r.cnt_1_0),n3=Math.max(0,r.cnt_1_0-r.cnt_2_5);
 return 3*(0.00030418*n1+0.0018512*n2+0.02069706*n3)}
var SER=[
 {t:'Temperature',u:'°F',d:1,l:[function(r){return r.temp_c*9/5+32}]},
 {t:'Humidity',u:'%',d:0,l:[function(r){return r.humidity_pct}]},
 {t:'Pressure',u:'hPa',d:1,l:[function(r){return r.pressure_hpa}]},
 {t:'PM2.5',u:UG,d:0,l:[function(r){return r.pm2_5},est],n:['raw','est.']},
 {t:'PM1.0',u:UG,d:0,l:[function(r){return r.pm1_0}]},
 {t:'PM10',u:UG,d:0,l:[function(r){return r.pm10}]},
 {t:'Sound',u:'',d:1,l:[function(r){return r.sound_avg_rms},function(r){return r.sound_peak_rms}],n:['avg','peak']},
 {t:'Light',u:'lux',d:0,l:[function(r){return lux(r.light,last?last.hw.ldr_r10:15000)}]}];
var COL=['#2563eb','#dc2626'];
function tfmt(e){return new Date(e*1000).toLocaleString([],{month:'short',day:'numeric',hour:'numeric',minute:'2-digit'})}
function draw(){if(!G)return;var rows=G.rows;
 if(rows.length<2){set('gmsg',rows.length?'Only one reading so far; graphs need two.':'No readings with a clock time yet.');$('charts').innerHTML='';return}
 var t0=rows[0].epoch,t1=rows[rows.length-1].epoch,span=Math.max(1,t1-t0);
 var gaps=rows.slice(1).map(function(r,i){return r.epoch-rows[i].epoch}).sort(function(a,b){return a-b}),gap=Math.max(gaps[gaps.length>>1]*3,120);
 set('gmsg',rows.length+' readings, '+tfmt(t0)+' to '+tfmt(t1)+(G.skipped?' · '+G.skipped+' rows without a clock time skipped':'')+'.');
 var h='';SER.forEach(function(s,si){var ys=[];
  s.l.forEach(function(f){rows.forEach(function(r){var v=f(r);if(isFinite(v))ys.push(v)})});
  if(!ys.length)return;
  var lo=Math.min.apply(null,ys),hi=Math.max.apply(null,ys);if(hi-lo<1e-6){hi+=1;lo-=1}var pad=(hi-lo)*.08;lo-=pad;hi+=pad;
  var paths='';s.l.forEach(function(f,li){var d='',prev=null;rows.forEach(function(r){var v=f(r);
    if(!isFinite(v)){prev=null;return}var x=(r.epoch-t0)/span*1000,y=100-(v-lo)/(hi-lo)*100;
    d+=(prev===null||r.epoch-prev>gap?'M':'L')+x.toFixed(1)+' '+y.toFixed(1);prev=r.epoch});
   paths+='<path d="'+d+'" fill="none" stroke="'+COL[li]+'" stroke-width="1.6"'+(s.t=='PM2.5'&&li==1?' stroke-dasharray="5 4"':'')+' vector-effect="non-scaling-stroke"/>'});
  h+='<div class="ch"><div class="chh"><b>'+s.t+'</b><span id="gv'+si+'"></span></div>'+
   '<div class="chr">'+(s.n?s.n.map(function(n,i){return '<span style="color:'+COL[i]+'">&#9644; '+n+'</span>'}).join(' '):'')+
   '<span>'+(+(hi-pad)).toFixed(s.d)+' max · '+(+(lo+pad)).toFixed(s.d)+' min</span></div>'+
   '<svg viewBox="0 0 1000 100" preserveAspectRatio="none" data-si="'+si+'">'+paths+
   '<line class="hl" x1="-10" x2="-10" y1="0" y2="100" vector-effect="non-scaling-stroke"/></svg></div>'});
 h+='<div class="chx"><span>'+tfmt(t0)+'</span><span>'+tfmt(t1)+'</span></div>';
 $('charts').innerHTML=h;G.view={rows:rows,t0:t0,span:span};hover(rows.length-1);
 [].forEach.call(document.querySelectorAll('#charts svg'),function(sv){
  sv.onpointermove=function(e){var b=sv.getBoundingClientRect(),te=t0+(e.clientX-b.left)/b.width*span,i=0,lo2=0,hi2=rows.length-1;
   while(lo2<hi2){var m=(lo2+hi2)>>1;if(rows[m].epoch<te)lo2=m+1;else hi2=m}i=lo2;
   if(i>0&&te-rows[i-1].epoch<rows[i].epoch-te)i--;hover(i)};
  sv.onpointerleave=function(){hover(rows.length-1)}})}
// Show the values at reading i in every chart header, with a marker line.
function hover(i){var V=G.view,r=V.rows[i],x=((r.epoch-V.t0)/V.span*1000).toFixed(1);
 [].forEach.call(document.querySelectorAll('#charts .hl'),function(l){l.setAttribute('x1',x);l.setAttribute('x2',x)});
 SER.forEach(function(s,si){var e=$('gv'+si);if(!e)return;
  e.textContent=s.l.map(function(f){var v=f(r);return isFinite(v)?v.toFixed(s.d):'–'}).join(' / ')+(s.u?' '+s.u:'')+' · '+tfmt(r.epoch)})}
tick();if(open.con)con(true);if(open.set)wifi();if(open.log||open.gr)logs().then(function(){if(open.gr)graph()});
setInterval(function(){tick();if(open.con)con()},3000);
</script></body></html>)HTML";

static void handleRoot() {
    const Params& p = params();
    String html = FPSTR(ROOT_PAGE);
    html.replace("%DEVICE%", esc(p.deviceId));
    html.replace("%SAMPLE%", String(p.sampleIntervalS));
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
