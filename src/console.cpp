#include "console.h"
#include "config.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <errno.h>

ConsoleOut Console;

// Ring buffer addressed by a monotonic byte count: index = pos % size. The
// backlog is the last min(written, size) bytes; a TCP client's cursor is a
// position in the same space, so one that falls a full buffer behind simply
// skips ahead and loses the oldest output instead of blocking anything.
static char ring[CONSOLE_BUF_BYTES];
static uint32_t written = 0;
static portMUX_TYPE ringLock = portMUX_INITIALIZER_UNLOCKED;

static WiFiServer server(CONSOLE_TCP_PORT);
static WiFiClient client;
static bool serverStarted = false;
static uint32_t clientPos = 0;

static const size_t TCP_CHUNK = 512;  // max bytes sent per consoleLoop() call

size_t ConsoleOut::write(const uint8_t* buf, size_t size) {
    Serial.write(buf, size);
    portENTER_CRITICAL(&ringLock);
    for (size_t i = 0; i < size; i++) {
        ring[(written + i) % CONSOLE_BUF_BYTES] = (char)buf[i];
    }
    written += size;
    portEXIT_CRITICAL(&ringLock);
    return size;
}

size_t ConsoleOut::write(uint8_t c) { return write(&c, 1); }

// Copy up to max bytes starting at stream position pos into out. Returns the
// number copied, after clamping pos to the oldest byte still buffered.
static size_t readFrom(uint32_t& pos, char* out, size_t max) {
    portENTER_CRITICAL(&ringLock);
    if (written - pos > CONSOLE_BUF_BYTES) pos = written - CONSOLE_BUF_BYTES;
    size_t n = written - pos;
    if (n > max) n = max;
    for (size_t i = 0; i < n; i++) out[i] = ring[(pos + i) % CONSOLE_BUF_BYTES];
    portEXIT_CRITICAL(&ringLock);
    return n;
}

String consoleBacklog() { return consoleSince(0); }

uint32_t consolePos() {
    portENTER_CRITICAL(&ringLock);
    uint32_t p = written;
    portEXIT_CRITICAL(&ringLock);
    return p;
}

String consoleSince(uint32_t pos) {  // pos is clamped to the oldest buffered byte
    String out;
    char chunk[256];
    size_t n;
    while ((n = readFrom(pos, chunk, sizeof(chunk))) > 0) {
        out.concat(chunk, n);
        pos += n;
    }
    return out;
}

// --- Typed input ---

static void (*commandHandler)(const String&) = nullptr;

void consoleSetCommandHandler(void (*handler)(const String& line)) { commandHandler = handler; }

// Accumulates printable bytes into a line and dispatches it on CR/LF. Telnet
// option negotiation (IAC = 0xFF + 2 bytes) and other control bytes are
// dropped, so a plain `telnet <host>` works as well as netcat.
struct LineReader {
    String line;
    uint8_t skip = 0;  // bytes left of a telnet IAC sequence
    void feed(uint8_t c) {
        if (skip) { skip--; return; }
        if (c == 0xFF) { skip = 2; return; }
        if (c == '\r' || c == '\n') {
            line.trim();
            if (line.length() && commandHandler) {
                String cmd = line;
                line = "";
                commandHandler(cmd);
            }
            line = "";
            return;
        }
        if (c == 8 || c == 127) { if (line.length()) line.remove(line.length() - 1); return; }
        if (c >= 0x20 && c < 0x7F && line.length() < 120) line += (char)c;
    }
};
static LineReader serialIn, tcpIn;

void consoleLoop() {
    while (Serial.available()) serialIn.feed(Serial.read());

    bool netUp = WiFi.status() == WL_CONNECTED || (WiFi.getMode() & WIFI_AP);
    if (!netUp) return;
    if (!serverStarted) {
        server.begin();
        server.setNoDelay(true);
        serverStarted = true;
    }

    WiFiClient incoming = server.accept();
    if (incoming) {
        if (client) client.stop();  // one client at a time; newest wins
        client = incoming;
        clientPos = 0;              // start with the full backlog
        tcpIn = LineReader();
        Console.printf("Console: TCP client connected from %s (type 'help')\n",
            client.remoteIP().toString().c_str());
    }
    if (!client) return;
    if (!client.connected()) {
        client.stop();
        return;
    }

    // Input: read whatever has arrived without blocking.
    uint8_t in[64];
    int got;
    while ((got = recv(client.fd(), in, sizeof(in), MSG_DONTWAIT)) > 0) {
        for (int i = 0; i < got; i++) tcpIn.feed(in[i]);
    }

    // Raw non-blocking send: WiFiClient::write() retries with 1 s selects (up
    // to ~10 s) when the socket buffer is full, which a client that stops
    // reading would turn into a stall. Here a full buffer just means "later".
    char chunk[TCP_CHUNK];
    size_t n = readFrom(clientPos, chunk, sizeof(chunk));
    if (n == 0) return;
    int sent = send(client.fd(), chunk, n, MSG_DONTWAIT);
    if (sent > 0) {
        clientPos += sent;
    } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        client.stop();
    }
}
