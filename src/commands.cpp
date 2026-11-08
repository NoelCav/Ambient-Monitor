#include "commands.h"
#include "climate.h"
#include "config.h"
#include "console.h"
#include "data_log.h"
#include "dust.h"
#include "health.h"
#include "params.h"
#include "sampler.h"
#include "tasks.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <time.h>

static const char* const HELP =
    "Commands (case-insensitive). Sensors: climate = temp/humidity/pressure (BME280),\n"
    "dust = particles (PMS5003), sound, light.\n"
    "\n"
    "READINGS\n"
    "  status                    uptime, WiFi, mode, sensors, next sample, log, task stacks\n"
    "  read [what]               the latest test reading (refreshed every 5 s), not logged.\n"
    "                            what: all (default) | climate | dust | sound | light\n"
    "                            e.g.  read        read dust\n"
    "                            dust needs the sensor awake: 'sample' or test mode.\n"
    "                            sound = RMS avg / loudest 125 ms, light = mean (0-4095)\n"
    "  sample                    take one logged sample. If the dust sensor is asleep\n"
    "                            it wakes, warms up (~90 s), then samples. The regular\n"
    "                            interval restarts from this sample.\n"
    "\n"
    "SETTINGS (apply immediately and are saved)\n"
    "  test on | test off        test mode: dust sensor stays on, live values every 5 s.\n"
    "                            'off': it sleeps until 90 s before each sample.\n"
    "  set sample <seconds>      sample interval, 1-86400.  e.g.  set sample 30\n"
    "\n"
    "WIFI (up to 5 saved networks; the strongest one in range is used)\n"
    "  wifi                      saved networks and the current connection\n"
    "  wifi add <ssid> [pass]    add a network, or change a saved one's password.\n"
    "                            Quote an SSID with spaces; omit pass for an open one.\n"
    "                            e.g.  wifi add \"Home Net\" hunter22hunter\n"
    "  wifi rm <ssid>            remove a saved network (not the last one)\n"
    "\n"
    "LOGS (one CSV row per sample; survive firmware updates)\n"
    "  log                       row count, size, free storage\n"
    "  log tail [n]              print the last n rows (default 10).  e.g.  log tail 5\n"
    "  log save <name> [range]   copy rows to saved/<name>.csv to keep an event.\n"
    "                            range: omitted = whole log | N = last N rows |\n"
    "                                   Nm / Nh = rows from the last N minutes / hours\n"
    "                            e.g.  log save garage-sweep 20\n"
    "                                  log save overnight 8h\n"
    "                            name: letters, digits, - and _ (spaces become -)\n"
    "  log list                  current and saved logs with sizes\n"
    "  log mv <name> <new>       rename a saved log.  e.g.  log mv garage-sweep sweep-1\n"
    "  log rm <name>             delete a saved log.  e.g.  log rm garage-sweep\n"
    "  log clear                 delete the current log (saved logs are kept)\n"
    "  When log.csv fills (512 KB) it moves to saved/archive-full-<start time>.csv;\n"
    "  a column change (firmware update) makes archive-columns-*. The oldest\n"
    "  archives are deleted if storage runs short.\n"
    "  Download: /log.csv, /saved.csv?name=<name>\n"
    "\n"
    "DIAGNOSTICS\n"
    "  diag climate              BME280 chip ID, calibration words, forced-measurement\n"
    "                            test, then re-initialize the sensor\n"
    "  diag i2c                  list devices on the I2C bus (climate sensor is 0x76)\n"
    "  reboot                    restart in 1 s\n"
    "\n"
    "Run from the web Console tab, telnet/nc to port 23, USB serial (115200), or\n"
    "  curl \"http://<device>.local/cmd?c=read%20dust\"\n";

static unsigned long rebootAt = 0;  // 0 = none pending

static void printLogStats() {
    DataLogStats s = dataLogStats();
    Console.printf("log.csv: %u rows, %u KB, room for ~%u more; %u KB free\n", (unsigned)s.rows,
        (unsigned)(s.bytes / 1024), (unsigned)s.rowsLeft, (unsigned)(s.freeBytes / 1024));
}

