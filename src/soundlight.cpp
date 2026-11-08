#include "soundlight.h"
#include "config.h"
#include "console.h"
#include "tasks.h"
#include <esp_adc/adc_continuous.h>

static SemaphoreHandle_t mtx;
static SoundLightStats spans[SPAN_COUNT];
static adc_continuous_handle_t adc;
static adc_channel_t soundCh, lightCh;

// Samples per window for one channel (the two channels share the rate).
static const uint32_t WINDOW_SAMPLES = ADC_SAMPLE_HZ / 2 * SOUND_WINDOW_MS / 1000;
static const uint32_t READ_RESULTS = 256;  // results per DMA read (~13 ms)

static void soundLightTask(void*) {
    static uint8_t raw[READ_RESULTS * SOC_ADC_DIGI_RESULT_BYTES];
    static adc_continuous_data_t data[READ_RESULTS];
    Window sound, light;
    for (;;) {
        uint32_t len = 0, n = 0;
        if (adc_continuous_read(adc, raw, sizeof(raw), &len, 1000) == ESP_OK &&
            adc_continuous_parse_data(adc, raw, len, data, &n) == ESP_OK) {
            for (uint32_t i = 0; i < n; i++) {
                if (!data[i].valid) continue;
                if (data[i].channel == soundCh) sound.add(data[i].raw_data);
                else if (data[i].channel == lightCh) light.add(data[i].raw_data);
            }
            if (sound.n >= WINDOW_SAMPLES) {
                Guard g(mtx);
                for (auto& s : spans) s.add(sound, light);
                sound = Window();
                light = Window();
            }
        }
        taskFeed();
    }
}

void soundLightBegin() {
    mtx = xSemaphoreCreateMutex();
    adc_unit_t unit;
    adc_continuous_io_to_channel(PIN_KY037_A, &unit, &soundCh);
    adc_continuous_io_to_channel(PIN_PHOTO, &unit, &lightCh);

    adc_continuous_handle_cfg_t hcfg = {};
    hcfg.max_store_buf_size = 4 * READ_RESULTS * SOC_ADC_DIGI_RESULT_BYTES;  // ~50 ms of slack
    hcfg.conv_frame_size = READ_RESULTS * SOC_ADC_DIGI_RESULT_BYTES;
    hcfg.flags.flush_pool = 1;  // if the task falls behind, drop old data, don't stall
    ESP_ERROR_CHECK(adc_continuous_new_handle(&hcfg, &adc));

    adc_digi_pattern_config_t pattern[2] = {};
    adc_channel_t chans[2] = {soundCh, lightCh};
    for (int i = 0; i < 2; i++) {
        pattern[i].atten = ADC_ATTEN_DB_12;  // 0-3.1 V
        pattern[i].channel = chans[i];
        pattern[i].unit = ADC_UNIT_1;
        pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    adc_continuous_config_t cfg = {};
    cfg.pattern_num = 2;
    cfg.adc_pattern = pattern;
    cfg.sample_freq_hz = ADC_SAMPLE_HZ;
    cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    ESP_ERROR_CHECK(adc_continuous_config(adc, &cfg));
    ESP_ERROR_CHECK(adc_continuous_start(adc));
    Console.printf("Sound/light: continuous ADC at %u Hz, %u ms windows\n",
        (unsigned)ADC_SAMPLE_HZ, (unsigned)SOUND_WINDOW_MS);
    startTask("soundlight", soundLightTask, 4096, 5);
}

SoundLightStats soundLightTake(Span span) {
    Guard g(mtx);
    SoundLightStats s = spans[span];
    spans[span] = SoundLightStats();
    return s;
}
