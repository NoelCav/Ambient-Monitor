#pragma once
#include "stats.h"

// Sound (KY-037 analog out) and light (photoresistor) task. Both pins are on
// ADC1, sampled continuously by DMA at ADC_SAMPLE_HZ (split between the two),
// so no sound is missed while other code runs. Samples are grouped into
// SOUND_WINDOW_MS windows (sound RMS about the window's own mean, light mean)
// and added to one accumulator per span.
// Note: analogRead() must not be used on ADC1 pins while this runs.

enum Span { SPAN_SAMPLE, SPAN_LIVE, SPAN_COUNT };  // logged sample, test reading

void soundLightBegin();

// Stats since the span was last taken, and start that span afresh.
SoundLightStats soundLightTake(Span span);
