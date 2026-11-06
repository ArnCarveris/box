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

/** Renders the bowed voices of a workload: mono bowed notes with noise of the hair, a vibrato
 * and pitch changes, and wide unison bowed notes, in blocks of 128 frames.
 *
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @param frames Frames to render, a multiple of 128
 */
static void det_bowed(
    float *left,
    float *right,
    int32_t frames)
{
    box_audio_core_t *core = audio_new();
    box_audio_synth_patch_t p = {0}, q;
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    int32_t at;
    p.layers = 1;
    p.seed = 13;
    p.drift = 4;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.06f, .sustain = 0.9f, .hold = 0.2f, .release = 0.1f };
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_BOWED, .frequency = 220,
        .level = 0.3f, .brightness = 0.6f, .bow_pressure = 0.5f, .bow_speed = 0.6f,
        .bow_position = 0.11f, .bow_noise = 0.3f };
    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = BOX_AUDIO_SYNTH_LFO_SINE, .rate = 5.5f,
        .pitch = 20, .random_phase = 1 };
    q = p;
    q.layer[0].voices = 3;
    q.layer[0].detune = 14;
    q.layer[0].width = 0.8f;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, &p));
    box_audio_core_apply(core, w.data, w.size);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 2, &q));
    box_audio_core_apply(core, w.data, w.size);
    for (at = 0; at < frames; at += 128) {
        if (at % 4096 == 0) {
            uint32_t voice = (uint32_t)at / 4096;
            audio_play(core, voice, 1 + voice % 2, 0, 0.2f, 0.1f * (float)(voice % 5) - 0.2f,
                0.75f + 0.05f * (float)(voice % 8), 0);
        }

        if (at % 4096 == 2048) {
            audio_voice_set(core, (uint32_t)at / 4096, BOX_AUDIO_PARAM_PITCH, 1.1f);
        }

        box_audio_core_render(core, left + at, right + at, 128);
    }

    box_audio_core_free(core);
}

void Deterministic_synth_bowed(void) {
    // audio/deterministic
    det_bowed(audio_l, audio_r, AUDIO_FRAMES - AUDIO_FRAMES % 128);
    det_bowed(det_l, det_r, AUDIO_FRAMES - AUDIO_FRAMES % 128);
    test_assert(memcmp(audio_l, det_l, sizeof(det_l)) == 0);
    test_assert(memcmp(audio_r, det_r, sizeof(det_r)) == 0);
    test_assert(audio_peak(audio_l, 0, AUDIO_FRAMES) > 0.01f);
    test_assert(audio_peak(audio_r, 0, AUDIO_FRAMES) > 0.01f);
}
