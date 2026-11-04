/**
 * @file Robust.c
 * @brief The audio core with bad input: NaN, infinities, denormals, odd frame counts and bytes.
 */

#include "util.h"

/** Output of a second run */
static float robust_l[AUDIO_FRAMES];
static float robust_r[AUDIO_FRAMES];

/** Whether every sample of a range is finite and within -1..1.
 *
 * @param x Samples
 * @param frames Frames
 * @return Whether they are
 */
static bool robust_clean(
    const float *x,
    int32_t frames)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        result = result && isfinite(x[i]) && x[i] >= -1 && x[i] <= 1;
    }

    return result;
}

void Robust_nan_input(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    int32_t i;
    for (i = 0; i < 4000; i ++) {
        float values[] = { NAN, INFINITY, -INFINITY, 3e38f, -3e38f, 0.25f, 1e-40f };
        audio_sound_a[i] = values[i % 7];
    }

    audio_sine(audio_sound_b, 4000, 200, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 4000, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1.3f, 0);
    audio_play(core, 2, 2, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 8000);
    test_assert(robust_clean(audio_l, 8000));
    test_assert(robust_clean(audio_r, 8000));
    test_int(box_audio_core_voice_count(core), 1);
    audio_render(core, 8000, 8000);
    for (i = 12000; i < 16000; i ++) {
        test_assert(audio_l[i] == audio_sound_b[i % 4000]);
    }

    box_audio_core_free(core);
}

void Robust_nan_stereo(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    int32_t i;
    for (i = 0; i < 2000; i ++) {
        audio_sound_a[i * 2] = i % 2 ? NAN : 0.5f;
        audio_sound_a[i * 2 + 1] = i % 3 ? -INFINITY : 0.5f;
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 2000, 2, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0.2f, 0.7f, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 8000);
    test_assert(robust_clean(audio_l, 8000));
    test_assert(robust_clean(audio_r, 8000));
    box_audio_core_free(core);
}

void Robust_nan_commands(void) {
    // audio/robust
    box_audio_route_t routes[] = { { 1, 0, 0 }, { 0, 255, 255 } };
    box_audio_core_t *core = audio_new();
    audio_sine(audio_sound_a, 4000, 200, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, NAN));
    audio_order(core, routes, 2);
    audio_play(core, 1, 1, 1, NAN, INFINITY, NAN, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 1, 1, INFINITY, -INFINITY, INFINITY, BOX_AUDIO_PLAY_LOOP);
    audio_voice_set(core, 2, BOX_AUDIO_PARAM_BUS, NAN);
    audio_voice_set(core, 2, BOX_AUDIO_PARAM_PITCH, -INFINITY);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, NAN);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, NAN);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, INFINITY);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_MUTE, NAN);
    audio_bus_set(core, 0, BOX_AUDIO_PARAM_VOLUME, INFINITY);
    audio_render(core, 0, 8000);
    test_assert(robust_clean(audio_l, 8000));
    test_assert(robust_clean(audio_r, 8000));
    box_audio_core_free(core);
}

void Robust_denormal_input(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    audio_fill(audio_sound_a, 4000, 1e-40f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 8000);
    test_flt(audio_peak(audio_l, 0, 8000), 0);
    test_flt(audio_peak(audio_r, 0, 8000), 0);
    box_audio_core_free(core);
}

void Robust_huge_chain(void) {
    // audio/robust
    box_audio_route_t routes[BOX_AUDIO_BUS_MAX];
    box_audio_core_t *core = audio_new();
    int32_t i;
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        routes[i].bus = (uint8_t)(BOX_AUDIO_BUS_MAX - 1 - i);
        routes[i].output = (uint8_t)(i < BOX_AUDIO_BUS_MAX - 1 ? BOX_AUDIO_BUS_MAX - 2 - i :
            BOX_AUDIO_BUS_NONE);
        routes[i].send = (uint8_t)(i < BOX_AUDIO_BUS_MAX - 2 ? BOX_AUDIO_BUS_MAX - 2 - i :
            BOX_AUDIO_BUS_NONE);
        routes[i].unused = 0;
        audio_bus_set(core, (uint32_t)i, BOX_AUDIO_PARAM_VOLUME, BOX_AUDIO_VOLUME_MAX);
        audio_bus_set(core, (uint32_t)i, BOX_AUDIO_PARAM_SEND, BOX_AUDIO_VOLUME_MAX);
    }

    audio_fill(audio_sound_a, 4000, 1);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    audio_order(core, routes, BOX_AUDIO_BUS_MAX);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 0);
    audio_play(core, 1, 1, BOX_AUDIO_BUS_MAX - 1, BOX_AUDIO_VOLUME_MAX, 0, 1,
        BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 8000);
    test_assert(robust_clean(audio_l, 8000));
    test_assert(robust_clean(audio_r, 8000));
    box_audio_core_free(core);
}

