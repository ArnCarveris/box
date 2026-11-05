/**
 * @file Voice.c
 * @brief Voices of the audio core: exact playback, ends, loops, pitch, fades, the cap and the
 * priorities of the plays, in the core and through the module.
 */

#include "world.h"
#include "../../../../src/jobs/jobs.h"

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

/** Play a looping sound on bus 0 at a priority with a single command.
 *
 * @param core The core
 * @param voice Handle of the voice
 * @param sound Id of the sound
 * @param volume Volume
 * @param priority Priority of the voice
 */
static void voice_play(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t sound,
    float volume,
    uint32_t priority)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_priority(&w, voice, sound, 0, volume, 0, 1, 1,
        BOX_AUDIO_PLAY_LOOP, priority, 0));
    box_audio_core_apply(core, w.data, w.size);
}

/** Take the events of a core and count those of a type, and those of the type for a handle.
 *
 * @param core The core
 * @param type A box_audio_event_type_t
 * @param id The handle
 * @param last Receives the last event of the type for the handle, may be NULL
 * @return Events of the type for the handle
 */
static int32_t voice_events_of(
    box_audio_core_t *core,
    int32_t type,
    uint32_t id,
    box_audio_event_t *last)
{
    box_audio_event_t events[64];
    int32_t count, i, result = 0;
    while ((count = box_audio_core_read_events(core, events, 64)) > 0) {
        for (i = 0; i < count; i ++) {
            if (events[i].type == type && events[i].id == id) {
                result ++;
                if (last) {
                    *last = events[i];
                }
            }
        }
    }

    return result;
}

void Voice_oneshots_spare_track(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    box_audio_event_t events[64];
    int32_t count, i, stolen = 0, ended = 0;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    voice_play(core, 1000, 1, 0.1f, 1);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX - 1; i ++) {
        voice_play(core, (uint32_t)i, 1, 0.5f, 0);
    }

    audio_render(core, 0, 512);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, NULL), 0);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        voice_play(core, (uint32_t)(100 + i), 1, 0.5f, 0);
        while ((count = box_audio_core_read_events(core, events, 64)) > 0) {
            int32_t k;
            for (k = 0; k < count; k ++) {
                test_assert(events[k].id != 1000);
                stolen += events[k].type == BOX_AUDIO_EVENT_VOICE_STOLEN;
                ended += events[k].type == BOX_AUDIO_EVENT_VOICE_END;
            }
        }
    }

    test_int(stolen, BOX_AUDIO_VOICE_MAX);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    test_int(box_audio_core_refused(core), 0);
    audio_render(core, 512, 1024);
    while ((count = box_audio_core_read_events(core, events, 64)) > 0) {
        for (i = 0; i < count; i ++) {
            test_assert(events[i].id != 1000);
            ended += events[i].type == BOX_AUDIO_EVENT_VOICE_END;
        }
    }

    test_int(ended, BOX_AUDIO_VOICE_MAX);
    box_audio_core_free(core);
}

void Voice_track_steals_quietest_oneshot(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    box_audio_event_t e;
    uint32_t i;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    voice_play(core, 1000, 1, 0.3f, 1);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX - 1; i ++) {
        voice_play(core, i, 1, i == 40 ? 0.1f : 0.5f, 0);
    }

    audio_render(core, 0, 512);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, NULL), 0);

    voice_play(core, 1001, 1, 0.5f, 1);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 40);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);

    voice_play(core, 1002, 1, 0.5f, 1);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 1000);
    test_int(box_audio_core_refused(core), 0);
    box_audio_core_free(core);
}

void Voice_equal_priority_steals_quietest(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    box_audio_event_t e;
    uint32_t i;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        voice_play(core, i, 1, i == 20 || i == 30 ? 0.1f : i == 50 ? 0.2f : 0.5f, 3);
    }

    audio_render(core, 0, 512);
    voice_play(core, 100, 1, 0.5f, 3);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 20);
    voice_play(core, 101, 1, 0.5f, 3);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 30);
    voice_play(core, 102, 1, 0.5f, 7);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 50);
    test_int(e.value, 3);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    test_int(box_audio_core_refused(core), 0);
    box_audio_core_free(core);
}

/** A play below every voice is refused: its handle ends at once and nothing is stolen. A
 * scheduled play is refused on its frame, and so are a legato that starts a voice and a stream.
 * A priority past the largest is clamped to it. */
