#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "PMS5003.h"
#include "config.h"

Adafruit_BME280 bme; // I2C
PMS5003 pms; // uses HardwareSerial

unsigned long lastSample = 0;
bool pmsOn = false;
bool pmsAlwaysOn = false;

// Rolling 1-second sound buffer (samples every 10ms)
const unsigned SOUND_WINDOW_MS = 1000;
const unsigned SOUND_SAMPLE_INTERVAL_MS = 1;
const unsigned SOUND_BUF_SIZE = SOUND_WINDOW_MS / SOUND_SAMPLE_INTERVAL_MS;
int soundBuf[SOUND_BUF_SIZE];
unsigned soundBufPos = 0;
unsigned soundBufCount = 0;
unsigned long lastSoundTick = 0;
long soundBufSum = 0;

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
    // ESP32 ADC is 12-bit (0-4095)
    if (v < 100)  return "Silent";
    if (v < 500)  return "Quiet";
    if (v < 1500) return "Moderate";
    if (v < 2500) return "Loud";
    if (v < 3500) return "Very Loud";
    return "Extremely Loud";
}

const char* lightLevel(int v) {
    // High ADC = bright
    if (v > 3800) return "Very Bright - direct sunlight";
    if (v > 3000) return "Bright - indirect sunlight";
    if (v > 2000) return "Bright indoor / overcast outdoor";
    if (v > 1000) return "Indoor lighting";
    if (v > 400)  return "Dim - low light";
    return "Dark - night/blackout";
}

void sampleBME280()
{
    float t = bme.readTemperature();
    if (t != NAN) {
        float p = bme.readPressure() / 100.0F;
        float h = bme.readHumidity();
        Serial.println("-- Environment --");
        Serial.printf("  Temp     = %.2f C      [%s]\n", t, tempLevel(t));
        Serial.printf("  Humidity = %.2f %%     [%s]\n", h, humidityLevel(h));
        Serial.printf("  Pressure = %.2f hPa   [%s]\n", p, pressureLevel(p));
    } else {
        Serial.println("BME280: read failed");
    }
}

void sampleSound()
{
    // Compute average and peak over rolling 1s buffer
    int avg = 0;
    int peak = 0;
    if (soundBufCount > 0) {
        avg = (int)(soundBufSum / (long)soundBufCount);
        for (unsigned i = 0; i < soundBufCount; ++i) {
            int v = soundBuf[i];
            if (v > peak) peak = v;
        }
    }

    Serial.println("-- Sound (1s rolling) --");
    Serial.printf("  Average=%4d  Peak=%4d  [%s]\n", avg, peak, soundLevel(avg));
}

void sampleLight()
{
    int light = analogRead(PIN_PHOTO);
    Serial.println("-- Light --");
    Serial.printf("  ADC = %4d  [%s]\n", light, lightLevel(light));
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
    // No official EPA breakpoints for PM1.0, use conservative thresholds
    if (v <= 10)   return "Good";
    if (v <= 25)   return "Moderate";
    if (v <= 45)   return "Unhealthy for Sensitive Groups";
    if (v <= 100)  return "Unhealthy";
    if (v <= 200)  return "Very Unhealthy";
    return "Hazardous";
}