void Robust_zero_frames(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    float l[4] = { 7, 7, 7, 7 }, r[4] = { 7, 7, 7, 7 };
    audio_fill(audio_sound_a, 4000, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    box_audio_core_render(core, l, r, 0);
    box_audio_core_render(core, l, r, -5);
    box_audio_core_render(core, NULL, r, 4);
    box_audio_core_render(core, l, NULL, 4);
    box_audio_core_render(NULL, l, r, 4);
    box_audio_core_apply(core, NULL, 100);
    box_audio_core_apply(core, l, 0);
    box_audio_core_apply(core, l, -3);
    box_audio_core_apply(NULL, l, 16);
    test_flt(l[0], 7);
    test_flt(r[3], 7);
    test_int(box_audio_core_read_events(core, NULL, 4), 0);
    test_int(box_audio_core_read_events(NULL, NULL, 4), 0);
    box_audio_core_render(core, l, r, 4);
    test_assert(l[0] > 0 && l[0] < 0.5f);
    box_audio_core_free(core);
    box_audio_core_free(NULL);
}

void Robust_odd_frames(void) {
    // audio/robust
    int32_t counts[] = { 1, 3, 127, 129, 1000, 7, 255, 4097, 2, 64 }, at = 0, i = 0;
    box_audio_core_t *core = audio_new();
    audio_sine(audio_sound_a, 4800, 220, 0.6f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4800, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 0.8f, 0.3f, 1.37f, BOX_AUDIO_PLAY_LOOP);
    audio_render(core, 0, 20000);
    box_audio_core_free(core);

    core = audio_new();
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4800, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 0, 0.8f, 0.3f, 1.37f, BOX_AUDIO_PLAY_LOOP);
    while (at < 20000) {
        int32_t n = counts[i % 10] < 20000 - at ? counts[i % 10] : 20000 - at;
        box_audio_core_render(core, robust_l + at, robust_r + at, n);
        at += n;
        i ++;
    }

    test_assert(memcmp(audio_l, robust_l, 20000 * sizeof(float)) == 0);
    test_assert(memcmp(audio_r, robust_r, 20000 * sizeof(float)) == 0);
    box_audio_core_free(core);
}

void Robust_one_frame_at_a_time(void) {
    // audio/robust
    box_audio_route_t routes[] = { { 1, 0, 2 }, { 2, 0, 255 }, { 0, 255, 255 } };
    box_audio_core_t *core;
    int32_t pass, i;
    audio_sine(audio_sound_a, 4800, 220, 0.6f);
    for (i = 0; i < 3000; i ++) {
        audio_sound_b[i * 2] = 0.4f * (float)sin(i * 0.011);
        audio_sound_b[i * 2 + 1] = 0.4f * (float)sin(i * 0.017);
    }

    for (pass = 0; pass < 2; pass ++) {
        core = audio_new();
        test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4800, 1, AUDIO_RATE));
        test_assert(box_audio_core_sound(core, 2, audio_sound_b, 3000, 2, 44100));
        audio_order(core, routes, 3);
        audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, 0.4f);
        audio_bus_set(core, 2, BOX_AUDIO_PARAM_PAN, 0.6f);
        audio_play(core, 1, 1, 1, 0.8f, -0.3f, 1.37f, BOX_AUDIO_PLAY_LOOP);
        audio_play(core, 2, 2, 2, 0.9f, 0.1f, 0.77f, 0);
        audio_play(core, 3, 2, 1, 4, 0, 1, BOX_AUDIO_PLAY_LOOP);
        if (pass == 0) {
            box_audio_core_render(core, audio_l, audio_r, 9000);
        } else {
            for (i = 0; i < 9000; i ++) {
                box_audio_core_render(core, robust_l + i, robust_r + i, 1);
            }
        }

        box_audio_core_free(core);
    }

    test_assert(memcmp(audio_l, robust_l, 9000 * sizeof(float)) == 0);
    test_assert(memcmp(audio_r, robust_r, 9000 * sizeof(float)) == 0);
}

