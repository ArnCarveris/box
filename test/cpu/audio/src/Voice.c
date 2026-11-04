/**
 * @file Voice.c
 * @brief Voices of the audio core: exact playback, ends, loops, pitch, fades and the cap.
 */

#include "util.h"

void Voice_plays_exactly(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    int32_t i;
    for (i = 0; i < 4000; i ++) {
        audio_sound_a[i] = 0.4f * (float)sin(i * 0.05) + 0.2f * (float)sin(i * 0.31);
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    audio_play(core, 7, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 4000);
    for (i = 0; i < AUDIO_RAMP; i ++) {
        test_assert(fabsf(audio_l[i]) <= fabsf(audio_sound_a[i]));
    }

    for (i = AUDIO_RAMP; i < 4000; i ++) {
        test_assert(audio_l[i] == audio_sound_a[i]);
        test_assert(audio_r[i] == audio_sound_a[i]);
    }

    box_audio_core_free(core);
}

void Voice_plays_stereo(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    int32_t i;
    for (i = 0; i < 2000; i ++) {
        audio_sound_a[i * 2] = 0.5f * (float)sin(i * 0.02);
        audio_sound_a[i * 2 + 1] = -0.25f * (float)sin(i * 0.07);
    }

    test_assert(box_audio_core_sound(core, 2, audio_sound_a, 2000, 2, AUDIO_RATE));
    audio_play(core, 1, 2, 0, 1, 0, 1, 0);
    audio_render(core, 0, 2000);
    for (i = AUDIO_RAMP; i < 2000; i ++) {
        test_assert(audio_l[i] == audio_sound_a[i * 2]);
        test_assert(audio_r[i] == audio_sound_a[i * 2 + 1]);
    }

    box_audio_core_free(core);
}

void Voice_ends(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    box_audio_event_t e;
    audio_fill(audio_sound_a, 1000, 0.5f);
    test_assert(box_audio_core_sound(core, 0, audio_sound_a, 1000, 1, AUDIO_RATE));
    audio_play(core, 42, 0, 0, 1, 0, 1, 0);
    test_int(box_audio_core_voice_count(core), 1);
    audio_render(core, 0, 3000);
    test_assert(audio_l[999] == 0.5f);
    test_flt(audio_peak(audio_l, 1000, 3000), 0);
    test_flt(audio_peak(audio_r, 1000, 3000), 0);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_uint(e.id, 42);
    box_audio_core_free(core);
}

void Voice_release_after_end(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_event_t e;
    audio_fill(audio_sound_a, 1000, 0.5f);
    test_assert(box_audio_core_sound(core, 3, audio_sound_a, 1000, 1, AUDIO_RATE));
    audio_play(core, 1, 3, 0, 1, 0, 1, 0);
    audio_render(core, 0, 256);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_release(&w, 3));
    box_audio_core_apply(core, w.data, w.size);
    test_int(audio_events(core, BOX_AUDIO_EVENT_SOUND_FREE, NULL), 0);
    audio_render(core, 256, 256);
    test_assert(audio_l[511] == 0.5f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_SOUND_FREE, NULL), 0);
    audio_render(core, 512, 1024);
    test_int(box_audio_core_read_events(core, &e, 1), 1);
    test_int(e.type, BOX_AUDIO_EVENT_VOICE_END);
    test_uint(e.id, 1);
    test_int(box_audio_core_read_events(core, &e, 1), 1);
    test_int(e.type, BOX_AUDIO_EVENT_SOUND_FREE);
    test_uint(e.id, 3);
    audio_play(core, 2, 3, 0, 1, 0, 1, 0);
    test_int(box_audio_core_voice_count(core), 0);
    test_assert(box_audio_core_sound(core, 3, audio_sound_a, 1000, 1, AUDIO_RATE));
    box_audio_core_free(core);
}

void Voice_release_unused(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_event_t e;
    audio_fill(audio_sound_a, 100, 0.5f);
    test_assert(box_audio_core_sound(core, 5, audio_sound_a, 100, 1, AUDIO_RATE));
    test_assert(!box_audio_core_sound(core, 5, audio_sound_a, 100, 1, AUDIO_RATE));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_release(&w, 5));
    box_audio_core_apply(core, w.data, w.size);
    test_int(audio_events(core, BOX_AUDIO_EVENT_SOUND_FREE, &e), 1);
    test_uint(e.id, 5);
    box_audio_core_free(core);
}

void Voice_loop_seam(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    int32_t i;
    float bound = 0.5f * (float)(2 * AUDIO_PI * 100 / AUDIO_RATE) * 1.01f;
    audio_sine(audio_sound_a, 480, 100, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 480, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 10000);
    for (i = AUDIO_RAMP; i < 10000; i ++) {
        test_assert(audio_l[i] == audio_sound_a[i % 480]);
    }

    test_assert(audio_max_step(audio_l, AUDIO_RAMP, 10000) <= bound);
    test_int(box_audio_core_voice_count(core), 1);
    box_audio_core_free(core);
}

