#pragma once

// Why this boot happened, as a short token for logs/status: "poweron",
// "software" (restart/OTA), "panic" (crash), "brownout", "task_wdt", etc.
// A brownout or watchdog here is what an unexplained silence looks like.
const char* resetReasonStr();

// Firmware rollback. After an OTA update the new firmware boots "pending";
// healthLoop() marks it good once it has run HEALTHY_AFTER_S with a reading
// taken and the portal reachable (WiFi or setup AP). If it crashes, hangs
// (watchdog) or is rebooted before that, the bootloader goes back to the
// previous firmware.
void healthLoop();
const char* firmwareState();  // "ok", "pending" (on trial) or "unknown"
