# cloud/ — dashboard, Firestore rules, rollup

Everything off-device: the web dashboard that charts readings, the Firestore security rules, and the rollup script that downsamples old data. Moved here from the noelcaverly.com repo on 2026-10-07; the dashboard is not currently hosted anywhere.

```
cloud/
├── dashboard/       static page: index.html, dashboard.js, style.css (no build step)
├── firebase/        firestore.rules
└── scripts/         rollup.js + package.json (firebase-admin only)
```
The workflow lives at `.github/workflows/rollup.yml` (repo root, as GitHub requires).

## Firebase setup (manual)
- [x] Firebase project `ambient-monitor-f9e46`
- [ ] Firestore in production mode
- [ ] Anonymous Authentication (dashboard reads)
- [ ] Email/Password Authentication → device user whose credentials go in `src/secrets.h`
- [ ] Deploy rules: `firebase deploy --only firestore:rules` (from `cloud/firebase`)
- [ ] TTL policy on `raw.expireAt`
- [ ] Service account JSON → GitHub secret `FIREBASE_SERVICE_ACCOUNT` on this repo

## Collections
| Collection | Written by | Contents |
|---|---|---|
| `raw` | ESP32 (Email/Password user) | one doc per reading, schema in `src/firestore_upload.cpp` |
| `agg_30m` | rollup (Admin SDK) | 30-min averages of `raw` older than 3 months, doc id = window start |
| `agg_1h` | rollup (Admin SDK) | 1-h averages of `agg_30m` older than 12 months |

Aggregates average only the fields present in the source docs and carry `count` = number of raw samples; the 1-h tier weights by that count. Writes happen before deletes so a crash only leaves duplicates that the next run cleans up.

## Dashboard
- Password gate: SHA-256 of the input compared to `EXPECTED_HASH` in `dashboard.js`, remembered in `sessionStorage` (`dash_authed`). New hash: `echo -n "yourpassword" | shasum -a 256`. This is a curtain, not security; the rules are what protect the data.
- Signs in anonymously, then queries one collection per range. Every query has a timestamp bound:

  | Range | Collection |
  |---|---|
  | 6h, 24h (default), 7d, 30d | `raw` |
  | 3m | `agg_30m` |
  | 1y, all | `agg_1h` |
- Results cached in `sessionStorage` (`sensor_cache_<range>`, 5 min for ≤24h, 30 min otherwise); range clicks are debounced 500 ms.
- Preview locally: `python -m http.server 3000 --directory cloud/dashboard`
- If it goes back on noelcaverly.com, the Cloudflare rule there blocks `/dashboard` except from home.

## Rollup
Manual only (`workflow_dispatch`) while the monitor isn't uploading. To run nightly, add the `schedule` block shown in the workflow's comment. GitHub disables scheduled workflows after 60 days without commits, so re-enable it from the Actions tab if that happens.