void samplePMS()
{
    PMS5003::DebugStats stats = pms.getDebugStats();
    if (pms.available()) {
        PMS5003::Data d;
        if (pms.read(&d)) {
            Serial.println("-- Particulate Matter --");
            Serial.printf("  PM1.0  = %3d ug/m3  [%s]\n",  d.pm1_0, pm1Level(d.pm1_0));
            Serial.printf("  PM2.5  = %3d ug/m3  [%s]\n",  d.pm2_5, pm25Level(d.pm2_5));
            Serial.printf("  PM10   = %3d ug/m3  [%s]\n",  d.pm10,  pm10Level(d.pm10));
            Serial.printf("  (rx=%u frames=%u chk=%u)\n",
                stats.bytesReceived, stats.framesDecoded, stats.checksumFailures);
        } else {
            Serial.println("PMS: parse failed");
        }
    } else {
        Serial.printf("PMS: no data available  (rx=%u buf=%u frames=%u chk=%u skip=%u hdr=%u)",
            stats.bytesReceived,
            stats.bufferBytes,
            stats.framesDecoded,
            stats.checksumFailures,
            stats.headerSkips,
            stats.headerMatches);
        if (stats.firstBytesLen > 0) {
            Serial.print(" firstBytes=");
            for (uint8_t i = 0; i < stats.firstBytesLen; i++) {
                Serial.printf("%02X", stats.firstBytes[i]);
                if (i + 1 < stats.firstBytesLen) Serial.print(" ");
            }
        }
        Serial.println();
    }
}
void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("Ambient Monitor - ESP32 firmware starting");

    // Initialize I2C with chosen pins
    Wire.begin(PIN_BME_SDA, PIN_BME_SCL);
    bool bme_ok = bme.begin(0x76);
    if (!bme_ok) {
        Serial.println("WARNING: BME280 not found at 0x76");
    } else {
        Serial.println("BME280 initialized");
    }

    // Setup analog pins
    pinMode(PIN_KY037_A, INPUT);
    pinMode(PIN_PHOTO, INPUT);

    // Initialize rolling sound buffer with current readings
    soundBufSum = 0;
    for (unsigned i = 0; i < SOUND_BUF_SIZE; ++i) {
        int v = analogRead(PIN_KY037_A);
        soundBuf[i] = v;
        soundBufSum += v;
    }
    soundBufCount = SOUND_BUF_SIZE;
    soundBufPos = 0;
    lastSoundTick = millis();

    // PMS5003 setup: initialize with SET pin for power control
    pms.begin(PIN_PMS_RX, PIN_PMS_TX, PIN_PMS_SET, 9600);

    // Decide behavior based on interval vs warmup: if interval <= warmup,
    // keep PMS always on to avoid power-cycling for short intervals.
    if (SAMPLE_INTERVAL_MS <= PMS_WARMUP_MS) {
        pmsAlwaysOn = true;
        pms.powerOn();
        pmsOn = true;
        Serial.println("PMS5003: configured always-on (interval <= warmup)");
        lastSample = millis();
    } else {
        pmsAlwaysOn = false;
        unsigned long offset = SAMPLE_INTERVAL_MS - PMS_WARMUP_MS;
        pms.powerOff();
        lastSample = millis() - offset;
        Serial.printf("PMS5003: scheduled first sample in %lu ms\n", (unsigned long)PMS_WARMUP_MS);
    }
}

void loop()
{
    unsigned long now = millis();
    unsigned long timeSinceLastSample = now - lastSample;
    // Non-blocking sound sampling into rolling 1s buffer
    if (now - lastSoundTick >= SOUND_SAMPLE_INTERVAL_MS) {
        lastSoundTick = now;
        int v = analogRead(PIN_KY037_A);
        if (soundBufCount < SOUND_BUF_SIZE) {
            // buffer not yet full
            soundBuf[soundBufPos] = v;
            soundBufSum += v;
            soundBufPos = (soundBufPos + 1) % SOUND_BUF_SIZE;
            soundBufCount++;
        } else {
            // overwrite oldest
            soundBufSum -= soundBuf[soundBufPos];
            soundBuf[soundBufPos] = v;
            soundBufSum += v;
            soundBufPos = (soundBufPos + 1) % SOUND_BUF_SIZE;
        }
    }
    
    // Turn on PMS PMS_WARMUP_MS before sampling (skip if always-on)
    if (!pmsAlwaysOn && !pmsOn && timeSinceLastSample >= (SAMPLE_INTERVAL_MS - PMS_WARMUP_MS)) {
        pms.powerOn();
        pmsOn = true;
        Serial.println("PMS5003 powered on for warmup");
    }
    
    // Sample when interval is reached
    if (timeSinceLastSample >= SAMPLE_INTERVAL_MS) {
        Serial.println("--- Sample ---");
        sampleBME280();
        sampleSound();
        sampleLight();
        samplePMS();
        
        // Turn off PMS after sampling if not configured always-on
        if (!pmsAlwaysOn) {
            pms.powerOff();
            pmsOn = false;
            Serial.println("PMS5003 powered off");
        }
        
        lastSample = now;
    }

    // Let PMS parser run in background
    pms.loop();
}
