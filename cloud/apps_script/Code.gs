/**
 * Ambient Monitor data store: a Google Sheet plus this Apps Script web app.
 * Setup steps are in cloud/README.md.
 *
 * Tabs (created on first use):
 *   readings  one row per sample, the device's log columns (header row = names)
 *   hourly    hourly averages per monitor, made by rollup() (nightly trigger)
 *
 * POST  ?key=<UPLOAD_KEY>&r10=<ohms>&name=<display name>   body: the device's CSV (header line + rows)
 *       Appends rows whose epoch is newer than the last one stored for that
 *       device_id, so retries never duplicate. -> {"ok":true,"added":N,"last":epoch}
 * GET   ?latest=1                       newest reading per monitor
 *       ?from=<epoch>&to=<epoch>[&device=<id>][&res=hour]
 *                                       readings in a time range (res=hour: hourly tab)
 *       -> {"columns":[...],"rows":[[...],...],"r10":{device_id: ohms},"names":{device_id: name}}
 *       (each monitor's light calibration and display name, sent with its uploads;
 *       data is keyed by device_id, so renaming a monitor keeps its history)
 *
 * Script properties (Project settings > Script properties):
 *   UPLOAD_KEY     required; the device's `upload key` must match
 *   RAW_KEEP_DAYS  optional; rollup() deletes raw rows older than this
 *                  (hourly averages are kept). Default 365.
 */

const READINGS = 'readings';
const HOURLY = 'hourly';

function sheet_(name) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  return ss.getSheetByName(name) || ss.insertSheet(name);
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}

// Header row of a tab, extended with any new names (columns are never removed,
// so old rows keep lining up when the device adds a column).
function header_(sh, names) {
  let head = sh.getLastColumn() ? sh.getRange(1, 1, 1, sh.getLastColumn()).getValues()[0] : [];
  const missing = names.filter(n => head.indexOf(n) < 0);
  if (missing.length) {
    sh.getRange(1, head.length + 1, 1, missing.length).setValues([missing]);
    head = head.concat(missing);
  }
  return head;
}

function doPost(e) {
  const props = PropertiesService.getScriptProperties();
  if (!e.parameter.key || e.parameter.key !== props.getProperty('UPLOAD_KEY')) {
    return json_({ok: false, error: 'bad key'});
  }
  const lines = e.postData.contents.trim().split('\n');
  const names = lines.shift().trim().split(',');
  const iEpoch = names.indexOf('epoch'), iDev = names.indexOf('device_id');
  if (iEpoch < 0 || iDev < 0) return json_({ok: false, error: 'need epoch and device_id columns'});

  const lock = LockService.getScriptLock();  // two monitors posting at once
  lock.waitLock(20000);
  try {
    const sh = sheet_(READINGS);
    const head = header_(sh, names);
    const out = {};
    const rows = [];
    lines.forEach(line => {
      const c = line.trim().split(',');
      const dev = c[iDev], epoch = Number(c[iEpoch]);
      if (!dev || !(epoch > 0)) return;  // rows from before the clock synced
      const key = 'last_' + dev;
      const last = out[key] !== undefined ? out[key] : Number(props.getProperty(key) || 0);
      if (epoch <= last) return;       // already stored (a retried upload)
      out[key] = epoch;
      const row = head.map(h => {
        const i = names.indexOf(h);
        if (i < 0 || c[i] === '') return '';
        const n = Number(c[i]);
        return isNaN(n) ? c[i] : n;
      });
      rows.push(row);
    });
    if (rows.length) sh.getRange(sh.getLastRow() + 1, 1, rows.length, head.length).setValues(rows);
    Object.keys(out).forEach(k => props.setProperty(k, String(out[k])));
    const devs = new Set(lines.map(l => l.split(',')[iDev]).filter(Boolean));
    if (Number(e.parameter.r10) > 0) devs.forEach(d => props.setProperty('r10_' + d, e.parameter.r10));
    if (e.parameter.name) devs.forEach(d => props.setProperty('name_' + d, e.parameter.name));
    const lastAll = Math.max(0, ...Object.values(out));
    return json_({ok: true, added: rows.length, last: lastAll});
  } finally {
    lock.releaseLock();
  }
}

