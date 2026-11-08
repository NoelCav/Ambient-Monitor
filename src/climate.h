#pragma once
#include <Arduino.h>
#include "reading.h"

// BME280 temperature / humidity / pressure. Forced mode, x1 oversampling: the
// chip sleeps between reads, which avoids self-heating. Thread-safe: all I2C
// access goes through one mutex, so any task may call these.

// Start I2C, print the chip diagnostics and initialize. Returns true if found.
bool climateBegin();

// One forced measurement. Re-initializes the sensor first if it was lost.
// Rejects reads still at the chip's reset value (raw temp 0x800000), which
// the Adafruit library would turn into a fake 23.52 C / 82.4 % / 699 hPa.
Climate climateRead();

bool climateFound();
uint8_t climateAddr();

// Console diagnostics: chip ID + calibration words, a raw forced conversion,
// then re-initialize. And a scan of the I2C bus.
void climateDiag();
void i2cScan();
