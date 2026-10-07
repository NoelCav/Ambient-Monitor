import { initializeApp } from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js';
import { getFirestore, collection, query, where, orderBy, getDocs } from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-firestore.js';
import { getAuth, signInAnonymously } from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js';

// --- CONFIG (safe to commit — public Firebase web config) ---
const FIREBASE_CONFIG = {
  apiKey: "AIzaSyCh8eG5kJ47smEPUcvR7SrgJG6mdAZGx4E",
  authDomain: "ambient-monitor-f9e46.firebaseapp.com",
  projectId: "ambient-monitor-f9e46",
  storageBucket: "ambient-monitor-f9e46.firebasestorage.app",
  messagingSenderId: "695783451359",
  appId: "1:695783451359:web:0271d4999d6415a97b976a",
  measurementId: "G-9SNRZ1T168",
};

// SHA-256 hex of the dashboard password
const EXPECTED_HASH = "00390de2b7074071bb6494e818e84884ef6331ceb0b1e70948bde3ef4ba57b92";

// ---- Auth gate ----

async function sha256hex(str) {
  const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(str));
  return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
}

function renderGate() {
  document.body.innerHTML = `
    <div class="gate">
      <form id="gate-form">
        <input type="password" id="pw-input" placeholder="password" autofocus autocomplete="current-password" />
        <button type="submit">enter</button>
        <span class="error" id="gate-error"></span>
      </form>
    </div>`;

  document.getElementById('gate-form').addEventListener('submit', async e => {
    e.preventDefault();
    const val = document.getElementById('pw-input').value;
    const hash = await sha256hex(val);
    if (hash === EXPECTED_HASH) {
      sessionStorage.setItem('dash_authed', '1');
      renderDashboard();
    } else {
      document.getElementById('gate-error').textContent = 'incorrect password';
      document.getElementById('pw-input').value = '';
    }
  });
}

// ---- Dashboard ----

const RANGES = ['6h', '24h', '7d', '30d', '3m', '1y', 'all'];

const RANGE_CONFIG = {
  '6h':  { collection: 'raw',    hours: 6 },
  '24h': { collection: 'raw',    hours: 24 },
  '7d':  { collection: 'raw',    hours: 24 * 7 },
  '30d': { collection: 'raw',    hours: 24 * 30 },
  '3m':  { collection: 'agg_30m', hours: 24 * 90 },
  '1y':  { collection: 'agg_1h', hours: 24 * 365 },
  'all': { collection: 'agg_1h', hours: null },
};

const TTL = range => (['6h','24h'].includes(range) ? 5 : 30) * 60 * 1000;

let db, currentRange = '24h', debounceTimer = null;

function getCached(range) {
  const raw = sessionStorage.getItem(`sensor_cache_${range}`);
  if (!raw) return null;
  const { data, fetchedAt } = JSON.parse(raw);
  if (Date.now() - fetchedAt > TTL(range)) return null;
  return data;
}

function setCache(range, data) {
  sessionStorage.setItem(`sensor_cache_${range}`, JSON.stringify({ data, fetchedAt: Date.now() }));
}

async function fetchData(range) {
  const cached = getCached(range);
  if (cached) return cached;

  const cfg = RANGE_CONFIG[range];
  const col = collection(db, cfg.collection);
  const nowSec = Math.floor(Date.now() / 1000);
  const fromSec = cfg.hours ? nowSec - cfg.hours * 3600 : 0;

  const q = query(col, where('timestamp', '>=', fromSec), orderBy('timestamp', 'asc'));
  const snap = await getDocs(q);
  const data = snap.docs.map(d => d.data());
  setCache(range, data);
  return data;
}

