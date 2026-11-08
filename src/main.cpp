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

Adafruit_BME280 bme;
PMS5003 pms;

unsigned long lastSample = 0;
bool pmsOn = false;
unsigned long pmsOnSince = 0;     // millis() when the PMS was last woken
PMS5003::Data lastPmsFrame = {};  // most recent full frame, for live debug
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
    int sound_p2p, sound_max5s;
    long light;
};
LiveDiag liveDiag;

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

const char* soundLevel(int v) {
    if (v < 100)  return "Silent";
    if (v < 500)  return "Quiet";
    if (v < 1500) return "Moderate";
    if (v < 2500) return "Loud";
    if (v < 3500) return "Very Loud";
    return "Extremely Loud";
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

void sampleSound(SensorReading& r) {
    r.sound_avg = 0;
    r.sound_peak = 0;
    if (soundBufCount > 0) {
        r.sound_avg = (int)(soundBufSum / (long)soundBufCount);
        for (unsigned i = 0; i < soundBufCount; ++i) {
            if (soundBuf[i] > r.sound_peak) r.sound_peak = soundBuf[i];
        }
    }
    r.sound_p2p = soundPeakToPeak();
    r.sound_p2p_max = soundP2pMaxSample > r.sound_p2p ? soundP2pMaxSample : r.sound_p2p;
    soundP2pMaxSample = 0;
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

    Console.println("-- Sound (1s rolling) --");
    Console.printf("  Average=%4d  Peak=%4d  [%s]\n", r.sound_avg, r.sound_peak, soundLevel(r.sound_avg));

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
    j += ",\"peak\":" + String(r.sound_peak);
    j += ",\"p2p\":" + String(r.sound_p2p);
    j += ",\"p2p_max\":" + String(r.sound_p2p_max);
    j += ",\"lvl\":\"" + jsonEsc(soundLevel(r.sound_avg)) + "\"}";

    j += ",\"light\":{\"adc\":" + String(r.light);
    j += ",\"lvl\":\"" + jsonEsc(lightLevel(r.light)) + "\"}";

    j += ",\"pms\":{\"valid\":" + String(r.pms_valid ? "true" : "false");
    if (r.pms_valid) {
        j += ",\"pm1\":" + String(r.pm1_0);
        j += ",\"pm25\":" + String(r.pm2_5);
        j += ",\"pm10\":" + String(r.pm10);
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

    // Test-mode snapshot, present once the first 5s diagnostic has run.
    const LiveDiag& d = liveDiag;
    if (params().liveDebug && d.at_ms) {
        const PMS5003::Data& f = d.frame;
        char buf[512];
        snprintf(buf, sizeof(buf),
            ",\"live\":{\"age_s\":%lu,\"pms\":{\"ok\":%s,\"t_s\":%lu,\"atm\":[%u,%u,%u],\"cf1\":[%u,%u,%u],"
            "\"cnt\":[%u,%u,%u,%u,%u,%u],\"dt_ms\":%lu,\"frames\":%lu,\"bad\":%lu,\"bytes\":%lu},"
            "\"bme\":{\"found\":%s,\"ok\":%s,\"temp\":%.2f,\"hum\":%.1f,\"pres\":%.1f},"
            "\"sound\":{\"p2p\":%d,\"max5s\":%d},\"light\":%ld}",
            (millis() - d.at_ms) / 1000, d.pms_ok ? "true" : "false", d.pms_t_s,
            f.pm1_0, f.pm2_5, f.pm10, f.pm1_0_cf1, f.pm2_5_cf1, f.pm10_cf1,
            f.cnt_0_3, f.cnt_0_5, f.cnt_1_0, f.cnt_2_5, f.cnt_5_0, f.cnt_10,
            (unsigned long)f.frameIntervalMs, (unsigned long)d.frames, (unsigned long)d.bad,
            (unsigned long)d.bytes,
            d.bme_found ? "true" : "false", d.bme_ok ? "true" : "false",
            d.bme_ok ? d.temp : 0.0f, d.bme_ok ? d.hum : 0.0f, d.bme_ok ? d.pres : 0.0f,
            d.sound_p2p, d.sound_max5s, d.light);
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
        Console.printf("  BME280:         OK (%.1f C, %.0f%%, %.0f hPa)\n",
            bme.readTemperature(), bme.readHumidity(), bme.readPressure() / 100.0F);
    } else {
        Console.println("  BME280:         NOT FOUND");
    }
    Console.printf("  KY-037 sound:   %d\n", analogRead(PIN_KY037_A));
    Console.printf("  Photoresistor:  %d\n", analogRead(PIN_PHOTO));

    pms.reset();
    pms.powerOn();
    unsigned long t0 = millis();
    while (millis() - t0 < 1500) {
        pms.loop();
        delay(5);
    }
    Console.printf("  PMS5003:        %s\n",
        pms.getDebugStats().bytesReceived > 0 ? "OK" : "NO DATA");

    Console.println("------------------------");
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

    // TEMP I2C scan — remove once BME is found. Prints every responding address.
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

    // Power on PMS PMS_WARMUP_MS before the next sample
    if (!pmsOn && timeSinceLastSample + PMS_WARMUP_MS >= sampleIntervalMs()) {
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
        // p2p is the last 1s; max5s is the loudest 1s block since the previous print.
        d.sound_p2p = soundPeakToPeak();
        d.sound_max5s = soundP2pMaxDebug;
        soundP2pMaxDebug = 0;
        d.light = lightBufCount ? lightBufSum / (long)lightBufCount : 0;
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
        snprintf(other, sizeof(other), "sound p2p=%d max5s=%d | light=%ld",
            d.sound_p2p, d.sound_max5s, d.light);
        if (d.bme_ok) {
            Console.printf("[ENV] T=%.2fC H=%.1f%% P=%.1fhPa | %s\n", d.temp, d.hum, d.pres, other);
        } else {
            // Raw registers only on failure: shows whether the chip is asleep,
            // stuck at reset values, or gone from the bus.
            if (bmeOk) printBmeRaw(bmeAddr);
            Console.printf("[ENV] BME %s | %s\n", bmeOk ? "measurement failed" : "not found", other);
        }
    }

    // Sample when interval is reached
    if (timeSinceLastSample >= sampleIntervalMs()) {
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
