# cloud/: the Google Sheet data store

Monitors upload their log rows to a Google Sheet through a small Apps Script web app (`apps_script/Code.gs`). The dashboard (`dashboard/`, served by GitHub Pages) reads them back from the same web app. It's free, has no expiry, and the data stays in your own Google Drive.

## One-time setup (about 5 minutes)

1. **Create the Sheet.** In Google Drive: New → Google Sheets. Name it e.g. "Ambient Monitor".
2. **Add the script.** In the Sheet: Extensions → Apps Script. Replace the editor's contents with `apps_script/Code.gs` and save.
3. **Set the upload key.** In the Apps Script editor: Project settings (gear) → Script properties → Add:
   - `UPLOAD_KEY`: any long random string (e.g. a password-manager password, letters and digits only). The monitors need the same value.
   - Optional: `RAW_KEEP_DAYS`, how many days of 10-minute rows to keep (default 365). Older rows are averaged into the `hourly` tab first, so long-term history isn't lost.
4. **Deploy the web app.** Deploy → New deployment → type "Web app":
   - Execute as: **Me**
   - Who has access: **Anyone** (the monitors and the dashboard call it without a Google login; writes still need the key)
   - Deploy, authorize the requested permissions, and copy the **Web app URL** (it ends in `/exec`).
5. **Schedule the hourly rollup.** In the editor: Triggers (clock icon) → Add trigger → function `rollup`, event source "Time-driven", "Day timer", e.g. 2-3 am.
6. **Point each monitor at it.** In the monitor's portal Console (or `nc <monitor> 23`):
   ```
   upload url https://script.google.com/macros/s/.../exec
   upload key <the UPLOAD_KEY>
   upload on
   upload
   ```
   `upload` shows the status; within a minute or so the rows from `log.csv` appear in the `readings` tab. Device details in the portal shows the upload state too.
7. **Point the dashboard at it:** put the same URL in `dashboard/config.js`.

## The dashboard

GitHub Pages publishes it (`.github/workflows/pages.yml`, on pushes to `main` that touch `web/`; repo Settings -> Pages -> Source: **GitHub Actions**). Because the site repo `NoelCav.github.io` is the account's user site with the custom domain `www.noelcaverly.com`, this project site appears at **https://www.noelcaverly.com/Ambient-Monitor/**, and the site's `/monitor/` redirects there. Don't set a custom domain on this repo's Pages: it would take the domain from the homepage.

## Updating the script

Edit `Code.gs` here, paste it into the editor, then Deploy → Manage deployments → edit (pencil) → Version: **New version** → Deploy. Keeping the same deployment keeps the same URL, so monitors and dashboard need no change.

## Notes

- **Duplicates:** the script remembers the newest epoch stored per `device_id` (script properties `last_<device>`) and skips anything older, so device retries never duplicate rows. To re-import after clearing the `readings` tab, delete those properties, then run `upload resend` on the monitor.
- **Size:** about 25 cells per row; a spreadsheet holds 10 million cells. With `RAW_KEEP_DAYS` = 365, two monitors stay far below that indefinitely.
- **New log columns** (after a firmware change) are added to the right of the header row automatically; old rows keep lining up. Columns are never removed automatically: to drop ones the monitors stopped logging, list them in `removeColumns()` and run it once from the editor.
- **Monitor names:** each upload carries the monitor's display name and light calibration (`name_<device>`, `r10_<device>` script properties), which the dashboard uses. Data is keyed by `device_id`, so renaming a monitor (`set name Bedroom`) keeps its history together. After editing `Code.gs`, redeploy as a new version (see above).
- **Rows without a clock time** (logged before NTP synced) are not uploaded.