function doGet(e) {
  const p = e.parameter;
  const sh = sheet_(p.res === 'hour' ? HOURLY : READINGS);
  if (sh.getLastRow() < 2) return json_({columns: [], rows: []});
  const all = sh.getRange(1, 1, sh.getLastRow(), sh.getLastColumn()).getValues();
  const columns = all.shift();
  const iEpoch = columns.indexOf('epoch'), iDev = columns.indexOf('device_id');
  let rows;
  if (p.latest) {
    const newest = {};
    all.forEach(r => { if (!newest[r[iDev]] || r[iEpoch] > newest[r[iDev]][iEpoch]) newest[r[iDev]] = r; });
    rows = Object.values(newest);
  } else {
    const from = Number(p.from || 0), to = Number(p.to || 4e9);
    rows = all.filter(r => r[iEpoch] >= from && r[iEpoch] <= to && (!p.device || r[iDev] === p.device));
  }
  const r10 = {}, names = {};
  const props = PropertiesService.getScriptProperties().getProperties();
  Object.keys(props).forEach(k => {
    if (k.indexOf('r10_') === 0) r10[k.slice(4)] = Number(props[k]);
    if (k.indexOf('name_') === 0) names[k.slice(5)] = props[k];
  });
  return json_({columns: columns, rows: rows, r10: r10, names: names});
}

/**
 * Nightly (time-driven trigger, see cloud/README.md): average every finished
 * hour per monitor into the hourly tab, then delete raw rows older than
 * RAW_KEEP_DAYS. Numeric columns are averaged; `n` is the number of samples.
 */
function rollup() {
  const props = PropertiesService.getScriptProperties();
  const raw = sheet_(READINGS);
  if (raw.getLastRow() < 2) return;
  const all = raw.getRange(1, 1, raw.getLastRow(), raw.getLastColumn()).getValues();
  const columns = all.shift();
  const iEpoch = columns.indexOf('epoch'), iDev = columns.indexOf('device_id');
  const skip = ['epoch', 'device_id', 'uptime_s', 'boot', 'reset'];
  const numeric = columns.filter(c => skip.indexOf(c) < 0);
  const thisHour = Math.floor(Date.now() / 3600000) * 3600;

  const groups = {};  // "dev|hour" -> rows
  all.forEach(r => {
    const dev = r[iDev], hour = Math.floor(r[iEpoch] / 3600) * 3600;
    if (!dev || !(hour > 0) || hour >= thisHour) return;  // unfinished hour
    if (hour <= Number(props.getProperty('rolled_' + dev) || 0)) return;
    (groups[dev + '|' + hour] = groups[dev + '|' + hour] || []).push(r);
  });

  const hourly = sheet_(HOURLY);
  const head = header_(hourly, ['epoch', 'device_id', 'n'].concat(numeric));
  const out = [], rolled = {};
  Object.keys(groups).sort().forEach(k => {
    const [dev, hour] = k.split('|'), rs = groups[k];
    const avg = {epoch: Number(hour), device_id: dev, n: rs.length};
    numeric.forEach(c => {
      const i = columns.indexOf(c);
      const v = rs.map(r => r[i]).filter(x => x !== '' && !isNaN(x));
      avg[c] = v.length ? Math.round(v.reduce((a, b) => a + b, 0) / v.length * 100) / 100 : '';
    });
    out.push(head.map(h => (avg[h] !== undefined ? avg[h] : '')));
    rolled[dev] = Math.max(rolled[dev] || 0, Number(hour));
  });
  if (out.length) hourly.getRange(hourly.getLastRow() + 1, 1, out.length, head.length).setValues(out);
  Object.keys(rolled).forEach(d => props.setProperty('rolled_' + d, String(rolled[d])));

  // Delete raw rows older than RAW_KEEP_DAYS (rows are in time order, oldest at top).
  const keepDays = Number(props.getProperty('RAW_KEEP_DAYS') || 365);
  const cutoff = Date.now() / 1000 - keepDays * 86400;
  let old = 0;
  while (old < all.length && all[old][iEpoch] < cutoff) old++;
  if (old) raw.deleteRows(2, old);
}