void Robust_unknown_commands(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    uint8_t bytes[256];
    uint8_t unknown[12] = { 0xE7, 0x03, 12, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t short_play[8] = { BOX_AUDIO_CMD_PLAY, 0, 8, 0, 1, 0, 0, 0 };
    uint8_t truncated[8] = { BOX_AUDIO_CMD_STOP, 0, 200, 0, 1, 0, 0, 0 };
    box_audio_writer_t w;
    box_audio_event_t events[16];
    int32_t n, i, ignored = 0;
    bool unknown_seen = false;
    audio_fill(audio_sound_a, 4000, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_bytes(&w, unknown, sizeof(unknown)));
    test_assert(box_audio_write_bytes(&w, short_play, sizeof(short_play)));
    test_assert(box_audio_write_play(&w, 1, 1, 0, 1, 0, 1, BOX_AUDIO_PLAY_LOOP, 0));
    test_assert(box_audio_write_bus_set(&w, 0, (box_audio_param_t)77, 1, 0));
    test_assert(box_audio_write_voice_set(&w, 1, (box_audio_param_t)77, 1, 0));
    test_assert(box_audio_write_bytes(&w, truncated, sizeof(truncated)));
    box_audio_core_apply(core, w.data, w.size);
    box_audio_core_apply(core, bytes, 3);
    n = box_audio_core_read_events(core, events, 16);
    for (i = 0; i < n; i ++) {
        if (events[i].type == BOX_AUDIO_EVENT_IGNORED) {
            ignored ++;
            unknown_seen = unknown_seen || events[i].id == 999;
        }
    }

    test_int(ignored, 5);
    test_bool(unknown_seen, true);
    test_int(box_audio_core_voice_count(core), 1);
    audio_render(core, 0, 2048);
    test_assert(audio_l[2047] == 0.5f);
    box_audio_core_free(core);
}

/* A LOST event in the last slot counts the events dropped, a second overflow counts from 0 again
 * and without an overflow there is no LOST event. */
void Robust_events_overflow(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    box_audio_event_t events[BOX_AUDIO_EVENT_MAX + 1];
    int32_t i;
    for (i = 0; i < BOX_AUDIO_EVENT_MAX * 2; i ++) {
        audio_play(core, 1, 999, 0, 1, 0, 1, 0);
    }

    test_int(box_audio_core_events_lost(core), BOX_AUDIO_EVENT_MAX + 1);
    test_int(box_audio_core_read_events(core, events, BOX_AUDIO_EVENT_MAX + 1),
        BOX_AUDIO_EVENT_MAX);
    for (i = 0; i < BOX_AUDIO_EVENT_MAX - 1; i ++) {
        test_int(events[i].type, BOX_AUDIO_EVENT_IGNORED);
    }

    test_int(events[BOX_AUDIO_EVENT_MAX - 1].type, BOX_AUDIO_EVENT_LOST);
    test_int(events[BOX_AUDIO_EVENT_MAX - 1].value, BOX_AUDIO_EVENT_MAX + 1);
    test_int(box_audio_core_events_lost(core), 0);
    test_int(box_audio_core_read_events(core, events, BOX_AUDIO_EVENT_MAX), 0);

    for (i = 0; i < BOX_AUDIO_EVENT_MAX + 10; i ++) {
        audio_play(core, 1, 999, 0, 1, 0, 1, 0);
    }

    test_int(box_audio_core_read_events(core, events, BOX_AUDIO_EVENT_MAX + 1),
        BOX_AUDIO_EVENT_MAX);
    test_int(events[BOX_AUDIO_EVENT_MAX - 1].type, BOX_AUDIO_EVENT_LOST);
    test_int(events[BOX_AUDIO_EVENT_MAX - 1].value, 11);
    test_int(box_audio_core_events_lost(core), 0);

    for (i = 0; i < BOX_AUDIO_EVENT_MAX - 1; i ++) {
        audio_play(core, 1, 999, 0, 1, 0, 1, 0);
    }

    test_int(audio_events(core, BOX_AUDIO_EVENT_LOST, NULL), 0);
    test_int(box_audio_core_read_events(core, events, BOX_AUDIO_EVENT_MAX), 0);
    audio_render(core, 0, 256);
    test_flt(audio_peak(audio_l, 0, 256), 0);
    box_audio_core_free(core);
}

/** Bytes of a batch of 300 plays and a release */
#define ROBUST_BURST_BYTES 32768

/** Plays of a burst, at the voice cap each steals a voice and most end one */
#define ROBUST_BURST 300

static uint8_t robust_burst[ROBUST_BURST_BYTES];

/** Read every event of the core in reads of at most 64, and look for a SOUND_FREE.
 *
 * @param core The core
 * @param sound Sound whose SOUND_FREE is looked for
 * @param lost Receives the sum of the values of the LOST events
 * @return Number of SOUND_FREE events of the sound
 */
static int32_t robust_drain(
    box_audio_core_t *core,
    uint32_t sound,
    int32_t *lost)
{
    box_audio_event_t events[64];
    int32_t count, i, freed = 0, total = 0;
    *lost = 0;
    do {
        count = box_audio_core_read_events(core, events, 64);
        for (i = 0; i < count; i ++) {
            if (events[i].type == BOX_AUDIO_EVENT_SOUND_FREE && events[i].id == sound) {
                freed ++;
            } else if (events[i].type == BOX_AUDIO_EVENT_LOST) {
                *lost += events[i].value;
            }
        }

        total += count;
        test_assert(total <= BOX_AUDIO_EVENT_MAX * 4);
    } while (count);

    return freed;
}

/* Steals and ends of the plays fill the ring before the release of the unused sound, whose id
 * stays taken until the transport learns it is free. */
void Robust_release_after_burst(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    box_audio_writer_t w;
    int32_t i, lost;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    audio_fill(audio_sound_b, 100, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 100, 1, AUDIO_RATE));

    box_audio_writer_init(&w, robust_burst, sizeof(robust_burst));
    for (i = 0; i < ROBUST_BURST; i ++) {
        test_assert(box_audio_write_play(&w, (uint32_t)i, 1, 0, 0.5f, 0, 1, 0, 0));
    }

    test_assert(box_audio_write_release(&w, 2));
    box_audio_core_apply(core, w.data, w.size);
    test_assert(box_audio_core_events_lost(core) > 0);

    test_assert(!box_audio_core_sound(core, 2, audio_sound_b, 100, 1, AUDIO_RATE));
    test_int(robust_drain(core, 2, &lost), 1);
    test_assert(lost > 0);
    test_int(box_audio_core_events_lost(core), 0);
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 100, 1, AUDIO_RATE));
    test_int(box_audio_core_voice_count(core), BOX_AUDIO_VOICE_MAX);
    box_audio_core_free(core);
}

