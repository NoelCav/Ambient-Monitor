// Host unit tests for lib/ambient (pure logic): `pio test -e native`.
#include <unity.h>
#include <string.h>
#include "pms_parser.h"
#include "stats.h"
#include "air_quality.h"
#include "levels.h"
#include "light.h"
#include "args.h"

void setUp() {}
void tearDown() {}

// Build a valid 32-byte active-mode frame from 13 data words.
static void makeFrame(uint8_t* f, const uint16_t words[13]) {
    f[0] = 0x42; f[1] = 0x4D; f[2] = 0; f[3] = 28;
    for (int i = 0; i < 13; i++) {
        f[4 + 2 * i] = words[i] >> 8;
        f[5 + 2 * i] = words[i] & 0xFF;
    }
    uint16_t sum = 0;
    for (int i = 0; i < 30; i++) sum += f[i];
    f[30] = sum >> 8;
    f[31] = sum & 0xFF;
}

static const uint16_t WORDS[13] = {11, 22, 33, 10, 20, 30, 3000, 2600, 700, 60, 2, 1, 0x9700};

static int feed(PmsParser& p, const uint8_t* data, size_t n, PmsFrame& out) {
    int got = 0;
    for (size_t i = 0; i < n; i++) got += p.push(data[i], out);
    return got;
}

void test_parser_decodes_fields() {
    uint8_t f[32];
    makeFrame(f, WORDS);
    PmsParser p;
    PmsFrame out = {};
    TEST_ASSERT_EQUAL(1, feed(p, f, 32, out));
    TEST_ASSERT_EQUAL_UINT16(11, out.cf1[0]);
    TEST_ASSERT_EQUAL_UINT16(33, out.cf1[2]);
    TEST_ASSERT_EQUAL_UINT16(10, out.pm[0]);
    TEST_ASSERT_EQUAL_UINT16(20, out.pm[1]);
    TEST_ASSERT_EQUAL_UINT16(30, out.pm[2]);
    TEST_ASSERT_EQUAL_UINT16(3000, out.cnt[0]);
    TEST_ASSERT_EQUAL_UINT16(1, out.cnt[5]);
    TEST_ASSERT_EQUAL_UINT32(1, p.frames);
    TEST_ASSERT_EQUAL_UINT32(0, p.errors);
}

void test_parser_resyncs_after_garbage_and_split_reads() {
    uint8_t f[32];
    makeFrame(f, WORDS);
    uint8_t stream[3 + 64];
    stream[0] = 0x00; stream[1] = 0x42; stream[2] = 0x42;  // noise, incl. a stray header byte
    memcpy(stream + 3, f, 32);
    memcpy(stream + 35, f, 32);
    PmsParser p;
    PmsFrame out = {};
    // Fed in uneven chunks, as UART reads arrive.
    int got = feed(p, stream, 10, out) + feed(p, stream + 10, 30, out) + feed(p, stream + 40, sizeof(stream) - 40, out);
    TEST_ASSERT_EQUAL(2, got);
    TEST_ASSERT_EQUAL_UINT32(0, p.errors);
}

void test_parser_rejects_bad_checksum_then_recovers() {
    uint8_t bad[32], good[32];
    makeFrame(bad, WORDS);
    bad[10] ^= 0x01;
    makeFrame(good, WORDS);
    PmsParser p;
    PmsFrame out = {};
    TEST_ASSERT_EQUAL(0, feed(p, bad, 32, out));
    TEST_ASSERT_EQUAL_UINT32(1, p.errors);
    TEST_ASSERT_EQUAL(1, feed(p, good, 32, out));
}

void test_parser_rejects_wrong_length() {
    uint8_t f[32];
    makeFrame(f, WORDS);
    f[3] = 20;  // passive-mode/other length
    PmsParser p;
    PmsFrame out = {};
    TEST_ASSERT_EQUAL(0, feed(p, f, 32, out));
    TEST_ASSERT_EQUAL_UINT32(1, p.errors);
}

void test_window_rms_removes_dc() {
    Window w;
    // Square wave of +-10 around a DC bias of 2000: RMS about the mean is 10.
    for (int i = 0; i < 1000; i++) w.add(i % 2 ? 2010 : 1990);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, w.mean());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, w.meanSquare());
    Window flat;
    for (int i = 0; i < 100; i++) flat.add(1234);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, flat.meanSquare());
}

void test_sound_light_stats_average_energy_and_peak() {
    SoundLightStats s;
    Window quiet, loud, light;
    for (int i = 0; i < 100; i++) {
        quiet.add(i % 2 ? 2003 : 1997);  // RMS 3
        loud.add(i % 2 ? 2040 : 1960);   // RMS 40
        light.add(1000);
    }
    s.add(quiet, light);
    s.add(quiet, light);
    s.add(quiet, light);
    s.add(loud, light);
    // Energy average: sqrt((3*9 + 1600) / 4) = sqrt(406.75)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sqrtf(406.75f), s.soundAvg());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, s.soundPeak());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, s.light.mean());
}

