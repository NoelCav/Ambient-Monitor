// Shared by the device portal (web/device.html, served from the monitor as
// /common.js) and the dashboard (web/index.html, GitHub Pages): helpers,
// collapsible sections, reading tiles with their colour scales, and charts.
// Level words and thresholds match lib/ambient/levels.h (console); ALT-CF3
// and lux match lib/ambient/air_quality.h and light.h: keep them in step.

var $=function(i){return document.getElementById(i)};
function set(i,v){$(i).textContent=v}
function esc(s){var d=document.createElement('div');d.textContent=s==null?'':String(s);return d.innerHTML}
function dur(s){return s<120?Math.round(s)+' s':s<7200?Math.round(s/60)+' min':s<172800?(s/3600).toFixed(1)+' h':(s/86400).toFixed(1)+' days'}
function msg(i,t){var e=$(i);e.textContent=t;e.classList.toggle('hide',!t)}
function tfmt(e){return new Date(e*1000).toLocaleString([],{month:'short',day:'numeric',hour:'numeric',minute:'2-digit'})}

// Collapsible sections: a ".sec" header bar followed by its body (id k, or
// k+'-b'). defaults = {key: 1 open / 0 closed}; state is remembered per
// browser under storeKey; onOpen(k) runs when a section is opened.
var open={},openHook=null,openStore='open';
function body(k){return $(k)||$(k+'-b')}
function show(k){body(k).classList.toggle('hide',!open[k]);set('pm-'+k,open[k]?'−':'+')}
function sections(defaults,storeKey,onOpen){open=defaults;openStore=storeKey;openHook=onOpen;
 try{var o=JSON.parse(localStorage.getItem(storeKey));if(o)for(var k in o)if(k in open)open[k]=o[k]}catch(e){}
 for(var k in open)show(k)}
function tog(k){open[k]=open[k]?0:1;show(k);
 if(open[k])setTimeout(function(){var b=body(k),h=b.previousElementSibling;
  if(b.getBoundingClientRect().bottom>innerHeight)h.scrollIntoView({behavior:'smooth',block:'start'})},60);
 try{localStorage.setItem(openStore,JSON.stringify(open))}catch(e){}
 if(open[k]&&openHook)openHook(k)}

// A tile: label, value + unit, and q = [meaning, outline colour, plain text].
// The meaning text takes the colour too unless q[2] (pale colours like light).
function tile(l,v,u,q){var m=q?q[0]:'',c=q?q[1]:null,tc=c&&!q[2];
 return '<div class="t"'+(c?' style="border-color:'+c+'"':'')+'><div class="l">'+esc(l)+'</div><div class="v">'+esc(v)+
 (u?' <small>'+esc(u)+'</small>':'')+'</div><div class="m"'+(tc?' style="color:'+c+'"':'')+'>'+(m?esc(m):'&nbsp;')+'</div></div>'}
function grid(t){return '<div class="g">'+t+'</div>'}
function none(n,m){var t='';n.forEach(function(l){t+=tile(l,'–','',[m,null])});return t}

// Colour scales: smooth gradients between [value, colour] stops.
var GREEN='#16a34a',YEL='#ca8a04',ORG='#ea580c',RED='#dc2626',PUR='#9333ea',BLUE='#2563eb',UG='µg/m³';
function hex(c){return [1,3,5].map(function(i){return parseInt(c.substr(i,2),16)})}
function grad(st,v){if(v<=st[0][0])return st[0][1];for(var i=1;i<st.length;i++)if(v<=st[i][0]){
 var a=hex(st[i-1][1]),b=hex(st[i][1]),f=(v-st[i-1][0])/(st[i][0]-st[i-1][0]);
 return 'rgb('+a.map(function(x,j){return Math.round(x+(b[j]-x)*f)}).join(',')+')'}return st[st.length-1][1]}
function band(B,v){for(var i=0;i<B.length;i++)if(v<=B[i][0])return B[i][1];return B[B.length-1][1]}
// Temperature: blue (cold) -> green (comfortable 19-24 °C) -> red (hot).
function tempQ(c){return [band([[0,'Freezing'],[10,'Very cold'],[18,'Cold'],[22,'Cool'],[26,'Comfortable'],[30,'Warm'],[35,'Hot'],[1e9,'Very hot']],c),
 grad([[12,BLUE],[19,GREEN],[24,GREEN],[27,ORG],[31,RED]],c)]}
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
var SQ=[4,15,60];
function sndQ(v){return [band([[SQ[0],'Quiet'],[SQ[1],'Some noise'],[SQ[2],'Loud'],[1e9,'Very loud']],v),grad([[SQ[0],GREEN],[SQ[1],YEL],[SQ[2],RED]],v)]}
// Light in lux: dark grey (dark) -> yellow (bright), on a log scale. Not good or bad.
function lightQ(lx){return [band([[10,'Dark'],[50,'Dim'],[200,'Indoor lighting'],[500,'Bright indoor'],[5000,'Very bright'],[1e9,'Daylight']],lx),grad([[0,'#4b5563'],[3,'#eab308']],Math.log10(1+lx)),1]}

