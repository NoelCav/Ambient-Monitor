#include "firestore_upload.h"
#include "wifi_manager.h"
#include "config.h"
#include "params.h"
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <time.h>

static FirebaseData fbdo;
static FirebaseAuth auth;
static FirebaseConfig fbConfig;
static bool firebaseReady = false;

// --- Ring buffer for offline storage ---
// QUEUE_SIZE is defined in config.h (1008 slots ≈ 7 days at the 10-min default,
// ~40 KB RAM). Offline coverage scales with the runtime sample interval.
static SensorReading queue[QUEUE_SIZE];
static unsigned queueHead = 0;
static unsigned queueTail = 0;
static unsigned queueCount = 0;
static bool firstUploadLogged = false;

static void enqueue(const SensorReading& r) {
    queue[queueHead] = r;
    queueHead = (queueHead + 1) % QUEUE_SIZE;
    if (queueCount < QUEUE_SIZE) {
        queueCount++;
    } else {
        queueTail = (queueTail + 1) % QUEUE_SIZE;
        Serial.println("Firestore: queue full, oldest reading dropped");
    }
}

static bool dequeue(SensorReading& r) {
    if (queueCount == 0) return false;
    r = queue[queueTail];
    queueTail = (queueTail + 1) % QUEUE_SIZE;
    queueCount--;
    return true;
}

// --- Time helpers ---

static time_t getNow() {
    time_t now;
    time(&now);
    return now;
}

// ISO 8601 timestamp for Firestore timestampValue field
static String isoTimestamp(time_t t) {
    struct tm timeinfo;
    gmtime_r(&t, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buf);
}

// --- Firebase helpers ---

static bool sendReading(const SensorReading& r) {
    if (!Firebase.ready()) return false;

    time_t now = getNow();
    // Don't upload if NTP hasn't synced yet (time < year 2024)
    if (now < 1704067200) {
        Serial.println("Firestore: skipping upload, NTP not synced yet");
        return false;
    }

    // expireAt = now + 6 months (~182 days)
    time_t expireAt = now + (182L * 24 * 3600);

    FirebaseJson doc;
    doc.set("fields/device_id/stringValue", params().deviceId);
    doc.set("fields/timestamp/integerValue", String((long)now));
    doc.set("fields/expireAt/timestampValue", isoTimestamp(expireAt));

    if (r.bme_valid) {
        doc.set("fields/temperature/doubleValue", r.temperature);
        doc.set("fields/humidity/doubleValue", r.humidity);
        doc.set("fields/pressure/doubleValue", r.pressure);
    }
    doc.set("fields/bme_valid/booleanValue", r.bme_valid);

    if (r.pms_valid) {
        doc.set("fields/pm1_0/integerValue", String(r.pm1_0));
        doc.set("fields/pm2_5/integerValue", String(r.pm2_5));
        doc.set("fields/pm10/integerValue", String(r.pm10));
    }
    doc.set("fields/pms_valid/booleanValue", r.pms_valid);

    doc.set("fields/sound_avg/integerValue", String(r.sound_avg));
    doc.set("fields/sound_peak/integerValue", String(r.sound_peak));
    doc.set("fields/light/integerValue", String(r.light));

    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID,
                                           "", "raw", doc.raw())) {
        if (!firstUploadLogged) {
            firstUploadLogged = true;
            Serial.println("Firestore: first upload successful — pipeline verified");
        }
        return true;
    } else {
        Serial.printf("Firestore: upload failed - %s\n", fbdo.errorReason().c_str());
        return false;
    }
}

// --- Public API ---

void firestoreSetup() {
    if (!params().uploadEnabled) {
        Serial.println("Firestore: uploads disabled (serial-only mode)");
        return;
    }
    if (!wifiConnected()) {
        Serial.println("Firestore: skipping setup, no WiFi");
        return;
    }

    fbConfig.api_key = FIREBASE_API_KEY;
    auth.user.email = FIREBASE_DEVICE_EMAIL;
    auth.user.password = FIREBASE_DEVICE_PASSWORD;
    fbConfig.token_status_callback = tokenStatusCallback;

    Firebase.begin(&fbConfig, &auth);
    Firebase.reconnectNetwork(true);
    fbdo.setBSSLBufferSize(2048, 1024);

    firebaseReady = true;
    Serial.println("Firestore: initialized with device auth");
}

void queueReading(const SensorReading& r) {
    enqueue(r);
    Serial.printf("Firestore: reading queued (%u/%u in buffer)\n", queueCount, QUEUE_SIZE);
}

void firestoreLoop(unsigned maxPerCall) {
    if (!params().uploadEnabled) return;

    // Try late init if WiFi came up after setup
    if (wifiConnected() && !firebaseReady) {
        firestoreSetup();
    }
    if (!wifiConnected() || !firebaseReady) return;

    unsigned sent = 0;
    while (queueCount > 0 && sent < maxPerCall) {
        SensorReading r;
        if (!dequeue(r)) break;

        if (sendReading(r)) {
            sent++;
        } else {
            // Un-dequeue: move tail back so this reading retries next cycle
            queueTail = (queueTail == 0) ? QUEUE_SIZE - 1 : queueTail - 1;
            queueCount++;
            Serial.println("Firestore: upload failed, will retry next cycle");
            break;
        }
    }

    if (sent > 0) {
        Serial.printf("Firestore: flushed %u readings (%u remaining)\n", sent, queueCount);
    }
}

unsigned firestoreQueueCount() { return queueCount; }