void test_frame_average_rolls_and_rounds() {
    FrameAverage<3> a;
    PmsFrame f = {};
    for (int v = 1; v <= 5; v++) {  // keeps 3, 4, 5
        f.pm[1] = v;
        f.cnt[0] = v * 10;
        a.add(f);
    }
    PmsFrame m = a.average();
    TEST_ASSERT_EQUAL_UINT16(4, m.pm[1]);
    TEST_ASSERT_EQUAL_UINT16(40, m.cnt[0]);
    TEST_ASSERT_EQUAL(3, (int)a.count);
    a.clear();
    TEST_ASSERT_EQUAL_UINT16(0, a.average().pm[1]);
}

void test_alt_cf3_matches_published_formula() {
    PmsFrame f = {};
    uint16_t cnt[6] = {5689, 5075, 1456, 149, 3, 0};
    memcpy(f.cnt, cnt, sizeof(cnt));
    // 3 * (0.00030418*614 + 0.0018512*3619 + 0.02069706*1307) = 3 * 33.937 = 101.81
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 101.81f, pm25AltCf3(f));
    PmsFrame z = {};
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, pm25AltCf3(z));
}

void test_levels_band_edges_are_inclusive() {
    TEST_ASSERT_EQUAL_STRING("good", pm25Level(9.0f));
    TEST_ASSERT_EQUAL_STRING("moderate", pm25Level(9.1f));
    TEST_ASSERT_EQUAL_STRING("hazardous", pm25Level(500));
    TEST_ASSERT_EQUAL_STRING("comfortable", tempLevel(22.5f));
    TEST_ASSERT_EQUAL_STRING("comfortable", humidityLevel(36));
    TEST_ASSERT_EQUAL_STRING("quiet", soundLevel(1.3f));
    TEST_ASSERT_EQUAL_STRING("dim", lightLevel(30));
    TEST_ASSERT_EQUAL_STRING("unhealthy for sensitive", pm10Level(200));
}

void test_band_index_matches_portal_gradient() {
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0, bandIndex(PM25_LIMITS, 5));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0, bandIndex(PM25_LIMITS, 9));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, bandIndex(PM25_LIMITS, (9 + 35.4f) / 2));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 4, bandIndex(PM25_LIMITS, 500));
    PmsFrame f = {};
    f.pm[2] = 254;  // PM10 at the "unhealthy for sensitive" limit -> index 2, worse than PM2.5/PM1 at 0
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2, airIndex(f));
}

void test_lux_and_one_point_calibration() {
    // Darker -> higher LDR resistance -> lower lux, monotonic.
    TEST_ASSERT_TRUE(lightLux(1000, LDR_R10_DEFAULT) < lightLux(2500, LDR_R10_DEFAULT));
    TEST_ASSERT_TRUE(lightLux(2500, LDR_R10_DEFAULT) < lightLux(4000, LDR_R10_DEFAULT));
    // Calibrating at a reading makes that reading report the measured lux.
    float r10 = ldrR10For(2500, 300);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 300, lightLux(2500, r10));
}

void test_split_first_arg_quoted_and_plain() {
    std::string a, b;
    splitFirstArg("\"Home Net\" pass word", a, b);
    TEST_ASSERT_EQUAL_STRING("Home Net", a.c_str());
    TEST_ASSERT_EQUAL_STRING("pass word", b.c_str());
    splitFirstArg("plain  secret ", a, b);
    TEST_ASSERT_EQUAL_STRING("plain", a.c_str());
    TEST_ASSERT_EQUAL_STRING("secret", b.c_str());
    splitFirstArg("open", a, b);
    TEST_ASSERT_EQUAL_STRING("open", a.c_str());
    TEST_ASSERT_EQUAL_STRING("", b.c_str());
    splitFirstArg("\"unterminated", a, b);
    TEST_ASSERT_EQUAL_STRING("unterminated", a.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parser_decodes_fields);
    RUN_TEST(test_parser_resyncs_after_garbage_and_split_reads);
    RUN_TEST(test_parser_rejects_bad_checksum_then_recovers);
    RUN_TEST(test_parser_rejects_wrong_length);
    RUN_TEST(test_window_rms_removes_dc);
    RUN_TEST(test_sound_light_stats_average_energy_and_peak);
    RUN_TEST(test_frame_average_rolls_and_rounds);
    RUN_TEST(test_alt_cf3_matches_published_formula);
    RUN_TEST(test_levels_band_edges_are_inclusive);
    RUN_TEST(test_band_index_matches_portal_gradient);
    RUN_TEST(test_lux_and_one_point_calibration);
    RUN_TEST(test_split_first_arg_quoted_and_plain);
    return UNITY_END();
}