// Approximate lux from the raw photoresistor reading (lightLux() in
// lib/ambient/light.h); r10 is the monitor's calibration (`light cal`).
var SAT=4090,R10_DEFAULT=15000;
function lux(raw,r10){var v=Math.max(raw,1)*3.1/4095,ohm=10000*(3.3-v)/v;return 10*Math.pow((r10||R10_DEFAULT)/ohm,1/0.6)}
// ALT-CF3 indoor PM2.5 from a log row's particle counts (pm25AltCf3() in
// lib/ambient/air_quality.h): computed rather than logged, to save log space.
function est(r){var n1=Math.max(0,r.cnt_0_3-r.cnt_0_5),n2=Math.max(0,r.cnt_0_5-r.cnt_1_0),n3=Math.max(0,r.cnt_1_0-r.cnt_2_5);
 return 3*(0.00030418*n1+0.0018512*n2+0.02069706*n3)}

// The nine reading tiles. r has the shape of a status.json Reading:
// {climate:{ok,temp,hum,pres}, dust:{ok,pm:[pm1,pm25,pm10],est}, sound:{avg,peak}, light, lux}.
// dustMsg is shown in the PM tiles when there is no dust data.
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
// The same Reading shape from a log row (CSV column names, as on the device
// and in the Sheet). Empty columns mean that sensor had no reading.
function rowReading(r,r10){var ok=function(k){return isFinite(r[k])&&r[k]!==''};
 return {climate:{ok:ok('temp_c'),temp:+r.temp_c,hum:+r.humidity_pct,pres:+r.pressure_hpa},
  dust:{ok:ok('pm2_5'),pm:[+r.pm1_0,+r.pm2_5,+r.pm10],est:est(r)},
  sound:{avg:+r.sound_avg_rms||0,peak:+r.sound_peak_rms||0},light:+r.light||0,lux:lux(+r.light||0,r10)}}

// Charts: one small chart per reading on a shared time axis, as SVG.
// mons = [{name, rows (log-row objects sorted by epoch), r10}]. With one
// monitor each chart's lines are coloured per series (PM2.5 raw / est.,
// sound avg / peak); with several, each monitor has a colour and the second
// series is dashed.
var SER=[
 {t:'Temperature',u:'°F',d:1,l:[function(r){return r.temp_c*9/5+32}]},
 {t:'Humidity',u:'%',d:0,l:[function(r){return r.humidity_pct}]},
 {t:'Pressure',u:'hPa',d:1,l:[function(r){return r.pressure_hpa}]},
 {t:'PM2.5',u:UG,d:0,l:[function(r){return r.pm2_5},est],n:['raw','est.']},
 {t:'PM1.0',u:UG,d:0,l:[function(r){return r.pm1_0}]},
 {t:'PM10',u:UG,d:0,l:[function(r){return r.pm10}]},
 {t:'Sound',u:'',d:1,l:[function(r){return r.sound_avg_rms},function(r){return r.sound_peak_rms}],n:['avg','peak']},
 {t:'Light',u:'lux',d:0,l:[function(r,m){return lux(r.light,m.r10)}]}];
var COL=['#2563eb','#dc2626','#16a34a','#ca8a04','#9333ea','#0891b2'];
function parseCsv(txt){var L=txt.trim().split('\n'),h=L.shift().trim().split(','),out=[];
 L.forEach(function(l){var c=l.trim().split(','),r={};if(c.length<h.length)return;
  h.forEach(function(k,i){r[k]=c[i]===''?NaN:+c[i]});out.push(r)});return out}
// Nearest row to time te (rows sorted by epoch), or null if none within maxGap.
function nearest(rows,te,maxGap){var lo=0,hi=rows.length-1;if(hi<0)return null;
 while(lo<hi){var m=(lo+hi)>>1;if(rows[m].epoch<te)lo=m+1;else hi=m}
 if(lo>0&&te-rows[lo-1].epoch<rows[lo].epoch-te)lo--;return Math.abs(rows[lo].epoch-te)<=maxGap?rows[lo]:null}
