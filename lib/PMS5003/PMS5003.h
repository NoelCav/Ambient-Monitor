#pragma once
#include <Arduino.h>

class PMS5003 {
public:
    struct Data {
        uint16_t pm1_0;
        uint16_t pm2_5;
        uint16_t pm10;
    };

    struct DebugStats {
        uint32_t bytesReceived;
        uint32_t framesDecoded;
        uint32_t checksumFailures;
        uint32_t headerSkips;
        uint32_t headerMatches;
        uint32_t bufferBytes;
        uint8_t firstBytes[16];
        uint8_t firstBytesLen;
    };

    PMS5003();
    ~PMS5003();

    // Use begin with RX, TX pins, and SET pin for power control
    void begin(int rxPin, int txPin, int setPin, long baud = 9600);
    // Power control
    void powerOn();
    void powerOff();
    // Clear buffer state
    void reset();
    // Must be called frequently to feed parser
    void loop();
    // Non-blocking availability
    bool available();
    // Read latest complete frame into data. Returns true on success.
    bool read(Data* out);
    // Get parser/debug stats and reset counters for next sample interval
    DebugStats getDebugStats();

private:
    HardwareSerial* _serial = nullptr;
    int _setPin = -1;
    Stream* _stream = nullptr;
    static const int BUF_SIZE = 256;
    uint8_t _buf[BUF_SIZE];
    int _buf_pos = 0;
    volatile bool _has_data = false;
    Data _latest{};
    DebugStats _debug;
    void processBuffer();
};
