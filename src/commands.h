#pragma once
#include <Arduino.h>

// Console command interpreter. Lines typed on USB serial or the TCP console
// (port 23), or sent to GET /cmd?c=..., are executed on the main loop; output
// goes to the Console like everything else.
void commandsSetup();

// Call from loop(): carries out deferred actions (reboot).
void commandsLoop();

// Run one command line (echoed to the console as "> line").
void commandRun(const String& line);
