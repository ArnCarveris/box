/**
 * @file Deterministic.c
 * @brief The same commands at the same block boundaries give bit identical output.
 */

#include "util.h"

/** Output of the second run */
static float det_l[AUDIO_FRAMES];
static float det_r[AUDIO_FRAMES];

void Deterministic_two_runs(void) {
    // audio/deterministic
    box_audio_core_t *core = audio_new();
    audio_workload(core, audio_l, audio_r, AUDIO_FRAMES);
    box_audio_core_free(core);

    core = audio_new();
    audio_workload(core, det_l, det_r, AUDIO_FRAMES);
    box_audio_core_free(core);

    test_assert(memcmp(audio_l, det_l, sizeof(det_l)) == 0);
    test_assert(memcmp(audio_r, det_r, sizeof(det_r)) == 0);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0.01f);
    test_assert(audio_peak(audio_r, 0, AUDIO_FRAMES) > 0.01f);
}

void Deterministic_two_cores_at_once(void) {
    // audio/deterministic
    box_audio_core_t *a = audio_new(), *b = audio_new();
    int32_t at;
    audio_sine(audio_sound_a, 4800, 330, 0.5f);
    test_assert(box_audio_core_sound(a, 1, audio_sound_a, 4800, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(b, 1, audio_sound_a, 4800, 1, AUDIO_RATE));
    for (at = 0; at < 20000; at += 128) {
        if (at % 1024 == 0) {
            uint32_t voice = (uint32_t)at / 1024;
            float pitch = 0.5f + 0.05f * (float)(voice % 20);
            audio_play(a, voice, 1, 0, 0.2f, 0.1f * (float)(voice % 10) - 0.5f, pitch,
                BOX_AUDIO_PLAY_LOOP);
            audio_play(b, voice, 1, 0, 0.2f, 0.1f * (float)(voice % 10) - 0.5f, pitch,
                BOX_AUDIO_PLAY_LOOP);
        }

        box_audio_core_render(a, audio_l + at, audio_r + at, 128);
        box_audio_core_render(b, det_l + at, det_r + at, 128);
    }

    test_assert(memcmp(audio_l, det_l, 20096 * sizeof(float)) == 0);
    test_assert(memcmp(audio_r, det_r, 20096 * sizeof(float)) == 0);
    box_audio_core_free(a);
    box_audio_core_free(b);
}
