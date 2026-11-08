#include "data_log.h"
#include "console.h"
#include "config.h"
#include "params.h"
#include "health.h"
#include "tasks.h"
#include "wifi_manager.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

const char* const DATA_LOG_PATH = "/log.csv";
static const char* const LEGACY_OLD_PATH = "/log.old.csv";  // pre-archive rotation target
const char* const DATA_LOG_SAVED_DIR = "/saved";
static const size_t SAVE_HEADROOM = 32 * 1024;  // keep free for the live log to grow
static const char* const ARCHIVE_PREFIX = "archive-";

static const char* const HEADER =
    "epoch,uptime_s,boot,reset,device_id,"
    "temp_c,humidity_pct,pressure_hpa,"
    "pm1_0,pm2_5,pm10,pm1_0_cf1,pm2_5_cf1,pm10_cf1,pms_awake_s,cnt_0_3,cnt_0_5,cnt_1_0,cnt_2_5,cnt_5_0,cnt_10,"
    "sound_avg_rms,sound_peak_rms,light\n";

static bool mounted = false;
// The sampler task appends while the loop task (portal, console) saves,
// renames and lists: every public function holds this mutex. Internal helpers
// (countRows, rotate, archive ...) assume it is already held.
static SemaphoreHandle_t mtx;  // created in dataLogSetup(), before any task starts
static uint32_t bootCount = 0;
static size_t rows = 0;  // data rows in log.csv: counted once at boot, then tracked

static size_t fileSize(const char* path) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
}

// Data rows in log.csv (lines minus the header), by reading the whole file.
// Only at boot; afterwards `rows` is kept up to date by append/rotate/clear.
static size_t countRows() {
    File f = LittleFS.open(DATA_LOG_PATH, FILE_READ);
    if (!f) return 0;
    size_t lines = 0;
    uint8_t buf[512];
    size_t n;
    while ((n = f.read(buf, sizeof(buf))) > 0) {
        for (size_t i = 0; i < n; i++) if (buf[i] == '\n') lines++;
    }
    f.close();
    return lines ? lines - 1 : 0;
}

static void writeHeaderIfEmpty() {
    if (LittleFS.exists(DATA_LOG_PATH)) return;
    File f = LittleFS.open(DATA_LOG_PATH, FILE_WRITE);
    if (f) {
        f.print(HEADER);
        f.close();
    }
}

// Epoch of the first stamped row in a log file (0 if none).
static long firstEpoch(const char* path) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return 0;
    f.readStringUntil('\n');  // header
    long e = 0;
    for (int i = 0; i < 50 && f.available() && !e; i++) e = f.readStringUntil('\n').toInt();
    f.close();
    return e;
}

// Archive names are archive-<reason>-<YYYYMMDD-HHMM | bootN>; this returns the
// time part, so archives compare in time order whatever the reason
// (bootN, used when the clock never synced, sorts after the dated ones).
static String archiveTime(const String& n) {
    int a = n.indexOf('-', strlen(ARCHIVE_PREFIX));
    return a < 0 ? n : n.substring(a + 1);
}

// Oldest archive first, by the time part of the name.
static String oldestArchive() {
    String best;
    File dir = LittleFS.open(DATA_LOG_SAVED_DIR);
    if (!dir || !dir.isDirectory()) return best;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String n = f.name();
        f.close();
        if (n.startsWith(ARCHIVE_PREFIX) && n.endsWith(".csv") && (!best.length() || archiveTime(n) < archiveTime(best))) best = n;
    }
    return best;
}