function charts(el,mons,note){
 mons=mons.filter(function(m){return m.rows.length});
 var all=[].concat.apply([],mons.map(function(m){return m.rows}));
 if(all.length<2){el.innerHTML='<div class="muted">'+(all.length?'Only one reading so far; graphs need two.':'No readings in this range.')+'</div>';return}
 var t0=Math.min.apply(null,all.map(function(r){return r.epoch})),t1=Math.max.apply(null,all.map(function(r){return r.epoch})),span=Math.max(1,t1-t0);
 // A gap longer than 3x the usual spacing (or 2 min) breaks the line: the monitor was off.
 mons.forEach(function(m){var g=m.rows.slice(1).map(function(r,i){return r.epoch-m.rows[i].epoch}).sort(function(a,b){return a-b});
  m.gap=Math.max((g[g.length>>1]||60)*3,120)});
 var multi=mons.length>1,col=function(mi,li){return COL[multi?mi:li]};
 var h=note?'<div class="muted">'+note+'</div>':'';
 if(multi)h+='<div class="chr" style="justify-content:flex-start;gap:14px">'+mons.map(function(m,mi){return '<span style="color:'+COL[mi]+'">&#9644; '+esc(m.name)+'</span>'}).join('')+'</div>';
 SER.forEach(function(s,si){var ys=[];
  mons.forEach(function(m){s.l.forEach(function(f){m.rows.forEach(function(r){var v=f(r,m);if(isFinite(v))ys.push(v)})})});
  if(!ys.length)return;
  var lo=Math.min.apply(null,ys),hi=Math.max.apply(null,ys);if(hi-lo<1e-6){hi+=1;lo-=1}var pad=(hi-lo)*.08;lo-=pad;hi+=pad;
  var paths='';mons.forEach(function(m,mi){s.l.forEach(function(f,li){var d='',prev=null;
   m.rows.forEach(function(r){var v=f(r,m);if(!isFinite(v)){prev=null;return}
    var x=(r.epoch-t0)/span*1000,y=100-(v-lo)/(hi-lo)*100;d+=(prev===null||r.epoch-prev>m.gap?'M':'L')+x.toFixed(1)+' '+y.toFixed(1);prev=r.epoch});
   paths+='<path d="'+d+'" fill="none" stroke="'+col(mi,li)+'" stroke-width="1.6"'+((multi||s.t=='PM2.5')&&li==1?' stroke-dasharray="5 4"':'')+' vector-effect="non-scaling-stroke"/>'})});
  var key=s.n?s.n.map(function(n,i){return '<span'+(multi?'':' style="color:'+COL[i]+'"')+'>'+(multi&&i?'- - ':'&#9644; ')+n+'</span>'}).join(' '):'';
  h+='<div class="ch"><div class="chh"><b>'+s.t+'</b><span data-si="'+si+'"></span></div>'+
   '<div class="chr"><span>'+key+'</span><span>'+(+(hi-pad)).toFixed(s.d)+' max · '+(+(lo+pad)).toFixed(s.d)+' min</span></div>'+
   '<svg viewBox="0 0 1000 100" preserveAspectRatio="none">'+paths+
   '<line class="hl" x1="-10" x2="-10" y1="0" y2="100" vector-effect="non-scaling-stroke"/></svg></div>'});
 h+='<div class="chx"><span>'+tfmt(t0)+'</span><span>'+tfmt(t1)+'</span></div>';
 el.innerHTML=h;
 // Hover / touch: a marker line and every monitor's values at that time in each chart header.
 function hover(te){var x=((te-t0)/span*1000).toFixed(1);
  [].forEach.call(el.querySelectorAll('.hl'),function(l){l.setAttribute('x1',x);l.setAttribute('x2',x)});
  [].forEach.call(el.querySelectorAll('.chh span'),function(e){var s=SER[e.dataset.si];
   e.textContent=mons.map(function(m){var r=nearest(m.rows,te,m.gap);
    var v=r?s.l.map(function(f){var y=f(r,m);return isFinite(y)?y.toFixed(s.d):'–'}).join(' / '):'–';
    return (multi?m.name+' ':'')+v}).join(' · ')+(s.u?' '+s.u:'')+' · '+tfmt(te)})}
 hover(t1);
 [].forEach.call(el.querySelectorAll('svg'),function(sv){
  sv.onpointermove=function(e){var b=sv.getBoundingClientRect();hover(t0+(e.clientX-b.left)/b.width*span)};
  sv.onpointerleave=function(){hover(t1)}})}
