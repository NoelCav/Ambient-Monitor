#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include "PMS5003.h"
#include "config.h"
#include "params.h"
#include "sensor_reading.h"
#include "wifi_manager.h"
#include "config_portal.h"
#include "data_log.h"
#include "console.h"
#include "health.h"
#include "commands.h"
#include "device.h"

Adafruit_BME280 bme;
PMS5003 pms;

unsigned long lastSample = 0;
bool pmsOn = false;
unsigned long pmsOnSince = 0;     // millis() when the PMS was last woken
PMS5003::Data lastPmsFrame = {};  // most recent full frame, for live debug
unsigned long lastPmsFrameMs = 0;  // millis() when lastPmsFrame arrived
bool sampleRequested = false;      // set by the `sample` command
bool bmeOk = false;
SensorReading latestReading = {};

// Rolling 1-second sound buffer (samples every 1ms)
const unsigned SOUND_BUF_SIZE = 1000;
const unsigned SOUND_SAMPLE_INTERVAL_MS = 1;
int soundBuf[SOUND_BUF_SIZE];
unsigned soundBufPos = 0;
unsigned soundBufCount = 0;
unsigned long lastSoundTick = 0;
long soundBufSum = 0;
int soundBlkMin = 4095, soundBlkMax = 0;  // current 1s block, for peak-hold
unsigned soundBlkN = 0;
int soundP2pMaxSample = 0;  // loudest 1s block since the last sample
int soundP2pMaxDebug = 0;   // loudest 1s block since the last debug print
long soundP2pSumSample = 0; unsigned soundP2pNSample = 0;  // for the average since the last sample
long soundP2pSumDebug = 0;  unsigned soundP2pNDebug = 0;   // for the average since the last debug print

// Latest test-mode snapshot (refreshed every 5s while live debug is on). Feeds
// both the [PMS]/[ENV] console lines and the portal's live test panel.
struct LiveDiag {
    unsigned long at_ms = 0;  // 0 = no snapshot yet
    unsigned long pms_t_s;    // seconds since the PMS woke
    bool pms_ok;
    PMS5003::Data frame;
    uint32_t frames, bad, bytes;  // in the last 5s window
    bool bme_found, bme_ok;
    float temp, hum, pres;
    int sound_avg5, sound_max5s;  // average / loudest 1 s block over the last 5 s
    long light;               // 1 s average
    uint16_t pm_avg[3];       // PM1/2.5/10 over the rolling frame buffer (same as a sample)
    unsigned pm_avg_n;        // frames in that average
};
LiveDiag liveDiag;

// Readings taken by the boot self-test, shown on the portal (flagged as not
// saved) until the first real sample exists. PM is absent: the dust sensor is
// still warming up at that point.
struct BootReading {
    bool taken = false, bme_ok = false;
    float temp = 0, hum = 0, pres = 0;
    int sound_p2p = 0;
    int light = 0;
};
BootReading bootReading;


// Rolling light buffer (samples every 100ms)
const unsigned LIGHT_BUF_SIZE = 10;
const unsigned LIGHT_SAMPLE_INTERVAL_MS = 100;
int lightBuf[LIGHT_BUF_SIZE];
unsigned lightBufPos = 0;
unsigned lightBufCount = 0;
long lightBufSum = 0;
unsigned long lastLightTick = 0;

// Rolling PMS buffer — averages the last N decoded frames (~1 frame/sec).
const unsigned PMS_BUF_SIZE = 10;  // ~10s window at ~1 frame/sec
uint16_t pm1Buf[PMS_BUF_SIZE];
uint16_t pm25Buf[PMS_BUF_SIZE];
uint16_t pm10Buf[PMS_BUF_SIZE];
unsigned pmsBufPos = 0;
unsigned pmsBufCount = 0;
long pm1Sum = 0;
long pm25Sum = 0;
long pm10Sum = 0;

void resetPmsBuffer() {
    pmsBufPos = 0;
    pmsBufCount = 0;
    pm1Sum = 0;
    pm25Sum = 0;
    pm10Sum = 0;
}

void pushPmsFrame(uint16_t a, uint16_t b, uint16_t c) {
    if (pmsBufCount < PMS_BUF_SIZE) {
        pmsBufCount++;
    } else {
        pm1Sum  -= pm1Buf[pmsBufPos];
        pm25Sum -= pm25Buf[pmsBufPos];
        pm10Sum -= pm10Buf[pmsBufPos];
    }
    pm1Buf[pmsBufPos]  = a;
    pm25Buf[pmsBufPos] = b;
    pm10Buf[pmsBufPos] = c;
    pm1Sum  += a;
    pm25Sum += b;
    pm10Sum += c;
    pmsBufPos = (pmsBufPos + 1) % PMS_BUF_SIZE;
}

// --- Level assessment functions ---

const char* tempLevel(float t) {
    if (t < 0)    return "Freezing";
    if (t < 10)   return "Very Cold";
    if (t < 18)   return "Cold";
    if (t < 22)   return "Cool";
    if (t < 26)   return "Comfortable";
    if (t < 30)   return "Warm";
    if (t < 35)   return "Hot";
    return "Very Hot";
}

