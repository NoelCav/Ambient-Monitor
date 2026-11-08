#pragma once
#include <Arduino.h>

// Drop-in replacement for Serial in log output: every byte goes to the USB
// serial port and into a RAM backlog (CONSOLE_BUF_BYTES) that is served over
// WiFi, so the device can be debugged without a cable:
//   - GET /console on the portal  (recent output, auto-refreshing page)
//   - raw TCP on CONSOLE_TCP_PORT (backlog, then live; e.g. `nc <host> 23`)
//
// Writes never touch the network: TCP clients are fed from the backlog by
// consoleLoop(), so a slow or dead client can't stall the caller.
class ConsoleOut : public Print {
public:
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t size) override;
    using Print::write;
};

extern ConsoleOut Console;

// Call regularly: starts the TCP server once the network is up, accepts a
// client (replacing any previous one) and sends it pending output.
void consoleLoop();

// Copy of the current backlog, oldest first.
String consoleBacklog();

// Typed input: complete lines from USB serial or the TCP client are passed to
// this handler (from consoleLoop(), i.e. on the main loop).
void consoleSetCommandHandler(void (*handler)(const String& line));

// Output position markers, so a caller can capture exactly what a command
// printed: pos = consolePos(); run(); text = consoleSince(pos).
uint32_t consolePos();
String consoleSince(uint32_t pos);
