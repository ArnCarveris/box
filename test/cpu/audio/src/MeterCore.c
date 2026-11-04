/**
 * @file MeterCore.c
 * @brief Meters of the core, rendered offline: the peak and the sums of squares at the output
 * of each bus, read and started over, and the values of the effects.
 */

#include "util.h"
#include <internal.h>

/** RMS of a sine of peak 0.5 */
#define METER_SINE_RMS 0.35355339f

/** Read the meters of every bus of a core.
 *
 * @param core The core
 * @param meters Receives BOX_AUDIO_BUS_MAX meters
 */
static void meter_read(
    box_audio_core_t *core,
    box_audio_meter_t *meters)
{
    test_int(box_audio_core_meters(core, meters, BOX_AUDIO_BUS_MAX), BOX_AUDIO_BUS_MAX);
}

/** RMS of a channel of a meter of the core.
 *
 * @param square Sum of squares
 * @param frames Frames of the sum
 * @return The RMS
 */
static float meter_rms(
    double square,
    uint32_t frames)
{
    test_assert(frames > 0);

    return (float)sqrt(square / (double)frames);
}

void MeterCore_sine(void) {
    // audio/meter
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_meter_t meters[BOX_AUDIO_BUS_MAX];
    audio_sine(audio_sound_a, AUDIO_FRAMES, 1000, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_order(core, routes, 2);
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 4800);
    meter_read(core, meters);
    audio_render(core, 0, 14400);
    meter_read(core, meters);
    test_int(meters[1].frames, 14400);
    test_int(meters[0].frames, 14400);
    test_assert(fabsf(meters[1].peak_left - 0.5f) < 1e-3f);
    test_assert(fabsf(meters[1].peak_right - 0.5f) < 1e-3f);
    test_assert(fabsf(meter_rms(meters[1].square_left, meters[1].frames) - METER_SINE_RMS) <
        1e-3f);
    test_assert(fabsf(meter_rms(meters[1].square_right, meters[1].frames) - METER_SINE_RMS) <
        1e-3f);
    test_assert(fabsf(meters[0].peak_left - 0.5f) < 1e-3f);
    test_assert(fabsf(meter_rms(meters[0].square_left, meters[0].frames) - METER_SINE_RMS) <
        1e-3f);
    box_audio_core_free(core);
}

void MeterCore_read_resets(void) {
    // audio/meter
    box_audio_core_t *core = audio_new();
    box_audio_meter_t meters[BOX_AUDIO_BUS_MAX];
    box_audio_meter_t few[2];
    audio_fill(audio_sound_a, 1000, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 1000, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 1024);
    test_int(box_audio_core_meters(core, few, 2), 2);
    test_int(few[0].frames, 1024);
    test_assert(fabsf(few[0].peak_left - 0.25f) < 1e-6f);
    meter_read(core, meters);
    test_int(meters[0].frames, 0);
    test_assert(meters[0].peak_left == 0);
    test_assert(meters[0].square_left == 0);
    test_int(box_audio_core_meters(core, meters, 0), 0);
    test_int(box_audio_core_meters(NULL, meters, 4), 0);
    test_int(box_audio_core_meters(core, NULL, 4), 0);
    box_audio_core_free(core);
}

void MeterCore_not_in_order(void) {
    // audio/meter
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_meter_t meters[BOX_AUDIO_BUS_MAX];
    int32_t i;
    audio_fill(audio_sound_a, 1000, 0.25f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 1000, 1, AUDIO_RATE));
    audio_order(core, routes, 2);
    audio_play(core, 1, 1, 5, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 512);
    meter_read(core, meters);
    test_int(meters[0].frames, 512);
    test_int(meters[1].frames, 512);
    test_assert(meters[1].peak_left == 0);
    for (i = 2; i < BOX_AUDIO_BUS_MAX; i ++) {
        test_int(meters[i].frames, 0);
        test_assert(meters[i].peak_left == 0 && meters[i].peak_right == 0);
    }

    box_audio_core_free(core);
}

void MeterCore_effect_meters(void) {
    // audio/meter
    box_audio_core_t *core = audio_new();
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    int32_t i;
    test_int(box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX),
        BOX_AUDIO_EFFECT_MAX);
    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        test_assert(meters[i].reduction == 0);
    }

    box_audio_effect_reduction(core, &core->effects[3], 6.0f);
    box_audio_effect_reduction(core, &core->effects[3], 3.0f);
    box_audio_effect_reduction(core, &core->effects[4], -2.0f);
    box_audio_effect_reduction(core, &core->effects[5], NAN);
    test_int(box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX),
        BOX_AUDIO_EFFECT_MAX);
    test_assert(meters[3].reduction == 6.0f);
    test_assert(meters[4].reduction == 0);
    test_assert(meters[5].reduction == 0);
    box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    test_assert(meters[3].reduction == 0);
    box_audio_core_free(core);
}

void MeterCore_render_sizes(void) {
    // audio/meter
    static float left[AUDIO_FRAMES], right[AUDIO_FRAMES];
    box_audio_core_t *a = audio_new(), *b = audio_new();
    box_audio_meter_t ma[BOX_AUDIO_BUS_MAX], mb[BOX_AUDIO_BUS_MAX];
    int32_t sizes[] = { 1, 7, 128, 300, 1031 };
    int32_t at, n, k = 0;
    uint32_t frames = 0;
    float peak = 0;
    double square = 0;
    audio_fill(audio_sound_a, 777, 0.3f);
    audio_sine(audio_sound_b, 1000, 100, 0.2f);
    test_assert(box_audio_core_sound(a, 1, audio_sound_a, 777, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(b, 1, audio_sound_a, 777, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(a, 2, audio_sound_b, 1000, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(b, 2, audio_sound_b, 1000, 1, AUDIO_RATE));
    audio_play(a, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(b, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(a, 2, 2, 0, 1, 0.5f, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(b, 2, 2, 0, 1, 0.5f, 1, BOX_AUDIO_PLAY_LOOP);
    box_audio_core_render(a, left, right, 9600);
    meter_read(a, ma);
    for (at = 0; at < 9600; at += n) {
        n = sizes[k ++ % 5];
        n = n < 9600 - at ? n : 9600 - at;
        box_audio_core_render(b, left, right, n);
        if (k % 3 == 0 || at + n == 9600) {
            meter_read(b, mb);
            frames += mb[0].frames;
            peak = mb[0].peak_right > peak ? mb[0].peak_right : peak;
            square += mb[0].square_right;
        }
    }

    /* The frames and the peaks do not depend on the render sizes, the sums only round */
    test_int(ma[0].frames, 9600);
    test_int(frames, 9600);
    test_assert(peak == ma[0].peak_right);
    test_assert(peak > 0.3f);
    test_assert(fabs(square - ma[0].square_right) <= 1e-5 * ma[0].square_right);
    box_audio_core_free(a);
    box_audio_core_free(b);
}
