#pragma once
#include <Arduino.h>

class PMS5003 {
public:
    struct Data {
        // Atmospheric-environment concentrations (ug/m3), Data4-6
        uint16_t pm1_0;
        uint16_t pm2_5;
        uint16_t pm10;
        // CF=1 "standard particle" concentrations (ug/m3), Data1-3
        uint16_t pm1_0_cf1;
        uint16_t pm2_5_cf1;
        uint16_t pm10_cf1;
        // Particle counts per 0.1 L of air with diameter beyond N um, Data7-12
        uint16_t cnt_0_3;
        uint16_t cnt_0_5;
        uint16_t cnt_1_0;
        uint16_t cnt_2_5;
        uint16_t cnt_5_0;
        uint16_t cnt_10;
        // ms since the previous decoded frame (0 for the first after reset).
        // Datasheet: ~2300 ms in stable mode, 200-800 ms in fast mode.
        uint32_t frameIntervalMs;
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
    unsigned long _lastFrameMs = 0;
    DebugStats _debug;
    void processBuffer();
};