void Voice_refused(void) {
    // audio/voice
    box_audio_core_t *core = audio_new();
    uint8_t bytes[128];
    box_audio_writer_t w;
    box_audio_stream_status_t status;
    box_audio_event_t e;
    float steady;
    uint32_t i;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        voice_play(core, i, 1, i == 9 ? 0.1f : 0.5f, 5);
    }

    audio_render(core, 0, 512);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_END, NULL), 0);
    steady = audio_l[511];
    test_assert(steady > 0);

    voice_play(core, 100, 1, 0.5f, 4);
    test_int(voice_events_of(core, BOX_AUDIO_EVENT_VOICE_END, 100, &e), 1);
    test_int(e.value, 1);
    test_int(box_audio_core_refused(core), 1);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    audio_render(core, 512, 512);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, NULL), 0);
    for (i = 512; i < 1024; i ++) {
        test_assert(audio_l[i] == steady);
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_priority(&w, 101, 1, 0, 0.5f, 0, 1, 1, 0, 0,
        box_audio_core_clock(core) + 100));
    box_audio_core_apply(core, w.data, w.size);
    test_int(voice_events_of(core, BOX_AUDIO_EVENT_VOICE_END, 101, NULL), 0);
    audio_render(core, 1024, 256);
    test_int(voice_events_of(core, BOX_AUDIO_EVENT_VOICE_END, 101, &e), 1);
    test_int(e.value, 2);

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato_priority(&w, 102, 9999, 1, 0, 0.5f, 0, 1, 1, 0, 2,
        0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(voice_events_of(core, BOX_AUDIO_EVENT_VOICE_END, 102, &e), 1);
    test_int(e.value, 3);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stream(&w, 103, 0, 0, 1.0f, 1, AUDIO_RATE, 4));
    box_audio_core_apply(core, w.data, w.size);
    test_int(voice_events_of(core, BOX_AUDIO_EVENT_VOICE_END, 103, &e), 1);
    test_int(e.value, 4);
    box_audio_core_stream_status(core, 0, &status);
    test_int(status.open, 0);
    test_int(box_audio_core_refused(core), 4);

    voice_play(core, 104, 1, 0.5f, 1000);
    test_int(audio_events(core, BOX_AUDIO_EVENT_VOICE_STOLEN, &e), 1);
    test_uint(e.id, 9);
    test_int(box_audio_core_refused(core), 4);
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    box_audio_core_free(core);
}

/** Render a fixed workload of plays, steals, legatos, stops and scheduled plays at fixed
 * block boundaries, with the writers of a mode.
 *
 * @param core The core
 * @param mode 0 for the writers without a priority, 1 for priority 0, 2 for priority 9
 * @param left Receives the left samples
 * @param right Receives the right samples
 * @param frames Frames to render
 * @return Hash of the handles of the steals and ends, in order
 */
static uint64_t voice_workload(
    box_audio_core_t *core,
    int32_t mode,
    float *left,
    float *right,
    int32_t frames)
{
    static uint8_t bytes[4096];
    uint32_t priority = mode == 2 ? 9 : 0;
    box_audio_event_t events[64];
    box_audio_writer_t w;
    uint64_t result = 0xCBF29CE484222325ull, when;
    int32_t at = 0, block = 0, count, i;
    audio_sine(audio_sound_a, 4800, 220, 0.6f);
    for (i = 0; i < 3000; i ++) {
        audio_sound_b[i * 2] = 0.5f * (float)sin(i * 0.013) * (float)sin(i * 0.0021);
        audio_sound_b[i * 2 + 1] = 0.3f * (float)cos(i * 0.029);
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4800, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 3000, 2, 44100));
    while (at < frames) {
        int32_t n = 37 + (block * 53) % 200;
        n = n < frames - at ? n : frames - at;
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        for (i = 0; i < 9; i ++) {
            uint32_t voice = (uint32_t)(block * 9 + i + 1);
            uint32_t sound = 1 + (voice % 2), flags = voice % 3 ? BOX_AUDIO_PLAY_LOOP : 0;
            float volume = 0.05f + 0.01f * (float)(voice % 7);
            float pan = -1 + 0.13f * (float)(voice % 16);
            float pitch = 0.5f + 0.1f * (float)(voice % 13);
            when = voice % 5 == 0 ? box_audio_core_clock(core) + 20 + voice % 90 : 0;
            if (mode == 0 && voice % 4 == 0) {
                box_audio_write_legato_at(&w, voice, voice - 3, sound, 0, volume, pan, pitch,
                    flags, when);
            } else if (mode == 0) {
                box_audio_write_play(&w, voice, sound, 0, volume, pan, pitch, flags, when);
            } else if (voice % 4 == 0) {
                box_audio_write_legato_priority(&w, voice, voice - 3, sound, 0, volume, pan,
                    pitch, 1, flags, priority, when);
            } else {
                box_audio_write_play_priority(&w, voice, sound, 0, volume, pan, pitch, 1,
                    flags, priority, when);
            }
        }

        box_audio_write_stop(&w, (uint32_t)(block * 9 - 13), 0);
        box_audio_write_voice_set(&w, (uint32_t)(block * 9 - 5), BOX_AUDIO_PARAM_VOLUME,
            0.01f, 0);
        box_audio_core_apply(core, w.data, w.size);
        box_audio_core_render(core, left + at, right + at, n);
        while ((count = box_audio_core_read_events(core, events, 64)) > 0) {
            for (i = 0; i < count; i ++) {
                result ^= (uint64_t)events[i].type * 0x9E3779B97F4A7C15ull ^ events[i].id;
                result *= 0x100000001B3ull;
            }
        }

        at += n;
        block ++;
    }

    return result;
}