/* The voice of sound 2 is the loudest, so the cap steals the plays of the burst instead. It ends
 * in the render while the ring is full, so its SOUND_FREE comes from the end of the source. */
void Robust_release_playing_after_burst(void) {
    // audio/robust
    box_audio_core_t *core = audio_new();
    box_audio_writer_t w;
    int32_t i, lost;
    audio_fill(audio_sound_a, AUDIO_FRAMES, 0.01f);
    audio_fill(audio_sound_b, 100, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_FRAMES, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 100, 1, AUDIO_RATE));

    box_audio_writer_init(&w, robust_burst, sizeof(robust_burst));
    test_assert(box_audio_write_play(&w, 10000, 2, 0, 1, 0, 1, 0, 0));
    test_assert(box_audio_write_release(&w, 2));
    for (i = 0; i < ROBUST_BURST; i ++) {
        test_assert(box_audio_write_play(&w, (uint32_t)i, 1, 0, 0.5f, 0, 1, 0, 0));
    }

    box_audio_core_apply(core, w.data, w.size);
    test_assert(box_audio_core_events_lost(core) > 0);
    test_assert(!box_audio_core_sound(core, 2, audio_sound_b, 100, 1, AUDIO_RATE));

    audio_render(core, 0, 256);
    test_assert(!box_audio_core_sound(core, 2, audio_sound_b, 100, 1, AUDIO_RATE));
    test_int(robust_drain(core, 2, &lost), 1);
    test_assert(lost > 0);
    test_int(box_audio_core_events_lost(core), 0);
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 100, 1, AUDIO_RATE));
    box_audio_core_free(core);
}