void Voice_loop_seam_pitch(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    float bound = 0.5f * (float)(2 * AUDIO_PI * 100 * 1.37 / AUDIO_RATE) * 1.05f;
    audio_sine(audio_sound_a, 480, 100, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 480, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1.37f, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 10000);
    test_assert(audio_max_step(audio_l, AUDIO_RAMP, 10000) <= bound);
    test_assert(fabs(audio_frequency(audio_l, 1000, 10000) - 137) < 0.5);
    box_audio_core_free(core);
}

void Voice_pitch_octave(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    double base, up;
    audio_sine(audio_sound_a, AUDIO_FRAMES, 200, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 20000);
    base = audio_frequency(audio_l, 1000, 20000);
    box_audio_core_free(core);

    core = audio_new();
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 2, 1, 0, 1, 0, 2, 0);
    audio_render(core, 0, 20000);
    up = audio_frequency(audio_l, 1000, 20000);
    test_assert(fabs(base - 200) < 0.5);
    test_assert(fabs(up - 400) < 1);
    test_assert(fabs(up / base - 2) < 0.005);
    box_audio_core_free(core);
}

void Voice_sound_rate(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    audio_sine(audio_sound_a, AUDIO_FRAMES, 200, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE / 2));
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 20000);
    test_assert(fabs(audio_frequency(audio_l, 1000, 20000) - 100) < 0.5);
    box_audio_core_free(core);
}

void Voice_set_pitch(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    audio_sine(audio_sound_a, AUDIO_FRAMES, 200, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 10000);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PITCH, 0.5f);
    audio_render(core, 10000, 20000);
    test_assert(fabs(audio_frequency(audio_l, 1000, 10000) - 200) < 0.5);
    test_assert(fabs(audio_frequency(audio_l, 11000, 30000) - 100) < 0.5);
    box_audio_core_free(core);
}

void Voice_start_no_click(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    float bound = 0.8f / AUDIO_RAMP * 1.01f;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.8f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 2000);
    test_assert(audio_l[0] > 0);
    test_assert(audio_max_step(audio_l, 0, 2000) <= bound);
    test_assert(audio_max_step(audio_r, 0, 2000) <= bound);
    test_assert(audio_l[1999] == 0.8f);
    box_audio_core_free(core);
}

void Voice_stop_no_click(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    float bound = 0.8f / AUDIO_RAMP * 1.01f;
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_event_t e;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.8f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    audio_play(core, 9, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, 1000);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stop(&w, 9, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_voice_count(core), 0);
    audio_render(core, 1000, 2000);
    test_assert(audio_max_step(audio_l, 1, 3000) <= bound);
    test_assert(audio_l[1000] > 0);
    test_flt(audio_peak(audio_l, 1000 + AUDIO_RAMP, 3000), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_uint(e.id, 9);
    box_audio_core_free(core);
}

void Voice_cap_steals_quietest(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    box_audio_event_t e;
    uint32_t i;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, i, 1, 0, i == 17 ? 0.1f : 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, 512);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, NULL), 0);

    audio_play(core, 100, 1, 0, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 17);
    test_int(e.value, 1);
    audio_render(core, 0, 512);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, &e), 1);
    test_uint(e.id, 17);

    audio_play(core, 101, 1, 0, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 0);
    test_int(e.value, 2);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    box_audio_core_free(core);
}

void Voice_steal_no_click(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    float fade = 0.01f * 0.1f / (BOX_AUDIO_CORE_STEAL_FADE * AUDIO_RATE);
    float ramp = 0.01f * 0.5f / AUDIO_RAMP;
    uint32_t i;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        audio_play(core, i, 1, 0, i == 3 ? 0.1f : 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    }

    audio_render(core, 0, 1024);
    audio_play(core, 100, 1, 0, 0.5f, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 1024, 1024);
    test_assert(audio_max_step(audio_l, 1000, 2048) <= (fade + ramp) * 1.01f);
    box_audio_core_free(core);
}

void Voice_many_plays(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    uint32_t i;
    int32_t stolen = 0;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    for (i = 0; i < 1000; i ++) {
        audio_play(core, i, 1, 0, 0.5f, 0, 1, 0);
        stolen += audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, NULL);
    }

    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    test_int(stolen, 1000 - BOX_AUDIO_VOICE_MAX);
    audio_render(core, 0, 1024);
    test_assert(audio_peak(audio_l, 0, 1024) <= 1);
    box_audio_core_free(core);
}

void Voice_play_unknown_sound(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    box_audio_event_t e;
    audio_play(core, 1, 12, 0, 1, 0, 1, 0);
    audio_play(core, 1, BOX_AUDIO_SOUND_MAX + 5, 0, 1, 0, 1, 0);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_IGNORED, &e), 2);
    test_uint(e.id, BOX_AUDIO_CMD_PLAY);
    audio_render(core, 0, 256);
    test_flt(audio_peak(audio_l, 0, 256), 0);
    box_audio_core_free(core);
}
