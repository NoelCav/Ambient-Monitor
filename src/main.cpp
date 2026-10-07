#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "PMS5003.h"
#include "config.h"
#include "params.h"
#include "sensor_reading.h"
#include "wifi_manager.h"
#include "firestore_upload.h"
#include "config_portal.h"

Adafruit_BME280 bme;
PMS5003 pms;

unsigned long lastSample = 0;
bool pmsOn = false;
bool bmeOk = false;
unsigned samplesSinceFlush = 0;
bool flushing = false;
SensorReading latestReading = {};

// Rolling 1-second sound buffer (samples every 1ms)
const unsigned SOUND_BUF_SIZE = 1000;
const unsigned SOUND_SAMPLE_INTERVAL_MS = 1;
int soundBuf[SOUND_BUF_SIZE];
unsigned soundBufPos = 0;
unsigned soundBufCount = 0;
unsigned long lastSoundTick = 0;
long soundBufSum = 0;

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

void sampleBME280(SensorReading& r) {
    if (!bmeOk) {
        bmeOk = bme.begin(0x76) || bme.begin(0x77);
        if (bmeOk) delay(50);  // let the first conversion complete before reading
    }

    r.temperature = bme.readTemperature();
    r.humidity = bme.readHumidity();
    r.pressure = bme.readPressure() / 100.0F;
    r.bme_valid = !isnan(r.temperature) && !isnan(r.humidity) && !isnan(r.pressure);
    if (!r.bme_valid) bmeOk = false;
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
    }
}

// --- Serial output ---

void printReading(const SensorReading& r) {
    Serial.println("--- Sample ---");

    if (r.bme_valid) {
        Serial.println("-- Environment --");
        Serial.printf("  Temp     = %.2f C      [%s]\n", r.temperature, tempLevel(r.temperature));
        Serial.printf("  Humidity = %.2f %%     [%s]\n", r.humidity, humidityLevel(r.humidity));
        Serial.printf("  Pressure = %.2f hPa   [%s]\n", r.pressure, pressureLevel(r.pressure));
    } else {
        Serial.println("BME280: read failed");
    }

    Serial.println("-- Sound (1s rolling) --");
    Serial.printf("  Average=%4d  Peak=%4d  [%s]\n", r.sound_avg, r.sound_peak, soundLevel(r.sound_avg));

    Serial.println("-- Light --");
    Serial.printf("  ADC = %4d  [%s]\n", r.light, lightLevel(r.light));

    if (r.pms_valid) {
        Serial.println("-- Particulate Matter --");
        Serial.printf("  PM1.0  = %3d ug/m3  [%s]\n", r.pm1_0, pm1Level(r.pm1_0));
        Serial.printf("  PM2.5  = %3d ug/m3  [%s]\n", r.pm2_5, pm25Level(r.pm2_5));
        Serial.printf("  PM10   = %3d ug/m3  [%s]\n", r.pm10, pm10Level(r.pm10));
    } else {
        Serial.println("PMS: no data");
    }

    Serial.printf("-- Status: WiFi=%s  Queue=%u --\n",
        wifiConnected() ? "connected" : "disconnected",
        firestoreQueueCount());
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

    j += "}";
    return j;
}

// --- Startup self-test ---

void runSelfTest() {
    Serial.println("--- Device self-test ---");

    if (bmeOk) {
        Serial.printf("  BME280 (0x76):  OK (%.1f C, %.0f%%, %.0f hPa)\n",
            bme.readTemperature(), bme.readHumidity(), bme.readPressure() / 100.0F);
    } else {
        Serial.println("  BME280 (0x76):  NOT FOUND");
    }
    Serial.printf("  KY-037 sound:   %d\n", analogRead(PIN_KY037_A));
    Serial.printf("  Photoresistor:  %d\n", analogRead(PIN_PHOTO));

    pms.reset();
    pms.powerOn();
    unsigned long t0 = millis();
    while (millis() - t0 < 1500) {
        pms.loop();
        delay(5);
    }
    Serial.printf("  PMS5003:        %s\n",
        pms.getDebugStats().bytesReceived > 0 ? "OK" : "NO DATA");

    Serial.println("------------------------");
}

// --- Setup & Loop ---

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("Ambient Monitor - ESP32 firmware starting");

    paramsLoad();

    Wire.begin(PIN_BME_SDA, PIN_BME_SCL);
    Wire.setTimeOut(50);  // don't block boot if a line is stuck low (shorted bus)

    // TEMP I2C scan — remove once BME is found. Prints every responding address.
    Serial.print("I2C scan:");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf(" 0x%02X", a);
            found++;
        }
    }
    Serial.println(found ? "" : " (nothing on bus)");

    bmeOk = bme.begin(0x76) || bme.begin(0x77);
    if (!bmeOk) {
        Serial.println("WARNING: BME280 not found at 0x76 (will retry while sampling)");
    } else {
        Serial.println("BME280 initialized");
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
    firestoreSetup();
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

    // Power on PMS PMS_WARMUP_MS before the next sample
    if (!pmsOn && timeSinceLastSample + PMS_WARMUP_MS >= sampleIntervalMs()) {
        pms.powerOn();
        resetPmsBuffer();
        pmsOn = true;
    }

    // Drain decoded frames into the rolling buffer while the sensor is powered
    if (pmsOn && pms.available()) {
        PMS5003::Data d;
        if (pms.read(&d)) pushPmsFrame(d.pm1_0, d.pm2_5, d.pm10);
    }

#if PMS_DEBUG_LIVE
    // Every 5s print the average of the last 5 frames for live smoke/dust testing.
    static unsigned long lastPmsDebug = 0;
    if (pmsOn && now - lastPmsDebug >= 5000) {
        lastPmsDebug = now;
        unsigned k = pmsBufCount < 5 ? pmsBufCount : 5;
        if (k > 0) {
            long a = 0, b = 0, c = 0;
            for (unsigned i = 0; i < k; i++) {
                unsigned idx = (pmsBufPos + PMS_BUF_SIZE - 1 - i) % PMS_BUF_SIZE;
                a += pm1Buf[idx];
                b += pm25Buf[idx];
                c += pm10Buf[idx];
            }
            Serial.printf("[PMS debug] last %u frames: PM1.0=%ld PM2.5=%ld PM10=%ld\n",
                k, a / (long)k, b / (long)k, c / (long)k);
        } else {
            Serial.println("[PMS debug] no frames yet");
        }
    }
#endif

    // Sample when interval is reached
    if (timeSinceLastSample >= sampleIntervalMs()) {
        latestReading = {};
        latestReading.timestamp_ms = now;

        sampleBME280(latestReading);
        sampleSound(latestReading);
        sampleLight(latestReading);
        samplePMS(latestReading);
        printReading(latestReading);

#if !PMS_DEBUG_LIVE
        pms.powerOff();
        pmsOn = false;
#endif

        if (params().uploadEnabled) {
            queueReading(latestReading);
            if (++samplesSinceFlush >= params().batchSize) {
                flushing = true;
                samplesSinceFlush = 0;
            }
        }

        lastSample = now;
    }

    // Drain the queue non-blockingly while a flush is open; retries failed uploads.
    if (flushing) {
        firestoreLoop();
        if (firestoreQueueCount() == 0) flushing = false;
    }

    pms.loop();
    yield();
}
