import { initializeApp, cert } from 'firebase-admin/app';
import { getFirestore } from 'firebase-admin/firestore';

const serviceAccount = JSON.parse(process.env.FIREBASE_SERVICE_ACCOUNT);
initializeApp({ credential: cert(serviceAccount) });
const db = getFirestore();

const nowSec = Math.floor(Date.now() / 1000);
const RAW_CUTOFF   = nowSec - 90  * 24 * 3600; // 3 months
const AGG30_CUTOFF = nowSec - 365 * 24 * 3600; // 12 months

function floorTo(ts, seconds) {
  return Math.floor(ts / seconds) * seconds;
}

async function rollupTier({ srcCol, dstCol, cutoff, windowSec, label }) {
  const snap = await db.collection(srcCol).where('timestamp', '<', cutoff).get();
  if (snap.empty) return { rolled: 0, deleted: 0 };

  const SENSOR_FIELDS = ['temperature', 'humidity', 'pressure', 'pm1_0', 'pm2_5', 'pm10', 'sound_avg', 'sound_peak', 'light'];

  const windows = new Map();
  for (const doc of snap.docs) {
    const d = doc.data();
    const key = floorTo(d.timestamp, windowSec);
    if (!windows.has(key)) {
      const sums = {}; const counts = {};
      for (const f of SENSOR_FIELDS) { sums[f] = 0; counts[f] = 0; }
      windows.set(key, { docs: [], samples: 0, sums, counts });
    }
    const w = windows.get(key);
    w.docs.push(doc.ref);
    // Raw docs weigh 1; aggregate docs weigh by the samples they summarize
    const weight = d.count ?? 1;
    w.samples += weight;
    for (const f of SENSOR_FIELDS) {
      if (d[f] != null) { w.sums[f] += d[f] * weight; w.counts[f] += weight; }
    }
  }

  // Writes first (crash-safe)
  let writeBatch = db.batch();
  let writeCount = 0;
  for (const [windowStart, w] of windows) {
    const avg = {};
    for (const f of SENSOR_FIELDS) {
      if (w.counts[f] > 0) avg[f] = w.sums[f] / w.counts[f];
    }
    writeBatch.set(db.collection(dstCol).doc(String(windowStart)), {
      timestamp: windowStart,
      ...avg,
      count: w.samples,
    }, { merge: true });
    writeCount++;
    if (writeCount % 499 === 0) {
      await writeBatch.commit();
      writeBatch = db.batch();
    }
  }
  await writeBatch.commit();

  // Deletes in batches of 500
  const allRefs = snap.docs.map(d => d.ref);
  for (let i = 0; i < allRefs.length; i += 500) {
    const batch = db.batch();
    allRefs.slice(i, i + 500).forEach(ref => batch.delete(ref));
    await batch.commit();
  }

  return { rolled: windows.size, deleted: allRefs.length };
}

try {
  const r1 = await rollupTier({ srcCol: 'raw', dstCol: 'agg_30m', cutoff: RAW_CUTOFF,   windowSec: 1800, label: 'raw→agg_30m' });
  const r2 = await rollupTier({ srcCol: 'agg_30m', dstCol: 'agg_1h', cutoff: AGG30_CUTOFF, windowSec: 3600, label: 'agg_30m→agg_1h' });
  console.log(`rolled up ${r1.deleted} raw → ${r1.rolled} agg_30m, ${r2.deleted} agg_30m → ${r2.rolled} agg_1h`);
} catch (err) {
  console.error(err);
  process.exit(1);
}