void Voice_priority_zero_identical(void) {
    // audio/voice
    static float l[3][AUDIO_FRAMES / 2], r[3][AUDIO_FRAMES / 2];
    uint64_t events[3];
    int32_t mode;
    for (mode = 0; mode < 3; mode ++) {
        box_audio_core_t *core = audio_new();
        events[mode] = voice_workload(core, mode, l[mode], r[mode], AUDIO_FRAMES / 2);
        test_int(box_audio_core_refused(core), 0);
        box_audio_core_free(core);
    }

    test_assert(audio_peak(l[0], 0, AUDIO_FRAMES / 2) > 0);
    for (mode = 1; mode < 3; mode ++) {
        test_assert(memcmp(l[0], l[mode], sizeof(l[0])) == 0);
        test_assert(memcmp(r[0], r[mode], sizeof(r[0])) == 0);
        test_assert(events[0] == events[mode]);
    }
}

void Voice_stream_never_stolen(void) {
    // audio/voice
    static float chunk[BOX_AUDIO_STREAM_CHUNK];
    box_audio_core_t *core = audio_new();
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_stream_status_t status;
    int32_t c, i;
    audio_fill(chunk, BOX_AUDIO_STREAM_CHUNK, 0.5f);
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_stream(&w, 500, 0, 0, 0.01f, 1, AUDIO_RATE,
        BOX_AUDIO_PRIORITY_MAX));
    box_audio_core_apply(core, w.data, w.size);
    for (c = 0; c < 8; c ++) {
        test_assert(box_audio_core_stream_chunk(core, 0, chunk, BOX_AUDIO_STREAM_CHUNK, false));
    }

    for (i = 0; i < 3 * BOX_AUDIO_VOICE_MAX; i ++) {
        voice_play(core, (uint32_t)i, 1, 0.5f, (uint32_t)(i % 3 == 0 ? 254 : i % 3));
        test_int(voice_events_of(core, BOX_AUDIO_EVENT_VOICE_STOLEN, 500, NULL), 0);
    }

    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    audio_render(core, 0, 4096);
    test_int(voice_events_of(core, BOX_AUDIO_EVENT_VOICE_END, 500, NULL), 0);
    box_audio_core_stream_status(core, 0, &status);
    test_int(status.open, 1);
    test_uint(status.voice, 500);
    test_int(status.starved, 0);
    box_audio_core_free(core);
}

/** Run a world in which a long quiet note of a track at a priority sounds while 100 loud one
 * shots play on a silent bus.
 *
 * @param priority Priority of the track
 * @return Level of the last frame of the output, the note while it sounds
 */
static float voice_track_under_oneshots(
    int32_t priority)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t note_sound = audio_dc(world, NULL, 0.05f, AUDIO_RATE * 6);
    ecs_entity_t shot_sound = audio_dc(world, NULL, 0.5f, AUDIO_RATE * 6);
    ecs_entity_t fx = audio_bus(world, "fx", 0, 0.0f);
    ecs_entity_t inst = ecs_new(world), clip = ecs_new(world), track = ecs_new(world), pl;
    const AudioStatus *status;
    NoteClip *nc;
    ClipPlacement *p;
    Track *tk;
    float result;
    int32_t i;
    ecs_set(world, shot_sound, AudioOutput, { .bus = fx });
    ecs_set(world, inst, Instrument, { .sound = note_sound, .root = 60,
        .velocity_curve = 1, .gate = true, .release = 0.005f });
    nc = ecs_ensure(world, clip, NoteClip);
    nc->length = 16;
    ecs_vec_append_t(NULL, &nc->notes, Note)[0] = (Note){
        .start = 0, .duration = 16, .pitch = 60, .velocity = 1 };
    ecs_modified(world, clip, NoteClip);
    tk = ecs_ensure(world, track, Track);
    test_int(tk->priority, 1);
    tk->instrument = inst;
    tk->priority = priority;
    ecs_modified(world, track, Track);
    pl = ecs_new_w_pair(world, EcsChildOf, track);
    p = ecs_ensure(world, pl, ClipPlacement);
    p->clip = clip;
    ecs_modified(world, pl, ClipPlacement);
    audio_run(world, 2);
    ecs_singleton_ensure(world, Transport)->playing = true;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 10);
    test_flt(audio_tap_l[audio_tap_count - 1], 0.05f);

    for (i = 0; i < 100; i ++) {
        test_assert(box_audio_play(world, shot_sound, 1.0f));
    }

    audio_run(world, 4);
    status = ecs_singleton_get(world, AudioStatus);
    test_int(status->voices, BOX_AUDIO_VOICE_MAX);
    test_int(status->voices_stolen, 100 + 1 - BOX_AUDIO_VOICE_MAX);
    test_int(status->voices_refused, 0);
    result = audio_tap_l[audio_tap_count - 1];
    ecs_fini(world);

    return result;
}

