/**
 * @file Ramp.c
 * @brief Ramps of the audio core: a step of a volume, pan, mute, send or cutoff never jumps.
 */

#include "util.h"

/** Create a core with a looped constant sound that plays on bus 1, which feeds the master.
 *
 * @param level Level of the sound
 * @return The core
 */
static box_audio_core_t* ramp_core(
    float level)
{
    box_audio_route_t routes[] = {
        AUDIO_ROUTE(1, 0, 2), AUDIO_OUT(2, 0), AUDIO_MASTER
    };
    box_audio_core_t *core = audio_new();
    audio_fill(audio_sound_a, 1024, level);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 1024, 1, AUDIO_RATE));
    audio_order(core, routes, 3);
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 2048);

    return core;
}

void Ramp_volume_step(void) {
    // audio/ramp
    box_audio_core_t *core = ramp_core(0.5f);
    float bound = 0.5f * 0.8f / AUDIO_RAMP * 1.01f;
    test_assert(audio_l[2047] == 0.5f);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.2f);
    audio_render(core, 2048, 2048);
    test_assert(audio_max_step(audio_l, 2048, 4096) <= bound);
    test_assert(audio_max_step(audio_r, 2048, 4096) <= bound);
    test_assert(audio_l[2048] < 0.5f);
    test_assert(audio_l[2048] > 0.49f);
    test_assert(fabsf(audio_l[2048 + AUDIO_RAMP] - 0.1f) < 1e-6f);
    test_assert(fabsf(audio_l[4095] - 0.1f) < 1e-6f);
    box_audio_core_free(core);
}

void Ramp_volume_up(void) {
    // audio/ramp
    box_audio_core_t *core = ramp_core(0.5f);
    float bound = 0.5f / AUDIO_RAMP * 1.01f;
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0);
    audio_render(core, 0, 2048);
    test_flt(audio_l[2047], 0);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 1);
    audio_render(core, 2048, 2048);
    test_assert(audio_max_step(audio_l, 1, 4096) <= bound);
    test_assert(audio_l[4095] == 0.5f);
    box_audio_core_free(core);
}

void Ramp_bus_volume_step(void) {
    // audio/ramp
    box_audio_core_t *core = ramp_core(0.5f);
    float bound = 0.5f / AUDIO_RAMP * 1.01f;
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0);
    audio_render(core, 2048, 1024);
    audio_bus_set(core, 0, BOX_AUDIO_PARAM_VOLUME, 0.25f);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 1);
    audio_render(core, 3072, 1024);
    test_assert(audio_max_step(audio_l, 1, 4096) <= bound);
    test_assert(fabsf(audio_l[4095] - 0.125f) < 1e-6f);
    box_audio_core_free(core);
}

void Ramp_pan_step(void) {
    // audio/ramp
    box_audio_core_t *core = ramp_core(0.5f);
    float bound = 0.5f * sqrtf(2) / AUDIO_RAMP * 1.01f;
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, -1);
    audio_render(core, 2048, 1024);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, 1);
    audio_render(core, 3072, 1024);
    test_assert(audio_max_step(audio_l, 1, 4096) <= bound);
    test_assert(audio_max_step(audio_r, 1, 4096) <= bound);
    test_flt(audio_l[4095], 0);
    box_audio_core_free(core);
}

void Ramp_mute_step(void) {
    // audio/ramp
    box_audio_core_t *core = ramp_core(0.5f);
    float bound = 0.5f / AUDIO_RAMP * 1.01f;
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_MUTE, 1);
    audio_render(core, 2048, 1024);
    test_flt(audio_l[3071], 0);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_MUTE, 0);
    audio_render(core, 3072, 1024);
    test_assert(audio_max_step(audio_l, 1, 4096) <= bound);
    test_assert(audio_l[4095] == 0.5f);
    box_audio_core_free(core);
}

void Ramp_send_step(void) {
    // audio/ramp
    box_audio_core_t *core = ramp_core(0.25f);
    float bound = 0.25f / AUDIO_RAMP * 1.01f;
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, 1);
    audio_render(core, 2048, 2048);
    test_assert(audio_max_step(audio_l, 1, 4096) <= bound);
    test_assert(fabsf(audio_l[4095] - 0.5f) < 1e-6f);
    box_audio_core_free(core);
}

void Ramp_restart_midway(void) {
    // audio/ramp
    box_audio_core_t *core = ramp_core(0.5f);
    float bound = 0.5f / AUDIO_RAMP * 1.01f;
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0);
    audio_render(core, 2048, 100);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 1);
    audio_render(core, 2148, 100);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0);
    audio_render(core, 2248, 1848);
    test_assert(audio_max_step(audio_l, 1, 4096) <= bound);
    test_flt(audio_l[4095], 0);
    box_audio_core_free(core);
}

void Ramp_cutoff_step(void) {
    // audio/ramp
    box_audio_core_t *core = audio_new();
    int32_t i, ramp = (int32_t)(BOX_AUDIO_CUTOFF_RAMP * AUDIO_RATE);
    audio_sine(audio_sound_a, AUDIO_RATE, 8000, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP | BOX_AUDIO_PLAY_ATTACK);
    audio_render(core, 0, 2048);
    test_assert(audio_peak(audio_l, 1024, 2048) > 0.43f);

    /* The filter closes over a ramp: the first frames are still bright, then dark */
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_CUTOFF, 200);
    audio_render(core, 2048, 4096);
    test_assert(audio_peak(audio_l, 2048, 2048 + ramp / 20) > 0.1f);
    test_assert(audio_peak(audio_l, 2048 + ramp + 64, 6144) < 0.01f);
    test_assert(audio_peak(audio_r, 2048 + ramp + 64, 6144) < 0.01f);
    test_assert(audio_peak(audio_l, 2048 + ramp + 64, 6144) > 0);

    /* Open again it passes the sound as is once the ramp ended */
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_CUTOFF, 0);
    audio_render(core, 6144, 4096);
    for (i = 6144 + ramp + 8; i < 10240; i ++) {
        test_assert(fabsf(audio_l[i] - audio_sound_a[i]) < 1e-5f);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    box_audio_core_free(core);
}