const char* humidityLevel(float h) {
    if (h < 20)   return "Very Dry - risk of static, irritation";
    if (h < 30)   return "Dry - consider a humidifier";
    if (h < 50)   return "Comfortable";
    if (h < 60)   return "Slightly Humid";
    if (h < 70)   return "Humid - mold risk begins";
    return "Very Humid - poor air quality risk";
}

const char* pressureLevel(float p) {
    if (p < 980)  return "Very Low - storm likely";
    if (p < 1000) return "Low - unsettled weather";
    if (p < 1013) return "Normal - slight low";
    if (p < 1020) return "Normal";
    if (p < 1040) return "High - fair weather";
    return "Very High";
}

// Levels for 1 s peak-to-peak (KY-037 via divider: quiet room ~6-11, TV ~20, clap 500+).
const char* soundLevel(int v) {
    if (v < 20)  return "Quiet";
    if (v < 100) return "Some noise";
    if (v < 400) return "Loud";
    return "Very loud";
}

const char* lightLevel(int v) {
    if (v > 3800) return "Very Bright - direct sunlight";
    if (v > 3000) return "Bright - indirect sunlight";
    if (v > 2000) return "Bright indoor / overcast outdoor";
    if (v > 1000) return "Indoor lighting";
    if (v > 400)  return "Dim - low light";
    return "Dark - night/blackout";
}

const char* pm25Level(uint16_t v) {
    if (v <= 12)   return "Good";
    if (v <= 35)   return "Moderate";
    if (v <= 55)   return "Unhealthy for Sensitive Groups";
    if (v <= 150)  return "Unhealthy";
    if (v <= 250)  return "Very Unhealthy";
    return "Hazardous";
}

const char* pm10Level(uint16_t v) {
    if (v <= 54)   return "Good";
    if (v <= 154)  return "Moderate";
    if (v <= 254)  return "Unhealthy for Sensitive Groups";
    if (v <= 354)  return "Unhealthy";
    if (v <= 424)  return "Very Unhealthy";
    return "Hazardous";
}

const char* pm1Level(uint16_t v) {
    if (v <= 10)   return "Good";
    if (v <= 25)   return "Moderate";
    if (v <= 45)   return "Unhealthy for Sensitive Groups";
    if (v <= 100)  return "Unhealthy";
    if (v <= 200)  return "Very Unhealthy";
    return "Hazardous";
}

// --- Sampling functions (populate SensorReading) ---

uint8_t bmeReadRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len);

// Find the BME and put it in forced mode with x1 oversampling (Bosch's
// "weather monitoring" setting): it sleeps between reads and only converts
// when asked, which avoids the self-heating of the library's default
// continuous x16 mode.
uint8_t bmeAddr = 0x76;

bool bmeInit() {
    if (bme.begin(0x76))      bmeAddr = 0x76;
    else if (bme.begin(0x77)) bmeAddr = 0x77;
    else return false;
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,   // temperature
                    Adafruit_BME280::SAMPLING_X1,   // pressure
                    Adafruit_BME280::SAMPLING_X1,   // humidity
                    Adafruit_BME280::FILTER_OFF);
    return true;
}

// Trigger one conversion and check it actually happened. The library computes
// a plausible-looking 23.52 C / 82.4 % / 699 hPa from the chip's power-on
// register values (raw temp 0x800000), so a chip that resets mid-conversion
// would otherwise pass as valid.
bool bmeMeasure() {
    if (!bme.takeForcedMeasurement()) return false;
    uint8_t t[3] = {0};
    if (bmeReadRegs(bmeAddr, 0xFA, t, 3)) return false;
    return !(t[0] == 0x80 && t[1] == 0 && t[2] == 0);
}

void sampleBME280(SensorReading& r) {
    if (!bmeOk) bmeOk = bmeInit();

    r.bme_valid = bmeOk && bmeMeasure();
    if (r.bme_valid) {
        r.temperature = bme.readTemperature();
        r.humidity = bme.readHumidity();
        r.pressure = bme.readPressure() / 100.0F;
        r.bme_valid = !isnan(r.temperature) && !isnan(r.humidity) && !isnan(r.pressure);
    }
    if (!r.bme_valid) bmeOk = false;
}

// Max - min over the rolling sound buffer. The KY-037 AO is a DC bias plus the
// audio signal, so the swing tracks loudness better than the average does.
int soundPeakToPeak() {
    if (soundBufCount == 0) return 0;
    int lo = soundBuf[0], hi = soundBuf[0];
    for (unsigned i = 1; i < soundBufCount; ++i) {
        if (soundBuf[i] < lo) lo = soundBuf[i];
        if (soundBuf[i] > hi) hi = soundBuf[i];
    }
    return hi - lo;
}

// Sound over the whole sample interval, from the 1 s peak-to-peak blocks:
// average loudness (steady noise like a TV) and the loudest second (bangs).
// Falls back to the current 1 s window if no block has completed yet.
void sampleSound(SensorReading& r) {
    int now = soundPeakToPeak();
    r.sound_avg = soundP2pNSample ? (int)(soundP2pSumSample / (long)soundP2pNSample) : now;
    r.sound_peak = soundP2pMaxSample > now ? soundP2pMaxSample : now;
    soundP2pMaxSample = 0;
    soundP2pSumSample = 0;
    soundP2pNSample = 0;
}

