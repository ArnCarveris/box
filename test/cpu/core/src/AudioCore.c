/**
 * @file AudioCore.c
 * @brief Tests for the audio core: the sine tone it renders, its frequency and its level.
 */

#include <bake_test.h>
#include <core.h>
#include <math.h>
#include <stdlib.h>

/** Pi, which math.h of a strict C11 build does not define */
#define AUDIO_PI 3.14159265358979323846

/** Frames of the longest render of a case, one second at 48 kHz */
#define AUDIO_FRAMES 48000

static float audio_left[AUDIO_FRAMES];
static float audio_right[AUDIO_FRAMES];

/** Render frames in quanta of the web, as the worklet does */
static void audio_render(
    box_audio_core_t *core,
    int32_t frames)
{
    int32_t at;
    for (at = 0; at < frames; at += BOX_AUDIO_CORE_QUANTUM) {
        int32_t n = frames - at < BOX_AUDIO_CORE_QUANTUM ? frames - at : BOX_AUDIO_CORE_QUANTUM;
        box_audio_core_render(core, audio_left + at, audio_right + at, n);
    }
}

/** Count the upward zero crossings of the left channel from a frame on */
static int32_t audio_crossings(
    int32_t from,
    int32_t frames)
{
    int32_t i, count = 0;
    for (i = from + 1; i < frames; i ++) {
        if (audio_left[i - 1] < 0 && audio_left[i] >= 0) {
            count ++;
        }
    }

    return count;
}

/** The largest absolute sample of the left channel from a frame on */
static float audio_peak(
    int32_t from,
    int32_t frames)
{
    int32_t i;
    float peak = 0;
    for (i = from; i < frames; i ++) {
        float v = fabsf(audio_left[i]);
        peak = v > peak ? v : peak;
    }

    return peak;
}

void AudioCore_silent_at_start(void) {
    // audio/core
    box_audio_core_t core;
    box_audio_core_init(&core, 48000);
    box_audio_core_set_frequency(&core, 440);
    audio_render(&core, 4800);
    test_flt(audio_peak(0, 4800), 0);
}

void AudioCore_sin_accuracy(void) {
    // audio/core
    int i;
    for (i = -2000; i <= 2000; i ++) {
        double turns = i / 997.0;
        test_assert(fabs(box_audio_core_sin(turns) - sin(turns * 2 * AUDIO_PI)) < 1e-6);
    }
}

void AudioCore_frequency_44100(void) {
    // audio/core
    box_audio_core_t core;
    box_audio_core_init(&core, 44100);
    box_audio_core_set_frequency(&core, 440);
    box_audio_core_set_level(&core, 0.25f);
    audio_render(&core, 44100);
    test_assert(abs(audio_crossings(0, 44100) - 440) <= 1);
}

void AudioCore_frequency_48000(void) {
    // audio/core
    box_audio_core_t core;
    box_audio_core_init(&core, 48000);
    box_audio_core_set_frequency(&core, 440);
    box_audio_core_set_level(&core, 0.25f);
    audio_render(&core, 48000);
    test_assert(abs(audio_crossings(0, 48000) - 440) <= 1);
}

void AudioCore_peak_level(void) {
    // audio/core
    box_audio_core_t core;
    box_audio_core_init(&core, 48000);
    box_audio_core_set_frequency(&core, 440);
    box_audio_core_set_level(&core, 0.25f);
    audio_render(&core, 48000);
    test_assert(fabsf(audio_peak(4800, 48000) - 0.25f) < 1e-3f);
}

void AudioCore_level_ramp(void) {
    // audio/core
    box_audio_core_t core;
    int32_t ramp = (int32_t)(BOX_AUDIO_CORE_RAMP * 48000);
    int32_t i;
    box_audio_core_init(&core, 48000);
    box_audio_core_set_frequency(&core, 440);
    box_audio_core_set_level(&core, 1);
    audio_render(&core, 4800);
    for (i = 1; i < ramp; i ++) {
        test_assert(fabsf(audio_left[i]) <= (float)(i + 1) / (float)ramp + 1e-4f);
    }

    test_assert(audio_peak(ramp, 4800) > 0.99f);
}

void AudioCore_set_frequency(void) {
    // audio/core
    box_audio_core_t core;
    box_audio_core_init(&core, 48000);
    box_audio_core_set_frequency(&core, 440);
    box_audio_core_set_level(&core, 0.25f);
    audio_render(&core, 4800);
    box_audio_core_set_frequency(&core, 880);
    test_flt(core.frequency, 880);
    audio_render(&core, 48000);
    test_assert(abs(audio_crossings(0, 48000) - 880) <= 1);
    test_assert(fabsf(audio_peak(0, 48000) - 0.25f) < 1e-3f);
}

void AudioCore_phase_continuous_across_blocks(void) {
    // audio/core
    box_audio_core_t a, b;
    float whole[256];
    float right[256];
    int32_t i;
    box_audio_core_init(&a, 44100);
    box_audio_core_init(&b, 44100);
    box_audio_core_set_frequency(&a, 1000);
    box_audio_core_set_frequency(&b, 1000);
    box_audio_core_set_level(&a, 0.5f);
    box_audio_core_set_level(&b, 0.5f);
    box_audio_core_render(&a, whole, right, 256);
    box_audio_core_render(&b, audio_left, audio_right, 100);
    box_audio_core_render(&b, audio_left + 100, audio_right + 100, 156);
    for (i = 0; i < 256; i ++) {
        test_flt(audio_left[i], whole[i]);
    }
}

void AudioCore_stereo_identical(void) {
    // audio/core
    box_audio_core_t core;
    int32_t i;
    box_audio_core_init(&core, 48000);
    box_audio_core_set_frequency(&core, 440);
    box_audio_core_set_level(&core, 0.25f);
    audio_render(&core, 4800);
    for (i = 0; i < 4800; i ++) {
        test_flt(audio_left[i], audio_right[i]);
    }
}

void AudioCore_clamps(void) {
    // audio/core
    box_audio_core_t core;
    box_audio_core_init(&core, 48000);
    box_audio_core_set_frequency(&core, 100000);
    test_flt(core.frequency, 24000);
    box_audio_core_set_frequency(&core, -5);
    test_flt(core.frequency, 0);
    box_audio_core_set_frequency(&core, NAN);
    test_flt(core.frequency, 0);
    box_audio_core_set_level(&core, 3);
    test_flt(core.level, 1);
    box_audio_core_set_level(&core, -1);
    test_flt(core.level, 0);
    box_audio_core_set_level(&core, NAN);
    test_flt(core.level, 0);
}

void AudioCore_invalid_sample_rate(void) {
    // audio/core
    box_audio_core_t core;
    box_audio_core_init(&core, 0);
    test_flt(core.sample_rate, 48000);
    box_audio_core_set_frequency(&core, 440);
    box_audio_core_set_level(&core, 0.25f);
    audio_render(&core, 48000);
    test_assert(abs(audio_crossings(0, 48000) - 440) <= 1);
}