static void printStatus() {
    SamplerState s = samplerState();
    DustStatus d = dustStatus();
    Console.printf("uptime %lu s, reset: %s, heap %u KB\n",
        millis() / 1000, resetReasonStr(), (unsigned)(ESP.getFreeHeap() / 1024));
    Console.printf("wifi: %s, ip %s, rssi %d dBm\n", wifiConnected() ? "connected" : "disconnected",
        WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    Console.printf("mode: %s, sample every %u s, next in %lu s%s\n", params().testMode ? "test" : "normal",
        (unsigned)params().sampleIntervalS, (unsigned long)s.nextSampleS, s.requested ? " (sample requested)" : "");
    Console.printf("climate: %s, dust: %s", climateFound() ? "ok" : "not found", d.on ? "awake" : "asleep");
    if (d.on) Console.printf(" %lu s%s", (unsigned long)(d.awakeMs / 1000), d.awakeMs < PMS_WARMUP_MS ? " (warming up)" : "");
    Console.println();
    printLogStats();
    tasksPrint(Console);
}

static void readCmd(const String& what) {
    if (what != "" && what != "all" && what != "climate" && what != "dust" && what != "sound" && what != "light") {
        Console.println("read what? all | climate | dust | sound | light");
        return;
    }
    Reading r = samplerState().live;
    if (!r.at_ms) {
        Console.println("no reading yet (first one comes within 5 s of boot)");
        return;
    }
    Console.println(readingText(r, what == "" ? "all" : what.c_str()));
}

static void sampleCmd() {
    samplerRequestSample();
    DustStatus d = dustStatus();
    if (d.on && d.awakeMs >= PMS_WARMUP_MS) Console.println("sample: dust sensor is warm, sampling now");
    else Console.printf("sample: %s the dust sensor, sampling in ~%lu s\n", d.on ? "warming up" : "waking",
        (unsigned long)((PMS_WARMUP_MS - (d.on ? d.awakeMs : 0)) / 1000));
}

static void setTest(bool on) {
    Params p = params();
    p.testMode = on;
    paramsSave(p);
    Console.printf("test mode %s%s\n", on ? "on" : "off",
        on ? "" : " (the dust sensor sleeps until shortly before each sample)");
}

static void setSampleInterval(uint32_t seconds) {
    Params p = params();
    p.sampleIntervalS = seconds;
    paramsSave(p);
    Console.printf("sample interval %u s\n", (unsigned)params().sampleIntervalS);
}

// "<ssid> <rest>" where the SSID may be "quoted" to include spaces.
static void splitSsid(const String& arg, String& ssid, String& rest) {
    int end;
    if (arg.startsWith("\"")) {
        end = arg.indexOf('"', 1);
        if (end < 0) end = arg.length();
        ssid = arg.substring(1, end);
        end++;
    } else {
        end = arg.indexOf(' ');
        if (end < 0) end = arg.length();
        ssid = arg.substring(0, end);
    }
    rest = end < (int)arg.length() ? arg.substring(end) : "";
    rest.trim();
}

static void cmdWifi(const String& sub, const String& arg) {
    String ssid, pass, err;
    splitSsid(arg, ssid, pass);
    if (sub == "" || sub == "list") {
        for (const WifiNet& w : params().wifi) {
            bool cur = wifiConnected() && WiFi.SSID() == w.ssid;
            Console.printf("  %-32s %s%s\n", w.ssid.c_str(), w.pass.length() ? "secured" : "open",
                cur ? (", connected " + String(WiFi.RSSI()) + " dBm").c_str() : "");
        }
        if (!wifiConnected()) Console.printf("  not connected (setup AP '%s' is on)\n", AP_SSID);
    } else if (sub == "add") {
        if (!wifiAdd(ssid, pass, err)) Console.printf("wifi add failed: %s\n", err.c_str());
    } else if (sub == "rm") {
        if (!wifiRemove(ssid, err)) Console.printf("wifi rm failed: %s\n", err.c_str());
    } else {
        Console.println("wifi what? list | add <ssid> [pass] | rm <ssid>");
    }
}

static void printSaved(const String& name, size_t bytes, void* ctx) {
    Console.printf("  saved/%-31s %6u KB\n", (name + ".csv").c_str(), (unsigned)((bytes + 1023) / 1024));
    (*(int*)ctx)++;
}

static void cmdLog(const String& sub, const String& arg) {
    if (!dataLogMounted()) { Console.println("log storage not mounted"); return; }
    if (sub == "") {
        printLogStats();
    } else if (sub == "tail") {
        long n = arg.length() ? arg.toInt() : 10;
        dataLogPrintTail(n > 0 ? n : 10);
    } else if (sub == "save") {
        // arg = "<name> [range]"
        int sp = arg.indexOf(' ');
        String name = sp < 0 ? arg : arg.substring(0, sp);
        String range = sp < 0 ? "" : arg.substring(sp + 1);
        range.trim();
        range.toLowerCase();
        size_t lastRows = 0;
        long sinceEpoch = 0;
        String err;
        if (!name.length()) {
            Console.println("usage: log save <name> [N | Nm | Nh]   (see help)");
            return;
        }
        if (range.length() && range != "all") {
            char unit = range[range.length() - 1];
            long n = range.toInt();
            if (n <= 0) {
                Console.println("range must be N rows, Nm or Nh (e.g. 20, 30m, 2h)");
                return;
            }
            if (unit == 'm' || unit == 'h') {
                if (!timeSynced()) {
                    Console.println("clock not synced yet; use a row count instead (e.g. log save x 20)");
                    return;
                }
                sinceEpoch = (long)time(nullptr) - n * (unit == 'h' ? 3600L : 60L);
            } else {
                lastRows = n;
            }
        }
        if (!dataLogSave(name, lastRows, sinceEpoch, err)) Console.printf("log save failed: %s\n", err.c_str());
    } else if (sub == "list") {
        DataLogStats s = dataLogStats();
        Console.printf("  log.csv                         %6u KB (current)\n", (unsigned)(s.bytes / 1024));
        int count = 0;
        dataLogForEachSaved(printSaved, &count);
        if (!count) Console.println("  (no saved logs)");
        Console.printf("  %u KB free\n", (unsigned)(s.freeBytes / 1024));
    } else if (sub == "mv") {
        int sp = arg.indexOf(' ');
        String err;
        if (sp < 0) Console.println("usage: log mv <name> <new-name>");
        else if (!dataLogRenameSaved(arg.substring(0, sp), arg.substring(sp + 1), err))
            Console.printf("log mv failed: %s\n", err.c_str());
    } else if (sub == "rm") {
        if (!arg.length()) Console.println("usage: log rm <name>");
        else Console.println(dataLogRemoveSaved(arg) ? "removed" : "no such saved log");
    } else if (sub == "clear") {
        dataLogClear();
    } else {
        Console.println("unknown log command; try 'help'");
    }
}

void commandRun(const String& raw) {
    String line = raw;
    line.trim();
    if (!line.length()) return;
    // Echo the command, but never a WiFi password.
    String shown = line, low = line;
    low.toLowerCase();
    if (low.startsWith("wifi add ")) {
        String ssid, pass;
        splitSsid(line.substring(9), ssid, pass);
        shown = "wifi add \"" + ssid + "\"" + (pass.length() ? " ********" : "");
    }
    Console.printf("> %s\n", shown.c_str());

    // Split into up to three words: cmd, sub, rest.
    String words[3];
    int n = 0, i = 0;
    while (n < 3 && i < (int)line.length()) {
        while (i < (int)line.length() && line[i] == ' ') i++;
        int start = i;
        if (n == 2) i = line.length();  // last word takes the remainder
        else while (i < (int)line.length() && line[i] != ' ') i++;
        if (i > start) words[n++] = line.substring(start, i);
    }
    String cmd = words[0], sub = words[1], arg = words[2];
    cmd.toLowerCase();
    sub.toLowerCase();

    if (cmd == "help" || cmd == "?") {
        Console.print(HELP);
    } else if (cmd == "status") {
        printStatus();
    } else if (cmd == "read") {
        readCmd(sub);
    } else if (cmd == "sample") {
        sampleCmd();
    } else if (cmd == "test") {
        if (sub == "on" || sub == "off") setTest(sub == "on");
        else Console.printf("test mode is %s (test on|off)\n", params().testMode ? "on" : "off");
    } else if (cmd == "set" && sub == "sample") {
        long s = arg.toInt();
        if (s >= 1 && s <= 86400) setSampleInterval(s);
        else Console.println("usage: set sample <1-86400 seconds>");
    } else if (cmd == "log") {
        cmdLog(sub, arg);
    } else if (cmd == "wifi") {
        cmdWifi(sub, arg);
    } else if (cmd == "diag") {
        if (sub == "climate") climateDiag();
        else if (sub == "i2c") i2cScan();
        else Console.println("diag what? climate | i2c");
    } else if (cmd == "reboot") {
        Console.println("rebooting in 1 s...");
        rebootAt = millis() + 1000;  // let console/HTTP clients get the reply first
    } else {
        Console.printf("unknown command '%s'; try 'help'\n", cmd.c_str());
    }
}

void commandsSetup() {
    consoleSetCommandHandler(commandRun);
}

void commandsLoop() {
    if (rebootAt && (long)(millis() - rebootAt) >= 0) ESP.restart();
}