function plotCharts(data, range) {
  const ts = data.map(d => new Date(d.timestamp * 1000).toISOString());

  const layout = {
    paper_bgcolor: 'transparent',
    plot_bgcolor: 'transparent',
    font: { color: getComputedStyle(document.body).getPropertyValue('--fg').trim() || '#e8e8e8' },
    margin: { t: 20, r: 60, b: 40, l: 60 },
    xaxis: { gridcolor: 'rgba(128,128,128,0.15)', type: 'date' },
  };

  const config = { responsive: true, displayModeBar: false, connectgaps: false };

  // Chart 1: Temperature + Humidity
  Plotly.react('chart1', [
    { x: ts, y: data.map(d => d.temperature), name: 'Temp °C', type: 'scatter', mode: 'lines', line: { color: '#f97316' } },
    { x: ts, y: data.map(d => d.humidity),    name: 'Humidity %', type: 'scatter', mode: 'lines', line: { color: '#38bdf8' }, yaxis: 'y2' },
  ], {
    ...layout,
    yaxis:  { ...layout.xaxis, title: '°C' },
    yaxis2: { ...layout.xaxis, title: '%', overlaying: 'y', side: 'right' },
  }, config);

  // Chart 2: Pressure
  Plotly.react('chart2', [
    { x: ts, y: data.map(d => d.pressure), name: 'Pressure hPa', type: 'scatter', mode: 'lines', line: { color: '#34d399' } },
  ], {
    ...layout,
    yaxis: { ...layout.xaxis, title: 'hPa' },
  }, config);

  // Chart 3: PM2.5 + PM10
  Plotly.react('chart3', [
    { x: ts, y: data.map(d => d.pm2_5), name: 'PM2.5 µg/m³', type: 'scatter', mode: 'lines', line: { color: '#6ee7b7' } },
    { x: ts, y: data.map(d => d.pm10),  name: 'PM10 µg/m³',  type: 'scatter', mode: 'lines', line: { color: '#f472b6' } },
    { x: ts, y: data.map(d => d.pm1_0), name: 'PM1.0 µg/m³', type: 'scatter', mode: 'lines', line: { color: '#94a3b8' } },
  ], {
    ...layout,
    yaxis: { ...layout.xaxis, title: 'µg/m³' },
  }, config);

  // Chart 4: Sound + Light
  Plotly.react('chart4', [
    { x: ts, y: data.map(d => d.sound_avg),  name: 'Sound avg',  type: 'scatter', mode: 'lines', line: { color: '#a78bfa' } },
    { x: ts, y: data.map(d => d.sound_peak), name: 'Sound peak', type: 'scatter', mode: 'lines', line: { color: '#c084fc' } },
    { x: ts, y: data.map(d => d.light),      name: 'Light lux',  type: 'scatter', mode: 'lines', line: { color: '#facc15' } },
  ], {
    ...layout,
    yaxis: { ...layout.xaxis },
  }, config);

  document.getElementById('doc-count').textContent =
    `loaded ${data.length} documents from ${RANGE_CONFIG[range].collection}`;
}

async function loadRange(range) {
  currentRange = range;
  document.querySelectorAll('.range-buttons button').forEach(b =>
    b.classList.toggle('active', b.dataset.range === range));

  document.getElementById('doc-count').textContent = 'loading…';

  try {
    const data = await fetchData(range);
    const last = data.at(-1);
    document.getElementById('last-updated').textContent = last
      ? `last updated: ${new Date(last.timestamp * 1000).toLocaleString()}`
      : 'no data';
    plotCharts(data, range);
  } catch (err) {
    document.getElementById('doc-count').textContent = `error: ${err.message}`;
  }
}

function scheduleLoad(range) {
  clearTimeout(debounceTimer);
  debounceTimer = setTimeout(() => loadRange(range), 500);
}

async function renderDashboard() {
  document.body.innerHTML = `
    <div style="max-width:900px;margin:0 auto;padding:1.5rem 1.25rem">
      <div class="dash-header">
        <h1>ambient monitor</h1>
        <span class="last-updated" id="last-updated"></span>
      </div>
      <div class="range-buttons">
        ${RANGES.map(r => `<button data-range="${r}">${r}</button>`).join('')}
      </div>
      <div class="chart-wrap"><div id="chart1"></div></div>
      <div class="chart-wrap"><div id="chart2"></div></div>
      <div class="chart-wrap"><div id="chart3"></div></div>
      <div class="chart-wrap"><div id="chart4"></div></div>
      <p class="doc-count" id="doc-count"></p>
      <footer class="dash-footer"><a href="https://www.noelcaverly.com">noelcaverly.com</a></footer>
    </div>`;

  document.querySelectorAll('.range-buttons button').forEach(btn => {
    btn.addEventListener('click', () => scheduleLoad(btn.dataset.range));
  });

  const app = initializeApp(FIREBASE_CONFIG);
  db = getFirestore(app);
  const auth = getAuth(app);
  await signInAnonymously(auth);

  loadRange('24h');
}

// ---- Entry point ----

if (sessionStorage.getItem('dash_authed') === '1') {
  renderDashboard();
} else {
  renderGate();
}
