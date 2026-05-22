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

void sampleBME280()
{
    if (bme.readTemperature() != NAN) {
        float t = bme.readTemperature();
        float p = bme.readPressure() / 100.0F;
        float h = bme.readHumidity();
        Serial.printf("BME280: T=%.2f C, P=%.2f hPa, H=%.2f %%\n", t, p, h);
    } else {
        Serial.println("BME280: read failed");
    }
}

void sampleSound()
{
    int sound = analogRead(PIN_KY037_A);
    Serial.printf("KY-037 Sound (ADC) = %d\n", sound);
}

void sampleLight()
{
    int light = analogRead(PIN_PHOTO);
    Serial.printf("Photoresistor (ADC) = %d\n", light);
}

void samplePMS()
{
    if (pms.available()) {
        PMS5003::Data d;
        if (pms.read(&d)) {
            Serial.printf("PMS PM1.0=%d PM2.5=%d PM10=%d\n", d.pm1_0, d.pm2_5, d.pm10);
        } else {
            Serial.println("PMS: parse failed");
        }
    } else {
        Serial.println("PMS: no data available");
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

    // PMS5003 setup: initialize with SET pin for power control
    pms.begin(PIN_PMS_RX, PIN_PMS_TX, PIN_PMS_SET, 9600);

    // Decide behavior based on interval vs warmup: if interval <= warmup,
    // keep PMS always on to avoid power-cycling for short intervals.
    if (SAMPLE_INTERVAL_MS <= PMS_WARMUP_MS) {
        pmsAlwaysOn = true;
        pms.powerOn(); // keep powered for entire runtime
        pmsOn = true;
        Serial.println("PMS5003: configured always-on (interval <= warmup)");
        lastSample = millis();
    } else {
        pmsAlwaysOn = false;
        // Ensure sensor is off initially; schedule initial warmup so first
        // sample happens after one warmup period. Set lastSample back by
        // (interval - warmup) so loop will immediately power on for warmup.
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