void sampleLight(SensorReading& r) {
    if (lightBufCount > 0) {
        r.light = (int)(lightBufSum / (long)lightBufCount);
    } else {
        r.light = analogRead(PIN_PHOTO);
    }
}

void samplePMS(SensorReading& r) {
    r.pms_valid = pmsBufCount > 0;
    if (r.pms_valid) {
        r.pm1_0 = (uint16_t)(pm1Sum  / (long)pmsBufCount);
        r.pm2_5 = (uint16_t)(pm25Sum / (long)pmsBufCount);
        r.pm10  = (uint16_t)(pm10Sum / (long)pmsBufCount);
        r.pm1_0_cf1 = lastPmsFrame.pm1_0_cf1;
        r.pm2_5_cf1 = lastPmsFrame.pm2_5_cf1;
        r.pm10_cf1 = lastPmsFrame.pm10_cf1;
        r.pms_awake_s = (millis() - pmsOnSince) / 1000;
        r.cnt_0_3 = lastPmsFrame.cnt_0_3;
        r.cnt_0_5 = lastPmsFrame.cnt_0_5;
        r.cnt_1_0 = lastPmsFrame.cnt_1_0;
        r.cnt_2_5 = lastPmsFrame.cnt_2_5;
        r.cnt_5_0 = lastPmsFrame.cnt_5_0;
        r.cnt_10  = lastPmsFrame.cnt_10;
    }
}

// --- Serial output ---

void printReading(const SensorReading& r) {
    Console.println("--- Sample ---");

    if (r.bme_valid) {
        Console.println("-- Environment --");
        Console.printf("  Temp     = %.2f C      [%s]\n", r.temperature, tempLevel(r.temperature));
        Console.printf("  Humidity = %.2f %%     [%s]\n", r.humidity, humidityLevel(r.humidity));
        Console.printf("  Pressure = %.2f hPa   [%s]\n", r.pressure, pressureLevel(r.pressure));
    } else {
        Console.println("BME280: read failed");
    }

    Console.println("-- Sound (1 s peak-to-peak, over the interval) --");
    Console.printf("  Average=%4d [%s]  Peak=%4d [%s]\n", r.sound_avg, soundLevel(r.sound_avg),
        r.sound_peak, soundLevel(r.sound_peak));

    Console.println("-- Light --");
    Console.printf("  ADC = %4d  [%s]\n", r.light, lightLevel(r.light));

    if (r.pms_valid) {
        Console.println("-- Particulate Matter --");
        Console.printf("  PM1.0  = %3d ug/m3  [%s]\n", r.pm1_0, pm1Level(r.pm1_0));
        Console.printf("  PM2.5  = %3d ug/m3  [%s]\n", r.pm2_5, pm25Level(r.pm2_5));
        Console.printf("  PM10   = %3d ug/m3  [%s]\n", r.pm10, pm10Level(r.pm10));
    } else {
        Console.println("PMS: no data");
    }

    Console.printf("-- Status: WiFi=%s  uptime=%lus  heap=%u --\n",
        wifiConnected() ? "connected" : "disconnected",
        millis() / 1000, (unsigned)ESP.getFreeHeap());
}

// --- Status JSON (for the live config-portal panel) ---