// Move a full (or outdated-layout) log into the saved list as
// saved/archive-<reason>-<start time>.csv. reason: full, columns or migrated;
// the time is the log's first row, in UTC. Then, if storage is short, delete
// the oldest archives (user-named saved logs are never deleted).
static void archive(const char* path, const char* reason) {
    LittleFS.mkdir(DATA_LOG_SAVED_DIR);
    long e = firstEpoch(path);
    String name;
    if (e > 0) {
        time_t t = e;
        struct tm tm;
        gmtime_r(&t, &tm);
        char b[24];
        strftime(b, sizeof(b), "%Y%m%d-%H%M", &tm);
        name = String(ARCHIVE_PREFIX) + reason + "-" + b;
    } else {
        name = String(ARCHIVE_PREFIX) + reason + "-boot" + String(bootCount);
    }
    String dst = String(DATA_LOG_SAVED_DIR) + "/" + name + ".csv";
    for (int i = 2; LittleFS.exists(dst); i++) dst = String(DATA_LOG_SAVED_DIR) + "/" + name + "-" + i + ".csv";
    // A failed rename leaves the log where it is (it keeps growing) rather
    // than losing data.
    if (LittleFS.rename(path, dst)) Console.printf("DataLog: %s -> %s\n", path, dst.c_str());
    else Console.printf("DataLog: could not archive %s (%s); keeping it\n", path, reason);
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < LOG_MAX_BYTES + SAVE_HEADROOM) {
        String old = oldestArchive();
        if (!old.length()) break;
        LittleFS.remove(String(DATA_LOG_SAVED_DIR) + "/" + old);
        Console.printf("DataLog: storage low, deleted oldest archive %s\n", old.c_str());
    }
}

static void rotate(const char* reason) {
    archive(DATA_LOG_PATH, reason);
    writeHeaderIfEmpty();
    rows = countRows();  // 0, unless the archive rename failed and the log stayed
}

static void rotateIfNeeded() {
    if (fileSize(DATA_LOG_PATH) >= LOG_MAX_BYTES) rotate("full");
}

// The log outlives firmware updates, so a build that changes the columns would
// otherwise append rows under the old header and misalign every reader. Keep
// one layout per file: if the stored header differs, start a new file.
static void rotateIfHeaderChanged() {
    File f = LittleFS.open(DATA_LOG_PATH, FILE_READ);
    if (!f) return;
    String stored = f.readStringUntil('\n');
    f.close();
    String current = HEADER;
    current.trim();
    stored.trim();
    if (stored != current) rotate("columns");
}

void dataLogSetup() {
    mtx = xSemaphoreCreateMutex();
    Guard g(mtx);
    // Partition label "spiffs" is the data partition in the default tables.
    // formatOnFail=true only formats when mounting fails (first boot ever).
    mounted = LittleFS.begin(true, "/littlefs", 10, "spiffs");
    if (!mounted) {
        Console.println("DataLog: LittleFS mount failed, logging disabled");
        return;
    }

    // Boot counter: lets rows from different power-ups be told apart even
    // before NTP has synced (epoch column empty).
    Preferences p;
    p.begin("datalog", false);
    bootCount = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", bootCount);
    p.end();

    // Logs rotated by older firmware went to log.old.csv; move it into the list.
    if (LittleFS.exists(LEGACY_OLD_PATH)) archive(LEGACY_OLD_PATH, "migrated");
    rotateIfHeaderChanged();
    writeHeaderIfEmpty();
    rows = countRows();
    Console.printf("DataLog: mounted, boot #%u, %u bytes used of %u\n",
        (unsigned)bootCount, (unsigned)fileSize(DATA_LOG_PATH), (unsigned)LittleFS.totalBytes());
}

bool dataLogMounted() { return mounted; }

void dataLogClear() {
    Guard g(mtx);
    if (!mounted) return;
    LittleFS.remove(DATA_LOG_PATH);
    writeHeaderIfEmpty();
    rows = 0;
    Console.println("DataLog: cleared");
}

