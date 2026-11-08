#pragma once
#include <string>

// Split "<first> <rest>" where first may be "double quoted" to contain spaces
// (e.g. a WiFi SSID). rest is trimmed of surrounding spaces. Pure C++ so the
// console's argument handling can be unit-tested on the host.
inline void splitFirstArg(const std::string& arg, std::string& first, std::string& rest) {
    size_t end;
    if (!arg.empty() && arg[0] == '"') {
        size_t close = arg.find('"', 1);
        if (close == std::string::npos) close = arg.size();
        first = arg.substr(1, close - 1);
        end = close + 1;
    } else {
        end = arg.find(' ');
        if (end == std::string::npos) end = arg.size();
        first = arg.substr(0, end);
    }
    rest = end < arg.size() ? arg.substr(end) : "";
    size_t a = rest.find_first_not_of(' '), b = rest.find_last_not_of(' ');
    rest = a == std::string::npos ? "" : rest.substr(a, b - a + 1);
}
