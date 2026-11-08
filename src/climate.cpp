#include "climate.h"
#include "config.h"
#include "console.h"
#include "tasks.h"
#include <Wire.h>
#include <Adafruit_BME280.h>

static Adafruit_BME280 bme;
static SemaphoreHandle_t mtx;
static bool found = false;
static uint8_t addr = 0x76;

// Read len registers from reg. A STOP between the write and the read (no
// repeated start) avoids bus-sequencing quirks. Returns the Wire error code,
// or 0xFF on a short read.
static uint8_t readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    uint8_t err = Wire.endTransmission();
    if (err) return err;
    if (Wire.requestFrom(addr, len) != len) return 0xFF;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return 0;
}

static bool initSensor() {
    if (bme.begin(0x76)) addr = 0x76;
    else if (bme.begin(0x77)) addr = 0x77;
    else return found = false;
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,  // temperature
                    Adafruit_BME280::SAMPLING_X1,  // pressure
                    Adafruit_BME280::SAMPLING_X1,  // humidity
                    Adafruit_BME280::FILTER_OFF);
    return found = true;
}

// Chip ID (0x60 = BME280) and the temperature calibration words. Garbage
// here, or values changing between boots, means the I2C link corrupts reads.
static void printChip() {
    uint8_t id = 0, c[6] = {0};
    uint8_t e1 = readRegs(0xD0, &id, 1);
    uint8_t e2 = readRegs(0x88, c, 6);
    Console.printf("Climate: BME280 @0x%02X chip_id=0x%02X%s dig_T1=%u dig_T2=%d dig_T3=%d (i2c err %u/%u)\n",
        addr, id, id == 0x60 ? "" : id == 0x58 ? " (BMP280: no humidity!)" : " (unexpected)",
        c[0] | (c[1] << 8), (int16_t)(c[2] | (c[3] << 8)), (int16_t)(c[4] | (c[5] << 8)), e1, e2);
}

// One forced conversion with raw register access: tells apart "writes never
// land", "chip drops off the bus mid-conversion" and "no conversion".
static void forcedTest() {
    Wire.beginTransmission(addr);
    Wire.write(0xF2); Wire.write(0x01);  // ctrl_hum: humidity x1
    uint8_t e1 = Wire.endTransmission();
    Wire.beginTransmission(addr);
    Wire.write(0xF4); Wire.write(0x25);  // ctrl_meas: T x1, P x1, forced
    uint8_t e2 = Wire.endTransmission();
    delay(20);                           // a x1 conversion takes < 10 ms
    uint8_t r[8] = {0};
    uint8_t e3 = readRegs(0xF7, r, 8);
    Console.printf("Climate: forced test raw T=%02X%02X%02X -> %s\n", r[3], r[4], r[5],
        (e1 || e2) ? "chip not responding (wiring/contact?)"
        : e3 ? "chip dropped off the bus mid-conversion (power/contact?)"
        : (r[3] == 0x80 && r[4] == 0 && r[5] == 0) ? "still at reset value, no conversion"
        : "converted");
}

bool climateBegin() {
    mtx = xSemaphoreCreateMutex();
    Wire.begin(PIN_BME_SDA, PIN_BME_SCL);
    Wire.setTimeOut(50);  // don't hang if a line is stuck low
    i2cScan();
    Guard g(mtx);
    printChip();
    forcedTest();
    initSensor();  // soft-resets and configures, after the raw diagnostics
    Console.printf("Climate: %s\n", found ? "BME280 ready (forced mode, x1)" : "BME280 NOT FOUND (retrying on each read)");
    return found;
}

Climate climateRead() {
    Climate c;
    Guard g(mtx);
    if (!found && !initSensor()) return c;
    uint8_t raw[3] = {0};
    if (!bme.takeForcedMeasurement() || readRegs(0xFA, raw, 3) ||
        (raw[0] == 0x80 && raw[1] == 0 && raw[2] == 0)) {
        found = false;  // re-initialize on the next read
        return c;
    }
    c.temp = bme.readTemperature();
    c.hum = bme.readHumidity();
    c.pres = bme.readPressure() / 100.0f;
    c.ok = !isnan(c.temp) && !isnan(c.hum) && !isnan(c.pres);
    return c;
}

bool climateFound() { return found; }
uint8_t climateAddr() { return addr; }

void climateDiag() {
    Guard g(mtx);
    printChip();
    forcedTest();
    Console.printf("Climate: %s\n", initSensor() ? "re-initialized (forced mode, x1)" : "not found");
}

void i2cScan() {
    Guard g(mtx);
    Console.print("I2C scan:");
    int n = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Console.printf(" 0x%02X", a);
            n++;
        }
    }
    Console.println(n ? "" : " (nothing on the bus)");
}