void dataLogAppend(const Reading& r) {
    Guard g(mtx);
    if (!mounted) return;
    rotateIfNeeded();

    bool synced = timeSynced();

    // Invalid sensors leave their columns empty rather than logging zeros.
    char env[48] = ",,";
    if (r.climate.ok) {
        snprintf(env, sizeof(env), "%.2f,%.1f,%.1f", r.climate.temp, r.climate.hum, r.climate.pres);
    }
    char pm[128] = ",,,,,,,,,,,,";
    if (r.dustOk) {
        const PmsFrame& d = r.dust;
        snprintf(pm, sizeof(pm), "%u,%u,%u,%u,%u,%u,%lu,%u,%u,%u,%u,%u,%u",
            d.pm[0], d.pm[1], d.pm[2], d.cf1[0], d.cf1[1], d.cf1[2], (unsigned long)r.dustAwakeS,
            d.cnt[0], d.cnt[1], d.cnt[2], d.cnt[3], d.cnt[4], d.cnt[5]);
    }

    char line[256];
    int n = snprintf(line, sizeof(line), "%s,%lu,%u,%s,%s,%s,%s,%.1f,%.1f,%.0f\n",
        synced ? String((long)time(nullptr)).c_str() : "",
        (unsigned long)(r.at_ms / 1000), (unsigned)bootCount, resetReasonStr(), params().deviceId.c_str(),
        env, pm, r.soundAvg, r.soundPeak, r.light);

    File f = LittleFS.open(DATA_LOG_PATH, FILE_APPEND);
    if (!f) {
        Console.println("DataLog: open for append failed");
        return;
    }
    if (f.write((const uint8_t*)line, n < (int)sizeof(line) ? n : sizeof(line) - 1)) rows++;
    f.close();
}

// --- Saved logs and inspection (used by console commands and the portal) ---