// Escape a level label for safe insertion into a JSON string value. Today's
// labels contain only '-', spaces and commas, but escape '"'/'\' defensively.
static String jsonEsc(const char* s) {
    String out;
    for (const char* p = s; *p; ++p) {
        char c = *p;
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Indoor PM2.5 estimate from particle counts: ALT-CF3 (Wallace et al. 2022,
// Sensors 22:4741), validated indoors and outdoors at a home. N1-N3 are the
// per-0.1 L counts in the 0.3-0.5, 0.5-1 and 1-2.5 um bins (the sensor reports
// cumulative ">x" counts, hence the differences). The raw atmospheric PM2.5
// stays the logged trend value; this is only the health estimate shown.
float pm25AltCf3(uint16_t c03, uint16_t c05, uint16_t c10, uint16_t c25) {
    float n1 = c03 > c05 ? c03 - c05 : 0, n2 = c05 > c10 ? c05 - c10 : 0, n3 = c10 > c25 ? c10 - c25 : 0;
    return 3.0f * (0.00030418f * n1 + 0.0018512f * n2 + 0.02069706f * n3);
}

String statusJson() {
    const SensorReading& r = latestReading;
    bool haveSample = r.timestamp_ms != 0;
    unsigned long ageS = haveSample ? (millis() - r.timestamp_ms) / 1000 : 0;

    String j = "{";
    j += "\"ts\":" + String(r.timestamp_ms);
    j += ",\"age_s\":" + String(ageS);
    j += ",\"have_sample\":" + String(haveSample ? "true" : "false");

    j += ",\"bme\":{\"valid\":" + String(r.bme_valid ? "true" : "false");
    if (r.bme_valid) {
        j += ",\"temp\":" + String(r.temperature, 2);
        j += ",\"hum\":" + String(r.humidity, 2);
        j += ",\"pres\":" + String(r.pressure, 2);
        j += ",\"tempLvl\":\"" + jsonEsc(tempLevel(r.temperature)) + "\"";
        j += ",\"humLvl\":\"" + jsonEsc(humidityLevel(r.humidity)) + "\"";
        j += ",\"presLvl\":\"" + jsonEsc(pressureLevel(r.pressure)) + "\"";
    }
    j += "}";

    j += ",\"sound\":{\"avg\":" + String(r.sound_avg);
    j += ",\"peak\":" + String(r.sound_peak) + "}";

    j += ",\"light\":{\"adc\":" + String(r.light);
    j += ",\"lvl\":\"" + jsonEsc(lightLevel(r.light)) + "\"}";

    j += ",\"pms\":{\"valid\":" + String(r.pms_valid ? "true" : "false");
    if (r.pms_valid) {
        j += ",\"pm1\":" + String(r.pm1_0);
        j += ",\"pm25\":" + String(r.pm2_5);
        j += ",\"pm10\":" + String(r.pm10);
        j += ",\"cf1\":" + String(r.pm2_5_cf1);
        j += ",\"est\":" + String(pm25AltCf3(r.cnt_0_3, r.cnt_0_5, r.cnt_1_0, r.cnt_2_5), 1);
        j += ",\"pm1Lvl\":\"" + jsonEsc(pm1Level(r.pm1_0)) + "\"";
        j += ",\"pm25Lvl\":\"" + jsonEsc(pm25Level(r.pm2_5)) + "\"";
        j += ",\"pm10Lvl\":\"" + jsonEsc(pm10Level(r.pm10)) + "\"";
    }
    j += "}";

    j += ",\"sys\":{\"uptime_s\":" + String(millis() / 1000);
    j += ",\"reset\":\"" + jsonEsc(resetReasonStr()) + "\"";
    j += ",\"heap\":" + String(ESP.getFreeHeap());
    j += ",\"rssi\":" + String(WiFi.RSSI());
    j += ",\"live_debug\":" + String(params().liveDebug ? "true" : "false");
    j += ",\"sample_s\":" + String(params().sampleIntervalS);
    j += ",\"next_sample_s\":" + String((sampleIntervalMs() - min(millis() - lastSample, (unsigned long)sampleIntervalMs())) / 1000);
    j += ",\"log_kb\":" + String(dataLogSize() / 1024) + "}";

    // Hardware state for the portal's startup checklist.
    {
        char buf[200];
        snprintf(buf, sizeof(buf),
            ",\"hw\":{\"climate\":%s,\"climate_addr\":%u,\"dust_on\":%s,\"dust_s\":%lu,\"warm_s\":%lu,"
            "\"log_ok\":%s,\"log_free_kb\":%u}",
            bmeOk ? "true" : "false", (unsigned)bmeAddr, pmsOn ? "true" : "false",
            pmsOn ? (millis() - pmsOnSince) / 1000 : 0UL, (unsigned long)(PMS_WARMUP_MS / 1000),
            dataLogMounted() ? "true" : "false", (unsigned)(dataLogFreeBytes() / 1024));
        j += buf;
    }

    // Test-mode snapshot, present once the first 5s diagnostic has run.
    const LiveDiag& d = liveDiag;
    if (params().liveDebug && d.at_ms) {
        const PMS5003::Data& f = d.frame;
        char buf[512];
        snprintf(buf, sizeof(buf),
            ",\"live\":{\"age_s\":%lu,\"pms\":{\"ok\":%s,\"t_s\":%lu,\"atm\":[%u,%u,%u],\"avg\":[%u,%u,%u],\"n\":%u,"
            "\"est\":%.1f,\"cf1\":[%u,%u,%u],\"cnt\":[%u,%u,%u,%u,%u,%u],\"dt_ms\":%lu,\"frames\":%lu,\"bad\":%lu,\"bytes\":%lu},"
            "\"bme\":{\"found\":%s,\"ok\":%s,\"temp\":%.2f,\"hum\":%.1f,\"pres\":%.1f},"
            "\"sound\":{\"avg\":%d,\"peak\":%d},\"light\":%ld}",
            (millis() - d.at_ms) / 1000, d.pms_ok ? "true" : "false", d.pms_t_s,
            f.pm1_0, f.pm2_5, f.pm10, d.pm_avg[0], d.pm_avg[1], d.pm_avg[2], d.pm_avg_n,
            pm25AltCf3(f.cnt_0_3, f.cnt_0_5, f.cnt_1_0, f.cnt_2_5),
            f.pm1_0_cf1, f.pm2_5_cf1, f.pm10_cf1,
            f.cnt_0_3, f.cnt_0_5, f.cnt_1_0, f.cnt_2_5, f.cnt_5_0, f.cnt_10,
            (unsigned long)f.frameIntervalMs, (unsigned long)d.frames, (unsigned long)d.bad,
            (unsigned long)d.bytes,
            d.bme_found ? "true" : "false", d.bme_ok ? "true" : "false",
            d.bme_ok ? d.temp : 0.0f, d.bme_ok ? d.hum : 0.0f, d.bme_ok ? d.pres : 0.0f,
            d.sound_avg5, d.sound_max5s, d.light);
        j += buf;
    }

    // Boot self-test readings, until the first sample replaces them.
    if (!haveSample && bootReading.taken) {
        char buf[160];
        snprintf(buf, sizeof(buf),
            ",\"boot\":{\"bme_ok\":%s,\"temp\":%.2f,\"hum\":%.1f,\"pres\":%.1f,\"sound\":%d,\"light\":%d}",
            bootReading.bme_ok ? "true" : "false", bootReading.temp, bootReading.hum,
            bootReading.pres, bootReading.sound_p2p, bootReading.light);
        j += buf;
    }

    j += "}";
    return j;
}

// --- BME280 diagnostics ---

// Print the chip ID and the factory temperature calibration words. A BME280
// reports ID 0x60; dig_T1 is typically ~27000-29000 and dig_T2 ~25000-27000.
// Garbage here (0, 0xFFFF, wildly different on each boot) means the I2C link
// is corrupting reads, which would explain absurd temperatures.
// Read len bytes starting at reg. Uses a STOP between the register write and
// the read (no repeated start) to rule out bus-sequencing quirks. Returns the
// Wire error code (0 = ok) or 0xFF on a short read.
uint8_t bmeReadRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    uint8_t err = Wire.endTransmission();
    if (err) return err;
    if (Wire.requestFrom(addr, len) != len) return 0xFF;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return 0;
}

void dumpBmeCalibration(uint8_t addr) {
    uint8_t id = 0;
    uint8_t e1 = bmeReadRegs(addr, 0xD0, &id, 1);  // chip ID register

    uint8_t c[6] = {0};
    uint8_t e2 = bmeReadRegs(addr, 0x88, c, 6);    // dig_T1..dig_T3, little-endian
    uint16_t t1 = c[0] | (c[1] << 8);
    int16_t  t2 = (int16_t)(c[2] | (c[3] << 8));
    int16_t  t3 = (int16_t)(c[4] | (c[5] << 8));
    Console.printf("BME280 @0x%02X: chip_id=0x%02X%s dig_T1=%u dig_T2=%d dig_T3=%d (i2c err %u/%u)\n",
        addr, id, id == 0x60 ? " (BME280)" : id == 0x58 ? " (BMP280!)" : " (unexpected)",
        t1, t2, t3, e1, e2);
}

// Write one forced-mode conversion request and read back. Tells apart "writes
// never land" (ctrl_meas reads back 00) from "chip converts fine but later
// falls back to sleep/reset" (ctrl_meas ok, raw data moves off 0x800000).
void bmeForcedTest(uint8_t addr) {
    Wire.beginTransmission(addr);
    Wire.write(0xF2); Wire.write(0x01);        // ctrl_hum: humidity x1
    uint8_t e1 = Wire.endTransmission();
    Wire.beginTransmission(addr);
    Wire.write(0xF4); Wire.write(0x25);        // ctrl_meas: T x1, P x1, forced
    uint8_t e2 = Wire.endTransmission();
    uint8_t ctrlNow = 0;
    bmeReadRegs(addr, 0xF4, &ctrlNow, 1);
    delay(20);                                 // x1/x1/x1 conversion is <10 ms
    uint8_t r[8] = {0};
    uint8_t e3 = bmeReadRegs(addr, 0xF7, r, 8);
    Console.printf("BME forced test: write err %u/%u, ctrl_meas after write=%02X, "
                  "raw P=%02X%02X%02X T=%02X%02X%02X H=%02X%02X (read err %u)%s\n",
        e1, e2, ctrlNow, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], e3,
        (e1 || e2) ? "  <-- chip not responding at all (wiring/contact?)"
        : e3 ? "  <-- chip dropped off the bus mid-conversion (power/contact?)"
           : (r[3] == 0x80 && r[4] == 0 && r[5] == 0) ? "  <-- still reset value, no conversion"
           : "  <-- converted");
}

// One-line raw register view: status (F3), ctrl_meas (F4, low 2 bits = mode:
// 00 sleep, 11 normal), config (F5) and the raw P/T/H ADC bytes (F7-FE). If the
// raw bytes never change between prints, the sensor isn't converting.
void printBmeRaw(uint8_t addr) {
    uint8_t r[12] = {0};
    uint8_t err = bmeReadRegs(addr, 0xF3, r, 12);
    Console.printf("[BME raw] err=%u status=%02X ctrl_meas=%02X config=%02X "
                  "P=%02X%02X%02X T=%02X%02X%02X H=%02X%02X\n",
        err, r[0], r[1], r[2], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11]);
}

