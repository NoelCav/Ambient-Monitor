#include "PMS5003.h"
#include <HardwareSerial.h>
PMS5003::PMS5003() {}
PMS5003::~PMS5003() {}
void PMS5003::begin(int rxPin, int txPin, int setPin, long baud)
{
    _setPin = setPin;
    if (_setPin >= 0) {
        pinMode(_setPin, OUTPUT);
        digitalWrite(_setPin, HIGH);
    }
    Serial1.begin(baud, SERIAL_8N1, rxPin, txPin);
    _serial = &Serial1;
    _stream = _serial;
    _debug.bytesReceived = 0;
    _debug.framesDecoded = 0;
    _debug.checksumFailures = 0;
    _debug.headerSkips = 0;
    _debug.headerMatches = 0;
    _debug.bufferBytes = 0;
    _debug.firstBytesLen = 0;
}
void PMS5003::powerOn()
{
    reset();
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
        if (_debug.firstBytesLen < sizeof(_debug.firstBytes)) {
            _debug.firstBytes[_debug.firstBytesLen++] = (uint8_t)b;
        }
        _debug.bytesReceived++;
    }
    int parsePos = 0;
    while (_buf_pos - parsePos >= 4) {
        // Find header
        if (_buf[parsePos] != 0x42 || _buf[parsePos + 1] != 0x4d) {
            parsePos++;
            _debug.headerSkips++;
            continue;
        }
        _debug.headerMatches++;

        // len field includes the 2 checksum bytes at the end
        uint16_t len = (_buf[parsePos + 2] << 8) | _buf[parsePos + 3];

        // Total frame: 2 (header) + 2 (len field) + len (payload + checksum)
        int frameSize = 2 + 2 + len;
        if (frameSize > BUF_SIZE) {
            parsePos++;
            _debug.headerSkips++;
            continue;
        }
        if (_buf_pos - parsePos < frameSize) break;

        // Checksum covers everything except the checksum bytes themselves
        // i.e. header(2) + len field(2) + payload(len-2) = 2+2+len-2 = len+2 bytes
        uint16_t sum = 0;
        for (int i = 0; i < 2 + len; ++i) sum += _buf[parsePos + i];

        // Checksum sits at the last 2 bytes of the frame
        uint16_t chk = (_buf[parsePos + 2 + len] << 8) | _buf[parsePos + 3 + len];

        if (sum != chk) {
            parsePos++;
            _debug.checksumFailures++;
            continue;
        }

        // PM values are at bytes 10-15 from frame start (atmospheric units)
        int idx = parsePos + 10;
        uint16_t pm1_0 = (_buf[idx]     << 8) | _buf[idx + 1];
        uint16_t pm2_5 = (_buf[idx + 2] << 8) | _buf[idx + 3];
        uint16_t pm10  = (_buf[idx + 4] << 8) | _buf[idx + 5];
        _latest.pm1_0 = pm1_0;
        _latest.pm2_5 = pm2_5;
        _latest.pm10  = pm10;
        _has_data = true;
        _debug.framesDecoded++;
        parsePos += frameSize;
    }
    if (parsePos > 0) {
        int rem = _buf_pos - parsePos;
        if (rem > 0) memmove(_buf, _buf + parsePos, rem);
        _buf_pos = rem;
    }
    _debug.bufferBytes = _buf_pos;
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
PMS5003::DebugStats PMS5003::getDebugStats()
{
    DebugStats stats = _debug;
    _debug.bytesReceived = 0;
    _debug.framesDecoded = 0;
    _debug.checksumFailures = 0;
    _debug.headerSkips = 0;
    _debug.headerMatches = 0;
    _debug.bufferBytes = _buf_pos;
    _debug.firstBytesLen = 0;
    return stats;
}