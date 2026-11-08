#pragma once

// Why this boot happened, as a short token for logs/status: "poweron",
// "software" (restart/OTA), "panic" (crash), "brownout", "task_wdt", etc.
// A brownout or watchdog here is what an unexplained silence looks like.
const char* resetReasonStr();