// --- Startup self-test ---

void runSelfTest() {
    Console.println("--- Device self-test ---");

    if (bmeOk && !bmeMeasure()) {
        Console.println("  BME280:         FOUND, but measurement failed");
    } else if (bmeOk) {
        bootReading.bme_ok = true;
        bootReading.temp = bme.readTemperature();
        bootReading.hum = bme.readHumidity();
        bootReading.pres = bme.readPressure() / 100.0F;
        Console.printf("  BME280:         OK (%.1f C, %.0f%%, %.0f hPa)\n",
            bootReading.temp, bootReading.hum, bootReading.pres);
    } else {
        Console.println("  BME280:         NOT FOUND");
    }

    // Wait for PMS bytes; meanwhile measure sound peak-to-peak and average
    // light the same way the sampler does, for the portal's boot readings.
    pms.reset();
    pms.powerOn();
    unsigned long t0 = millis();
    int sMin = 4095, sMax = 0;
    long lSum = 0, lN = 0;
    while (millis() - t0 < 1500) {
        pms.loop();
        int s = analogRead(PIN_KY037_A);
        if (s < sMin) sMin = s;
        if (s > sMax) sMax = s;
        lSum += analogRead(PIN_PHOTO);
        lN++;
        delay(1);
    }
    bootReading.sound_p2p = sMax - sMin;
    bootReading.light = lN ? lSum / lN : 0;
    bootReading.taken = true;
    Console.printf("  KY-037 sound:   p2p %d\n", bootReading.sound_p2p);
    Console.printf("  Photoresistor:  %d\n", bootReading.light);
    Console.printf("  PMS5003:        %s\n",
        pms.getDebugStats().bytesReceived > 0 ? "OK" : "NO DATA");

    Console.println("------------------------");
}