// Lowercase [a-z0-9_-], max 31 chars. Empty result means the name was unusable.
String dataLogSanitizeName(const String& in) {
    String out;
    for (unsigned i = 0; i < in.length() && out.length() < 31; i++) {
        char c = tolower(in[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
        else if (c == ' ') out += '-';
    }
    return out;
}

String dataLogSavedPath(const String& name) {
    return String(DATA_LOG_SAVED_DIR) + "/" + name + ".csv";
}

// Copy log.csv (header always included) to saved/<name>.csv. lastRows > 0
// keeps only the last N rows; sinceEpoch > 0 keeps only rows stamped at or
// after that time (rows without a clock time are skipped). Both 0 = whole log.
bool dataLogSave(const String& rawName, size_t lastRows, long sinceEpoch, String& err) {
    Guard g(mtx);
    if (!mounted) { err = "log storage not mounted"; return false; }
    String name = dataLogSanitizeName(rawName);
    if (name.length() == 0) { err = "name must contain letters or digits"; return false; }
    String dst = dataLogSavedPath(name);
    if (LittleFS.exists(dst)) { err = "'" + name + "' already exists (log rm " + name + " first)"; return false; }

    bool whole = lastRows == 0 && sinceEpoch == 0;
    size_t need = whole ? fileSize(DATA_LOG_PATH) : 0;  // subsets are small; write errors are caught below
    size_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (need + SAVE_HEADROOM > free) {
        err = "not enough space: need " + String((need + SAVE_HEADROOM) / 1024) + " KB, " +
              String(free / 1024) + " KB free (log rm an old save, or save fewer rows)";
        return false;
    }

    size_t skip = 0;
    if (lastRows) skip = rows > lastRows ? rows - lastRows : 0;

    LittleFS.mkdir(DATA_LOG_SAVED_DIR);
    File in = LittleFS.open(DATA_LOG_PATH, FILE_READ);
    File out = LittleFS.open(dst, FILE_WRITE);
    if (!in || !out) { err = "could not open files"; return false; }

    size_t kept = 0;
    size_t bytes = 0;  // File::size() reads 0 until close on LittleFS, so count here
    bool ok = true;
    String line = in.readStringUntil('\n');  // header
    ok = out.print(line + "\n") > 0;
    bytes += line.length() + 1;
    while (ok && in.available()) {
        line = in.readStringUntil('\n');
        if (!line.length()) continue;
        if (skip) { skip--; continue; }
        if (sinceEpoch && line.toInt() < sinceEpoch) continue;  // epoch is the first column; "" -> 0
        ok = out.print(line + "\n") == line.length() + 1;
        bytes += line.length() + 1;
        kept++;
    }
    in.close();
    out.close();
    if (!ok) {
        LittleFS.remove(dst);
        err = "write failed (storage full?)";
        return false;
    }
    Console.printf("DataLog: saved %u rows (%u bytes) as %s\n", (unsigned)kept, (unsigned)bytes, dst.c_str());
    return true;
}

bool dataLogRenameSaved(const String& rawFrom, const String& rawTo, String& err) {
    String from = dataLogSanitizeName(rawFrom), to = dataLogSanitizeName(rawTo);
    Guard g(mtx);
    if (!mounted) { err = "log storage not mounted"; return false; }
    if (!from.length() || !LittleFS.exists(dataLogSavedPath(from))) { err = "no saved log '" + from + "'"; return false; }
    if (!to.length()) { err = "new name must contain letters or digits"; return false; }
    if (LittleFS.exists(dataLogSavedPath(to))) { err = "'" + to + "' already exists"; return false; }
    if (!LittleFS.rename(dataLogSavedPath(from), dataLogSavedPath(to))) { err = "rename failed"; return false; }
    Console.printf("DataLog: renamed %s -> %s\n", from.c_str(), to.c_str());
    return true;
}

bool dataLogRemoveSaved(const String& rawName) {
    String name = dataLogSanitizeName(rawName);
    Guard g(mtx);
    return mounted && name.length() && LittleFS.remove(dataLogSavedPath(name));
}

void dataLogForEachSaved(void (*fn)(const String& name, size_t bytes, void* ctx), void* ctx) {
    Guard g(mtx);
    if (!mounted) return;
    File dir = LittleFS.open(DATA_LOG_SAVED_DIR);
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();  // basename, e.g. "garage.csv"
        if (name.endsWith(".csv")) fn(name.substring(0, name.length() - 4), f.size(), ctx);
        f.close();
    }
}

DataLogStats dataLogStats() {
    Guard g(mtx);
    DataLogStats s;
    if (!mounted) return s;
    s.rows = rows;
    s.bytes = fileSize(DATA_LOG_PATH);
    s.freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    // Rows that still fit before the log is archived, from the average row
    // size so far (header excluded; ~120 bytes assumed until there are rows).
    size_t hdr = strlen(HEADER);
    size_t rowBytes = s.rows && s.bytes > hdr ? (s.bytes - hdr) / s.rows : 120;
    s.rowsLeft = s.bytes < LOG_MAX_BYTES ? (LOG_MAX_BYTES - s.bytes) / (rowBytes ? rowBytes : 1) : 0;
    return s;
}

String dataLogRowsAfter(long afterEpoch, size_t maxRows, long& lastEpoch, size_t& pending) {
    Guard g(mtx);
    pending = 0;
    lastEpoch = afterEpoch;
    File f = mounted ? LittleFS.open(DATA_LOG_PATH, FILE_READ) : File();
    if (!f) return "";
    String out = f.readStringUntil('\n') + "\n", taken;
    size_t n = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        long e = line.toInt();  // epoch is the first column; "" -> 0
        if (e <= afterEpoch) continue;
        pending++;
        if (n < maxRows) {
            taken += line + "\n";
            lastEpoch = e;
            n++;
        }
    }
    f.close();
    return n ? out + taken : "";
}

// Print the header and the last n rows of log.csv to the console.
void dataLogPrintTail(size_t n) {
    Guard g(mtx);
    File f = LittleFS.open(DATA_LOG_PATH, FILE_READ);
    if (!f) { Console.println("(no log)"); return; }
    Console.println(f.readStringUntil('\n'));
    for (size_t skip = rows > n ? rows - n : 0; skip > 0 && f.available(); skip--) {
        f.readStringUntil('\n');
    }
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length()) Console.println(line);
    }
    f.close();
}
