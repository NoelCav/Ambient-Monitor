#include "data_log.h"
#include "config.h"
#include "params.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

const char* const DATA_LOG_PATH = "/log.csv";
const char* const DATA_LOG_OLD_PATH = "/log.old.csv";

static const char* const HEADER =
    "epoch,uptime_s,boot,device_id,"
    "temp_c,humidity_pct,pressure_hpa,"
    "pm1_0,pm2_5,pm10,cnt_0_3,cnt_0_5,cnt_1_0,cnt_2_5,cnt_5_0,cnt_10,"
    "sound_avg,sound_peak,sound_p2p,sound_p2p_max,light\n";

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

static void rotate(const char* why) {
    LittleFS.remove(DATA_LOG_OLD_PATH);
    LittleFS.rename(DATA_LOG_PATH, DATA_LOG_OLD_PATH);
    writeHeaderIfEmpty();
    Serial.printf("DataLog: rotated log.csv -> log.old.csv (%s)\n", why);
}

static void rotateIfNeeded() {
    File f = LittleFS.open(DATA_LOG_PATH, FILE_READ);
    if (!f) return;
    size_t size = f.size();
    f.close();
    if (size >= LOG_MAX_BYTES) rotate("size limit");
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
    if (stored != current) rotate("column layout changed");
}

void dataLogSetup() {
    // Partition label "spiffs" is the data partition in the default tables.
    // formatOnFail=true only formats when mounting fails (first boot ever).
    mounted = LittleFS.begin(true, "/littlefs", 10, "spiffs");
    if (!mounted) {
        Serial.println("DataLog: LittleFS mount failed, logging disabled");
        return;
    }

    // Boot counter: lets rows from different power-ups be told apart even
    // before NTP has synced (epoch column empty).
    Preferences p;
    p.begin("datalog", false);
    bootCount = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", bootCount);
    p.end();

    rotateIfHeaderChanged();
    writeHeaderIfEmpty();
    Serial.printf("DataLog: mounted, boot #%u, %u bytes used of %u\n",
        bootCount, (unsigned)dataLogSize(), (unsigned)LittleFS.totalBytes());
}

bool dataLogMounted() { return mounted; }

size_t dataLogSize() {
    if (!mounted) return 0;
    size_t total = 0;
    for (const char* path : {DATA_LOG_PATH, DATA_LOG_OLD_PATH}) {
        File f = LittleFS.open(path, FILE_READ);
        if (f) {
            total += f.size();
            f.close();
        }
    }
    return total;
}

void dataLogClear() {
    if (!mounted) return;
    LittleFS.remove(DATA_LOG_PATH);
    LittleFS.remove(DATA_LOG_OLD_PATH);
    writeHeaderIfEmpty();
    Serial.println("DataLog: cleared");
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
    char pm[80] = ",,,,,,,,";
    if (r.pms_valid) {
        snprintf(pm, sizeof(pm), "%u,%u,%u,%u,%u,%u,%u,%u,%u",
            r.pm1_0, r.pm2_5, r.pm10,
            r.cnt_0_3, r.cnt_0_5, r.cnt_1_0, r.cnt_2_5, r.cnt_5_0, r.cnt_10);
    }

    char line[256];
    int n = snprintf(line, sizeof(line), "%s,%lu,%u,%s,%s,%s,%d,%d,%d,%d,%d\n",
        synced ? String((long)now).c_str() : "",
        r.timestamp_ms / 1000, bootCount, params().deviceId.c_str(),
        env, pm, r.sound_avg, r.sound_peak, r.sound_p2p, r.sound_p2p_max, r.light);

    File f = LittleFS.open(DATA_LOG_PATH, FILE_APPEND);
    if (!f) {
        Serial.println("DataLog: open for append failed");
        return;
    }
    f.write((const uint8_t*)line, n < (int)sizeof(line) ? n : sizeof(line) - 1);
    f.close();
}
