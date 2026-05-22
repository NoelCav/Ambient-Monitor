#include "PMS5003.h"
#include <HardwareSerial.h>

PMS5003::PMS5003() {}
PMS5003::~PMS5003() {}

void PMS5003::begin(int rxPin, int txPin, int setPin, long baud)
{
    _setPin = setPin;
    if (_setPin >= 0) {
        pinMode(_setPin, OUTPUT);
        digitalWrite(_setPin, LOW);  // Start powered off
    }
    Serial1.begin(baud, SERIAL_8N1, rxPin, txPin);
    _serial = &Serial1;
    _stream = _serial;
}

void PMS5003::powerOn()
{
    reset();  // Clear buffer state
    if (_setPin >= 0) {
        digitalWrite(_setPin, HIGH);
    }
}

void PMS5003::powerOff()
{
    if (_setPin >= 0) {
        digitalWrite(_setPin, LOW);
    }
}

void PMS5003::reset()
{
    _buf_pos = 0;
    _has_data = false;
}

void PMS5003::loop()
{
    if (!_stream) return;
    while (_stream->available()) {
        int b = _stream->read();
        if (b < 0) break;
        if (_buf_pos < BUF_SIZE) _buf[_buf_pos++] = (uint8_t)b;
        // try to find a full frame whenever buffer has at least 32 bytes
        if (_buf_pos >= 32) {
            processBuffer();
            // shift remaining bytes to front
            int rem = _buf_pos - 32;
            if (rem > 0) memmove(_buf, _buf + 32, rem);
            _buf_pos = rem;
        }
    }
}

void PMS5003::processBuffer()
{
    // look for 0x42 0x4d start sequence
    if (_buf[0] != 0x42 || _buf[1] != 0x4d) return;
    // length bytes (2..3)
    uint16_t len = (_buf[2] << 8) | _buf[3];
    if (len + 4 > 32) return; // not expected
    // checksum
    uint16_t sum = 0;
    for (int i = 0; i < 4 + len; ++i) sum += _buf[i];
    uint16_t chk = (_buf[4 + len] << 8) | _buf[5 + len];
    if (sum != chk) return;

    // Standard frames place PM1.0(2,3), PM2.5(4,5), PM10(6,7) at bytes 10..15 offset from start
    // According to specs: bytes 10-11,12-13,14-15 (zero-based)
    int idx = 10;
    uint16_t pm1_0 = (_buf[idx] << 8) | _buf[idx + 1];
    uint16_t pm2_5 = (_buf[idx + 2] << 8) | _buf[idx + 3];
    uint16_t pm10  = (_buf[idx + 4] << 8) | _buf[idx + 5];

    _latest.pm1_0 = pm1_0;
    _latest.pm2_5 = pm2_5;
    _latest.pm10 = pm10;
    _has_data = true;
}

bool PMS5003::available()
{
    return _has_data;
}

bool PMS5003::read(Data* out)
{
    if (!_has_data) return false;
    if (out) *out = _latest;
    _has_data = false;
    return true;
}