// --- Device actions for console commands (see device.h) ---

static const char* pmsStateStr(unsigned long now) {
    static char buf[48];
    if (!pmsOn) return "asleep";
    unsigned long awake = (now - pmsOnSince) / 1000;
    if (now - pmsOnSince < PMS_WARMUP_MS) snprintf(buf, sizeof(buf), "warming up (%lu s)", awake);
    else snprintf(buf, sizeof(buf), "awake %lu s", awake);
    return buf;
}

void devI2cScan() {
    Console.print("I2C scan:");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Console.printf(" 0x%02X", a);
            found++;
        }
    }
    Console.println(found ? "" : " (nothing on bus)");
}

void devPrintStatus() {
    unsigned long now = millis();
    unsigned long since = now - lastSample;
    unsigned long next = since >= sampleIntervalMs() ? 0 : (sampleIntervalMs() - since) / 1000;
    Console.printf("uptime %lu s, reset: %s, heap %u KB\n",
        now / 1000, resetReasonStr(), (unsigned)(ESP.getFreeHeap() / 1024));
    Console.printf("wifi: %s, ip %s, rssi %d dBm\n",
        wifiConnected() ? "connected" : "disconnected",
        WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    Console.printf("mode: %s, sample every %u s, next in %lu s%s\n",
        params().liveDebug ? "test" : "normal", params().sampleIntervalS, next,
        sampleRequested ? " (sample requested)" : "");
    Console.printf("climate: %s, dust: %s\n", bmeOk ? "ok" : "not found", pmsStateStr(now));
    Console.printf("log: %u KB, %u KB free\n",
        (unsigned)(dataLogSize() / 1024), (unsigned)(dataLogFreeBytes() / 1024));
}

void devRequestSample() {
    unsigned long now = millis();
    sampleRequested = true;
    if (pmsOn && now - pmsOnSince >= PMS_WARMUP_MS) {
        Console.println("Sample: PMS is warm, sampling now");
    } else {
        unsigned long left = pmsOn ? (PMS_WARMUP_MS - (now - pmsOnSince)) / 1000 : PMS_WARMUP_MS / 1000;
        Console.printf("Sample: %s PMS, sampling in ~%lu s\n", pmsOn ? "waiting for" : "waking", left);
    }
}

bool devRead(const String& what) {
    bool all = what == "" || what == "all";
    if (!all && what != "climate" && what != "dust" && what != "sound" && what != "light") return false;
    unsigned long now = millis();
    if (all || what == "climate") {
        if (bmeOk && bmeMeasure()) {
            float t = bme.readTemperature(), h = bme.readHumidity(), p = bme.readPressure() / 100.0F;
            Console.printf("climate: %.2f C (%s), %.1f %% RH (%s), %.1f hPa\n",
                t, tempLevel(t), h, humidityLevel(h), p);
        } else {
            Console.printf("climate: %s\n", bmeOk ? "measurement failed (try diag climate)" : "sensor not found");
        }
    }
    if (all || what == "dust") {
        const PMS5003::Data& f = lastPmsFrame;
        if (pmsOn && lastPmsFrameMs && now - lastPmsFrameMs < 10000) {
            Console.printf("dust: PM1.0 %u, PM2.5 %u (%s), PM10 %u ug/m3 [%s]\n",
                f.pm1_0, f.pm2_5, pm25Level(f.pm2_5), f.pm10, pmsStateStr(now));
            Console.printf("      particles/0.1L >0.3 %u, >0.5 %u, >1 %u, >2.5 %u, >5 %u, >10 %u\n",
                f.cnt_0_3, f.cnt_0_5, f.cnt_1_0, f.cnt_2_5, f.cnt_5_0, f.cnt_10);
        } else if (pmsOn) {
            Console.printf("dust: no recent frames [%s]\n", pmsStateStr(now));
        } else {
            unsigned long since = now - lastSample;
            unsigned long wake = since + PMS_WARMUP_MS >= sampleIntervalMs() ? 0
                : (sampleIntervalMs() - PMS_WARMUP_MS - since) / 1000;
            Console.printf("dust: sensor asleep (wakes in %lu s; 'sample' wakes it now)\n", wake);
        }
    }
    if (all || what == "sound") {
        Console.printf("sound: now %d, since last sample avg %d / loudest %d (1 s peak-to-peak; quiet ~10, clap 500+)\n",
            soundPeakToPeak(), soundP2pNSample ? (int)(soundP2pSumSample / (long)soundP2pNSample) : 0,
            soundP2pMaxSample);
    }
    if (all || what == "light") {
        long l = lightBufCount ? lightBufSum / (long)lightBufCount : analogRead(PIN_PHOTO);
        Console.printf("light: %ld / 4095 (%s)\n", l, lightLevel(l));
    }
    return true;
}

void devDiagClimate() {
    dumpBmeCalibration(0x76);
    bmeForcedTest(0x76);
    bmeOk = bmeInit();
    Console.printf("climate sensor %s\n", bmeOk ? "re-initialized (forced mode, x1)" : "not found");
}

void devSetTestMode(bool on) {
    Params p = params();
    p.liveDebug = on;
    paramsSave(p);
    Console.printf("test mode %s%s\n", on ? "on" : "off",
        on ? (pmsOn ? "" : " (waking PMS)") : (pmsOn ? " (PMS sleeps after the next sample)" : ""));
}

void devSetSampleInterval(uint32_t seconds) {
    Params p = params();
    p.sampleIntervalS = seconds;
    paramsSave(p);
    Console.printf("sample interval %u s\n", params().sampleIntervalS);
}

// --- Setup & Loop ---

void setup() {
    Serial.begin(115200);
    delay(100);
    Console.println("Ambient Monitor - ESP32 firmware starting");
    Console.printf("Reset reason: %s  (build %s %s)\n", resetReasonStr(), __DATE__, __TIME__);

    paramsLoad();
    dataLogSetup();

    Wire.begin(PIN_BME_SDA, PIN_BME_SCL);
    Wire.setTimeOut(50);  // don't block boot if a line is stuck low (shorted bus)

    devI2cScan();

    // Raw diagnostics first; bmeInit() then soft-resets and configures the chip.
    dumpBmeCalibration(0x76);
    bmeForcedTest(0x76);
    bmeOk = bmeInit();
    if (!bmeOk) {
        Console.println("WARNING: BME280 not found at 0x76 (will retry while sampling)");
    } else {
        Console.printf("BME280 initialized at 0x%02X (forced mode, x1)\n", bmeAddr);
    }

    pinMode(PIN_KY037_A, INPUT);
    pinMode(PIN_PHOTO, INPUT);

    lastSoundTick = millis();
    lastLightTick = millis();

    pms.begin(PIN_PMS_RX, PIN_PMS_TX, PIN_PMS_SET, 9600);

    runSelfTest();

    pms.powerOff();
    lastSample = millis() - (sampleIntervalMs() > PMS_WARMUP_MS ? sampleIntervalMs() - PMS_WARMUP_MS : 0);

    wifiSetup();
    configPortalSetup();
    commandsSetup();
}

void loop() {
    unsigned long now = millis();
    unsigned long timeSinceLastSample = now - lastSample;

    // Sound sampling into rolling 1s buffer
    if (now - lastSoundTick >= SOUND_SAMPLE_INTERVAL_MS) {
        lastSoundTick = now;
        int v = analogRead(PIN_KY037_A);
        if (soundBufCount < SOUND_BUF_SIZE) {
            soundBuf[soundBufPos] = v;
            soundBufSum += v;
            soundBufPos = (soundBufPos + 1) % SOUND_BUF_SIZE;
            soundBufCount++;
        } else {
            soundBufSum -= soundBuf[soundBufPos];
            soundBuf[soundBufPos] = v;
            soundBufSum += v;
            soundBufPos = (soundBufPos + 1) % SOUND_BUF_SIZE;
        }

        // Peak-hold: p2p of each consecutive 1s block, keeping the loudest
        // block since the last sample / debug print so short noises between
        // reports aren't missed.
        if (v < soundBlkMin) soundBlkMin = v;
        if (v > soundBlkMax) soundBlkMax = v;
        if (++soundBlkN >= SOUND_BUF_SIZE) {
            int p2p = soundBlkMax - soundBlkMin;
            if (p2p > soundP2pMaxSample) soundP2pMaxSample = p2p;
            if (p2p > soundP2pMaxDebug)  soundP2pMaxDebug = p2p;
            soundP2pSumSample += p2p; soundP2pNSample++;
            soundP2pSumDebug += p2p;  soundP2pNDebug++;
            soundBlkMin = 4095;
            soundBlkMax = 0;
            soundBlkN = 0;
        }
    }

    // Light sampling into rolling buffer
    if (now - lastLightTick >= LIGHT_SAMPLE_INTERVAL_MS) {
        lastLightTick = now;
        int v = analogRead(PIN_PHOTO);
        if (lightBufCount < LIGHT_BUF_SIZE) {
            lightBuf[lightBufPos] = v;
            lightBufSum += v;
            lightBufPos = (lightBufPos + 1) % LIGHT_BUF_SIZE;
            lightBufCount++;
        } else {
            lightBufSum -= lightBuf[lightBufPos];
            lightBuf[lightBufPos] = v;
            lightBufSum += v;
            lightBufPos = (lightBufPos + 1) % LIGHT_BUF_SIZE;
        }
    }

    wifiLoop();
    configPortalLoop();
    consoleLoop();
    commandsLoop();

    // Power on the PMS PMS_WARMUP_MS before the next sample, or now if test
    // mode or a requested sample needs it.
    if (!pmsOn && (timeSinceLastSample + PMS_WARMUP_MS >= sampleIntervalMs()
                   || params().liveDebug || sampleRequested)) {
        pms.powerOn();
        resetPmsBuffer();
        pmsOn = true;
        pmsOnSince = now;
    }

    // Drain decoded frames into the rolling buffer while the sensor is powered
    if (pmsOn && pms.available()) {
        PMS5003::Data d;
        if (pms.read(&d)) {
            pushPmsFrame(d.pm1_0, d.pm2_5, d.pm10);
            lastPmsFrame = d;
            lastPmsFrameMs = now;
        }
    }

    // Test mode: every 5s print live diagnostics, one PMS line and one
    // environment line. t+ is seconds since the PMS woke, so settling time can
    // be read directly.
    static unsigned long lastPmsDebug = 0;
    if (params().liveDebug && pmsOn && now - lastPmsDebug >= 5000) {
        lastPmsDebug = now;
        PMS5003::DebugStats s = pms.getDebugStats();
        LiveDiag& d = liveDiag;
        d.at_ms = now;
        d.pms_t_s = (now - pmsOnSince) / 1000;
        d.pms_ok = s.framesDecoded > 0 || lastPmsFrame.frameIntervalMs > 0;
        d.frame = lastPmsFrame;
        d.frames = s.framesDecoded;
        d.bad = s.checksumFailures;
        d.bytes = s.bytesReceived;
        // Average and loudest 1 s block since the previous snapshot (~5 s).
        d.sound_avg5 = soundP2pNDebug ? (int)(soundP2pSumDebug / (long)soundP2pNDebug) : soundPeakToPeak();
        d.sound_max5s = soundP2pMaxDebug > d.sound_avg5 ? soundP2pMaxDebug : d.sound_avg5;
        soundP2pMaxDebug = 0;
        soundP2pSumDebug = 0;
        soundP2pNDebug = 0;
        d.light = lightBufCount ? lightBufSum / (long)lightBufCount : 0;
        // PM averaged over the rolling frame buffer, as samplePMS() does.
        d.pm_avg_n = pmsBufCount;
        d.pm_avg[0] = pmsBufCount ? pm1Sum / (long)pmsBufCount : 0;
        d.pm_avg[1] = pmsBufCount ? pm25Sum / (long)pmsBufCount : 0;
        d.pm_avg[2] = pmsBufCount ? pm10Sum / (long)pmsBufCount : 0;
        d.bme_found = bmeOk;
        d.bme_ok = bmeOk && bmeMeasure();
        if (d.bme_ok) {
            d.temp = bme.readTemperature();
            d.hum = bme.readHumidity();
            d.pres = bme.readPressure() / 100.0F;
        }

        const PMS5003::Data& f = d.frame;
        if (d.pms_ok) {
            Console.printf("[PMS t+%lus] atm=%u/%u/%u cf1=%u/%u/%u | n>0.3=%u >0.5=%u >1=%u >2.5=%u >5=%u >10=%u"
                          " | dt=%lums frames=%lu bad=%lu\n",
                d.pms_t_s,
                f.pm1_0, f.pm2_5, f.pm10, f.pm1_0_cf1, f.pm2_5_cf1, f.pm10_cf1,
                f.cnt_0_3, f.cnt_0_5, f.cnt_1_0, f.cnt_2_5, f.cnt_5_0, f.cnt_10,
                (unsigned long)f.frameIntervalMs, (unsigned long)d.frames, (unsigned long)d.bad);
        } else {
            Console.printf("[PMS t+%lus] no frames (bytes=%lu)\n", d.pms_t_s, (unsigned long)d.bytes);
        }

        char other[64];
        snprintf(other, sizeof(other), "sound avg=%d peak=%d | light=%ld",
            d.sound_avg5, d.sound_max5s, d.light);
        if (d.bme_ok) {
            Console.printf("[ENV] T=%.2fC H=%.1f%% P=%.1fhPa | %s\n", d.temp, d.hum, d.pres, other);
        } else {
            // Raw registers only on failure: shows whether the chip is asleep,
            // stuck at reset values, or gone from the bus.
            if (bmeOk) printBmeRaw(bmeAddr);
            Console.printf("[ENV] BME %s | %s\n", bmeOk ? "measurement failed" : "not found", other);
        }
    }

    // Sample when the interval is reached, or when requested and the PMS has
    // warmed up. Either way the interval restarts from this sample.
    bool pmsWarm = pmsOn && now - pmsOnSince >= PMS_WARMUP_MS;
    if (timeSinceLastSample >= sampleIntervalMs() || (sampleRequested && pmsWarm)) {
        if (sampleRequested) Console.println("Sample: taking requested sample");
        sampleRequested = false;
        latestReading = {};
        latestReading.timestamp_ms = now;

        sampleBME280(latestReading);
        sampleSound(latestReading);
        sampleLight(latestReading);
        samplePMS(latestReading);
        printReading(latestReading);
        dataLogAppend(latestReading);

        // Duty-cycle the PMS between samples, except in test mode.
        if (!params().liveDebug) {
            pms.powerOff();
            pmsOn = false;
        }

        lastSample = now;
    }

    pms.loop();
    yield();
}