void Voice_track_outranks_oneshots(void) {
    // audio/voice
    test_flt(voice_track_under_oneshots(1), 0.05f);
    test_flt(voice_track_under_oneshots(0), 0.0f);
}

void Voice_refused_forgotten(void) {
    // audio/voice
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t note_sound = audio_dc(world, NULL, 0.01f, AUDIO_RATE * 6);
    ecs_entity_t shot_sound = audio_dc(world, NULL, 0.5f, AUDIO_RATE * 6);
    ecs_entity_t inst = ecs_new(world), clip = ecs_new(world), track = ecs_new(world), pl;
    const AudioStatus *status;
    NoteClip *nc;
    ClipPlacement *p;
    Track *tk;
    int32_t i;
    ecs_set(world, inst, Instrument, { .sound = note_sound, .root = 60,
        .velocity_curve = 1, .gate = true, .release = 0.005f });
    nc = ecs_ensure(world, clip, NoteClip);
    nc->length = 16;
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        ecs_vec_append_t(NULL, &nc->notes, Note)[0] = (Note){
            .start = 0, .duration = 16, .pitch = 60 + (float)(i % 12), .velocity = 1 };
    }

    ecs_modified(world, clip, NoteClip);
    tk = ecs_ensure(world, track, Track);
    tk->instrument = inst;
    ecs_modified(world, track, Track);
    pl = ecs_new_w_pair(world, EcsChildOf, track);
    p = ecs_ensure(world, pl, ClipPlacement);
    p->clip = clip;
    ecs_modified(world, pl, ClipPlacement);
    audio_run(world, 2);
    ecs_singleton_ensure(world, Transport)->playing = true;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 10);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, BOX_AUDIO_VOICE_MAX);
    test_int(ecs_vec_count(&state->oneshots), BOX_AUDIO_VOICE_MAX);

    for (i = 0; i < 10; i ++) {
        test_assert(box_audio_play(world, shot_sound, 1.0f));
    }

    audio_run(world, 2);
    status = ecs_singleton_get(world, AudioStatus);
    test_int(status->voices, BOX_AUDIO_VOICE_MAX);
    test_int(status->voices_stolen, 0);
    test_int(status->voices_refused, 10);
    test_int(ecs_vec_count(&state->oneshots), BOX_AUDIO_VOICE_MAX);
    ecs_fini(world);
}

void Voice_stream_outranks_oneshots(void) {
    // audio/voice
    ecs_world_t *world = audio_world();
    ecs_entity_t shot_sound = audio_dc(world, NULL, 0.5f, AUDIO_RATE * 6);
    ecs_entity_t fx = audio_bus(world, "fx", 0, 0.0f);
    ecs_entity_t e = ecs_new(world);
    const AudioStreamStatus *ss;
    AudioStream *s;
    float peak = 0;
    int32_t i;
    test_int(box_jobs_set_workers(world, 0), 0);
    ecs_set(world, shot_sound, AudioOutput, { .bus = fx });
    s = ecs_ensure(world, e, AudioStream);
    s->path = ecs_os_strdup("test/assets/audio/tone.ogg");
    s->volume = 0.02f;
    ecs_modified(world, e, AudioStream);
    for (i = 0; i < 10; i ++) {
        ss = ecs_get(world, e, AudioStreamStatus);
        if (ss && ss->state == AudioStreamPlaying) {
            break;
        }

        audio_run(world, 1);
    }

    ss = ecs_get(world, e, AudioStreamStatus);
    test_assert(ss != NULL);
    test_int(ss->state, AudioStreamPlaying);
    for (i = 0; i < 100; i ++) {
        test_assert(box_audio_play(world, shot_sound, 1.0f));
    }

    audio_run(world, 6);
    ss = ecs_get(world, e, AudioStreamStatus);
    test_int(ss->state, AudioStreamPlaying);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, BOX_AUDIO_VOICE_MAX);
    test_int(ecs_singleton_get(world, AudioStatus)->voices_stolen,
        100 + 1 - BOX_AUDIO_VOICE_MAX);
    for (i = audio_tap_count - AUDIO_FRAME; i < audio_tap_count; i ++) {
        peak = fabsf(audio_tap_l[i]) > peak ? fabsf(audio_tap_l[i]) : peak;
    }

    test_assert(peak > 0.005f);
    ecs_fini(world);
}
