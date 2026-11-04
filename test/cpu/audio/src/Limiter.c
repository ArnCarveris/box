/**
 * @file Limiter.c
 * @brief The limiter of the master: the output stays within -1..1, a quiet signal is unchanged.
 */

#include "util.h"

/* The gain of the limiter is linked: the louder channel reaches the threshold, the other
 * keeps its ratio to it */
void Limiter_full_voices(void) {
    // audio/limiter
    box_audio_core_t *core = audio_new();
    uint32_t i;
    audio_fill(audio_sound_a, 4096, 1);
    audio_sine(audio_sound_b, 4096, 100, 1);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4096, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 4096, 1, AUDIO_RATE));
    audio_bus_set(core, 0, BOX_AUDIO_PARAM_VOLUME, BOX_AUDIO_VOLUME_MAX);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, i, 1 + i % 2, 0, BOX_AUDIO_VOLUME_MAX, i % 3 ? -1 : 1, 1,
            BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, AUDIO_FRAMES);
    for (i = 0; i < AUDIO_FRAMES; i ++) {
        test_assert(audio_l[i] >= -1 && audio_l[i] <= 1);
        test_assert(audio_r[i] >= -1 && audio_r[i] <= 1);
    }

    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0.9f);
    test_assert(audio_peak(audio_r, 0, AUDIO_FRAMES) > 0.1f);
    box_audio_core_free(core);
}

void Limiter_quiet_unchanged(void) {
    // audio/limiter
    box_audio_core_t *core = audio_new();
    int32_t i;
    audio_sine(audio_sound_a, 480, 100, 0.9f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 480, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, AUDIO_FRAMES);
    for (i = AUDIO_RAMP; i < AUDIO_FRAMES; i ++) {
        test_assert(audio_l[i] == audio_sound_a[i % 480]);
        test_assert(audio_r[i] == audio_sound_a[i % 480]);
    }

    box_audio_core_free(core);
}

void Limiter_recovers(void) {
    // audio/limiter
    box_audio_core_t *core = audio_new();
    uint8_t bytes[64];
    box_audio_writer_t w;
    int32_t i, quiet = 2048 + AUDIO_RAMP + (int32_t)(BOX_AUDIO_LIMITER_RELEASE * AUDIO_RATE);
    audio_sine(audio_sound_a, 480, 100, 0.5f);
    audio_fill(audio_sound_b, 1024, 1);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 480, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 1024, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 2, 0, BOX_AUDIO_VOLUME_MAX, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 2048);
    test_assert(audio_peak(audio_l, 0, 2048) <= BOX_AUDIO_LIMITER_THRESHOLD + 1e-6f);
    test_assert(audio_peak(audio_l, 0, 2048) > 0.9f);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 2, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 2048, AUDIO_FRAMES - 2048);
    for (i = quiet; i < AUDIO_FRAMES; i ++) {
        test_assert(audio_l[i] == audio_sound_a[i % 480]);
    }

    box_audio_core_free(core);
}

void Limiter_threshold(void) {
    // audio/limiter
    box_audio_core_t *core = audio_new();
    audio_fill(audio_sound_a, 1024, 0.8f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 1024, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 2, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 4096);
    test_assert(fabsf(audio_l[4095] - BOX_AUDIO_LIMITER_THRESHOLD) < 1e-6f);
    test_assert(audio_peak(audio_l, 0, 4096) <= BOX_AUDIO_LIMITER_THRESHOLD + 1e-6f);
    box_audio_core_free(core);
}
