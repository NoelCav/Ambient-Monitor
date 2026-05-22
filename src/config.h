#pragma once

// Pins from build_flags (defaults set in platformio.ini)
#ifndef PIN_BME_SDA
#define PIN_BME_SDA 1
#endif
#ifndef PIN_BME_SCL
#define PIN_BME_SCL 2
#endif

#ifndef PIN_KY037_A
#define PIN_KY037_A 4
#endif

#ifndef PIN_PHOTO
#define PIN_PHOTO 6
#endif

#ifndef PIN_PMS_SET
#define PIN_PMS_SET 7
#endif

#ifndef PIN_PMS_RX
#define PIN_PMS_RX 17
#endif

#ifndef PIN_PMS_TX
#define PIN_PMS_TX 18
#endif

#ifndef SAMPLE_INTERVAL_MS
#define SAMPLE_INTERVAL_MS 600000  // 10 minutes
#endif

#ifndef PMS_WARMUP_MS
#define PMS_WARMUP_MS 60000  // 1 minute warmup before sampling
#endif
