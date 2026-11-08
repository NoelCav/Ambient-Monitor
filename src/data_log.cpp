#include "data_log.h"
#include "console.h"
#include "config.h"
#include "params.h"
#include "health.h"
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
    "sound_avg_p2p,sound_peak_p2p,light\n";

static bool mounted = false;
static uint32_t bootCount = 0;

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
// saved/archive-<reason>-<start time>.csv: reason is full, columns or migrated;
// the time is the log's first row (UTC). If storage is short, the oldest archives are deleted first;
// user-named saved logs are never deleted automatically.
static void archive(const char* path, const char* reason, const char* why) {
    LittleFS.mkdir(DATA_LOG_SAVED_DIR);
    long e = firstEpoch(path);
    String name;
    if (e > 1704067200) {
        time_t t = e;
        struct tm tm;
        localtime_r(&t, &tm);
        char b[24];
        strftime(b, sizeof(b), "%Y%m%d-%H%M", &tm);
        name = String(ARCHIVE_PREFIX) + reason + "-" + b;
    } else {
        name = String(ARCHIVE_PREFIX) + reason + "-boot" + String(bootCount);
    }
    String dst = String(DATA_LOG_SAVED_DIR) + "/" + name + ".csv";
    for (int i = 2; LittleFS.exists(dst); i++) dst = String(DATA_LOG_SAVED_DIR) + "/" + name + "-" + i + ".csv";
    if (!LittleFS.rename(path, dst)) {
        LittleFS.remove(path);
        Console.printf("DataLog: archive of %s failed; deleted it (%s)\n", path, why);
    } else {
        Console.printf("DataLog: %s -> %s (%s)\n", path, dst.c_str(), why);
    }
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < LOG_MAX_BYTES + SAVE_HEADROOM) {
        String old = oldestArchive();
        if (!old.length()) break;
        LittleFS.remove(String(DATA_LOG_SAVED_DIR) + "/" + old);
        Console.printf("DataLog: storage low, deleted oldest archive %s\n", old.c_str());
    }
}

static void rotate(const char* reason, const char* why) {
    archive(DATA_LOG_PATH, reason, why);
    writeHeaderIfEmpty();
}

static void rotateIfNeeded() {
    File f = LittleFS.open(DATA_LOG_PATH, FILE_READ);
    if (!f) return;
    size_t size = f.size();
    f.close();
    if (size >= LOG_MAX_BYTES) rotate("full", "size limit");
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
    if (stored != current) rotate("columns", "column layout changed");
}

void dataLogSetup() {
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
    if (LittleFS.exists(LEGACY_OLD_PATH)) archive(LEGACY_OLD_PATH, "migrated", "migrating log.old.csv");
    rotateIfHeaderChanged();
    writeHeaderIfEmpty();
    Console.printf("DataLog: mounted, boot #%u, %u bytes used of %u\n",
        bootCount, (unsigned)dataLogSize(), (unsigned)LittleFS.totalBytes());
}

bool dataLogMounted() { return mounted; }

size_t dataLogSize() {
    if (!mounted) return 0;
    File f = LittleFS.open(DATA_LOG_PATH, FILE_READ);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
}

void dataLogClear() {
    if (!mounted) return;
    LittleFS.remove(DATA_LOG_PATH);
    writeHeaderIfEmpty();
    Console.println("DataLog: cleared");
}

void dataLogAppend(const SensorReading& r) {
    if (!mounted) return;
    rotateIfNeeded();

    time_t now;
    time(&now);
    bool synced = now > 1704067200;  // same "NTP synced" threshold as elsewhere

    // Invalid sensors leave their columns empty rather than logging zeros.
    char env[48] = ",,";
    if (r.bme_valid) {
        snprintf(env, sizeof(env), "%.2f,%.1f,%.1f", r.temperature, r.humidity, r.pressure);
    }
    char pm[128] = ",,,,,,,,,,,,";
    if (r.pms_valid) {
        snprintf(pm, sizeof(pm), "%u,%u,%u,%u,%u,%u,%lu,%u,%u,%u,%u,%u,%u",
            r.pm1_0, r.pm2_5, r.pm10, r.pm1_0_cf1, r.pm2_5_cf1, r.pm10_cf1, (unsigned long)r.pms_awake_s,
            r.cnt_0_3, r.cnt_0_5, r.cnt_1_0, r.cnt_2_5, r.cnt_5_0, r.cnt_10);
    }

    char line[256];
    int n = snprintf(line, sizeof(line), "%s,%lu,%u,%s,%s,%s,%s,%d,%d,%d\n",
        synced ? String((long)now).c_str() : "",
        r.timestamp_ms / 1000, bootCount, resetReasonStr(), params().deviceId.c_str(),
        env, pm, r.sound_avg, r.sound_peak, r.light);

    File f = LittleFS.open(DATA_LOG_PATH, FILE_APPEND);
    if (!f) {
        Console.println("DataLog: open for append failed");
        return;
    }
    f.write((const uint8_t*)line, n < (int)sizeof(line) ? n : sizeof(line) - 1);
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

static size_t fileSize(const char* path) {
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
}

// Copy log.csv (header always included) to saved/<name>.csv. lastRows > 0
// keeps only the last N rows; sinceEpoch > 0 keeps only rows stamped at or
// after that time (rows without a clock time are skipped). Both 0 = whole log.
bool dataLogSave(const String& rawName, size_t lastRows, long sinceEpoch, String& err) {
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
    if (lastRows) {
        size_t rows = dataLogRowCount();
        skip = rows > lastRows ? rows - lastRows : 0;
    }

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
    return mounted && name.length() && LittleFS.remove(dataLogSavedPath(name));
}

void dataLogForEachSaved(void (*fn)(const String& name, size_t bytes, void* ctx), void* ctx) {
    if (!mounted) return;
    File dir = LittleFS.open(DATA_LOG_SAVED_DIR);
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();  // basename, e.g. "garage.csv"
        if (name.endsWith(".csv")) fn(name.substring(0, name.length() - 4), f.size(), ctx);
        f.close();
    }
}

size_t dataLogFreeBytes() {
    return mounted ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0;
}

// Data rows in log.csv (lines minus the header).
size_t dataLogRowCount() {
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

// Print the header and the last n rows of log.csv to the console. Two passes
// (count, then skip) keep it simple; fine for a manual command.
void dataLogPrintTail(size_t n) {
    size_t rows = dataLogRowCount();
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
