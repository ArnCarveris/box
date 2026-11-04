/**
 * @file Schedule.c
 * @brief The clock of the core and commands with a when: they take effect on exactly their
 * frame whatever the render sizes, keep their order, apply late on a full schedule and are
 * cancelled by a stop that applies now.
 */

#include "world.h"
#include <transport.h>

/** Frames of the renders of the cases */
#define SCHEDULE_FRAMES 20000

/** Output of a second render, to compare with audio_l and audio_r */
static float schedule_l[SCHEDULE_FRAMES];
static float schedule_r[SCHEDULE_FRAMES];

/** Room for the commands of a batch */
static uint8_t schedule_bytes[64 * 1024];

/* The cases with a null transport call no flecs function that would set up the OS API they
 * allocate with. */
void Schedule_setup(void) {
    ecs_os_set_api_defaults();
}

/** Play a sound at a frame with a single command.
 *
 * @param core The core
 * @param voice Handle
 * @param sound Sound id
 * @param volume Volume
 * @param flags Flags
 * @param when Frame of the first sample, 0 for now
 */
static void schedule_play(
    box_audio_core_t *core,
    uint32_t voice,
    uint32_t sound,
    float volume,
    uint32_t flags,
    uint64_t when)
{
    box_audio_writer_t w;
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    test_assert(box_audio_write_play(&w, voice, sound, 0, volume, 0, 1, flags, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** Stop a voice at a frame with a single command.
 *
 * @param core The core
 * @param voice Handle
 * @param when Frame the fade starts at, 0 for now
 */
static void schedule_stop(
    box_audio_core_t *core,
    uint32_t voice,
    uint64_t when)
{
    box_audio_writer_t w;
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    test_assert(box_audio_write_stop(&w, voice, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** Set the volume of a voice at a frame with a single command.
 *
 * @param core The core
 * @param voice Handle
 * @param value Volume
 * @param when Frame the ramp starts at, 0 for now
 */
static void schedule_volume(
    box_audio_core_t *core,
    uint32_t voice,
    float value,
    uint64_t when)
{
    box_audio_writer_t w;
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    test_assert(box_audio_write_voice_set(&w, voice, BOX_AUDIO_PARAM_VOLUME, value, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** Render frames into buffers in renders of a size.
 *
 * @param core The core
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @param frames Frames
 * @param size Frames per render
 */
static void schedule_render(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames,
    int32_t size)
{
    int32_t at;
    for (at = 0; at < frames; at += size) {
        int32_t n = frames - at < size ? frames - at : size;
        box_audio_core_render(core, left + at, right + at, n);
    }
}

/** Find the first sample that is not 0.
 *
 * @param x Samples
 * @param frames Frames
 * @return Its index, frames when every one is 0
 */
static int32_t schedule_first(
    const float *x,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames && x[i] == 0; i ++) { }

    return i;
}

/** A core with a looping sound of 0.5 as sound 1.
 *
 * @return The core
 */
static box_audio_core_t* schedule_core(void)
{
    box_audio_core_t *core = audio_new();
    audio_fill(audio_sound_a, 4000, 0.5f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));

    return core;
}

/** A play at frame N has its first sample that is not 0 at N, with renders of 1, 128 and 1000
 * frames, and the clock counts every frame rendered. */
void Schedule_play_at_frame(void) {
    // audio/schedule
    int32_t sizes[] = { 1, 128, 1000 }, k;
    uint64_t at[] = { 1037, 128, 2999 };
    for (k = 0; k < 3; k ++) {
        int32_t j;
        for (j = 0; j < 3; j ++) {
            box_audio_core_t *core = schedule_core();
            test_assert(box_audio_core_clock(core) == 0);
            schedule_play(core, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, at[j]);
            test_int(box_audio_core_voice_count(core), 0);
            test_int(box_audio_core_scheduled(core), 1);
            schedule_render(core, audio_l, audio_r, 4000, sizes[k]);
            test_int(schedule_first(audio_l, 4000), (int32_t)at[j]);
            test_int(schedule_first(audio_r, 4000), (int32_t)at[j]);
            test_int(box_audio_core_voice_count(core), 1);
            test_int(box_audio_core_scheduled(core), 0);
            test_assert(box_audio_core_clock(core) == 4000);
            box_audio_core_free(core);
        }
    }
}

/** A stop starts its fade at its frame, with renders of 1, 128 and 1000 frames. */
void Schedule_stop_at_frame(void) {
    // audio/schedule
    int32_t sizes[] = { 1, 128, 1000 }, k, n = 2011;
    for (k = 0; k < 3; k ++) {
        box_audio_core_t *core = schedule_core();
        schedule_play(core, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, 0);
        schedule_stop(core, 1, (uint64_t)n);
        test_int(box_audio_core_voice_count(core), 1);
        schedule_render(core, audio_l, audio_r, 4000, sizes[k]);
        test_assert(audio_l[n - 1] == 0.5f);
        test_assert(audio_l[n] < 0.5f);
        test_assert(audio_l[n] > 0.49f);
        test_assert(audio_l[n + AUDIO_RAMP - 2] > 0);
        test_flt(audio_peak(audio_l, n + AUDIO_RAMP, 4000 - n - AUDIO_RAMP), 0);
        test_int(box_audio_core_voice_count(core), 0);
        box_audio_core_free(core);
    }
}

/** A volume set of a voice and of a bus start their ramps at their frames, with renders of 1,
 * 128 and 1000 frames. */
void Schedule_set_at_frame(void) {
    // audio/schedule
    int32_t sizes[] = { 1, 128, 1000 }, k, n = 1500, m = 3333;
    for (k = 0; k < 3; k ++) {
        box_audio_core_t *core = schedule_core();
        box_audio_writer_t w;
        schedule_play(core, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, 0);
        schedule_volume(core, 1, 0.5f, (uint64_t)n);
        box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
        test_assert(box_audio_write_bus_set(&w, 0, BOX_AUDIO_PARAM_VOLUME, 0, (uint64_t)m));
        box_audio_core_apply(core, w.data, w.size);
        test_int(box_audio_core_scheduled(core), 2);
        schedule_render(core, audio_l, audio_r, 5000, sizes[k]);
        test_assert(audio_l[n - 1] == 0.5f);
        test_assert(audio_l[n] < 0.5f);
        test_assert(audio_l[n] > 0.49f);
        test_assert(audio_l[n + AUDIO_RAMP] == 0.25f);
        test_assert(audio_l[m - 1] == 0.25f);
        test_assert(audio_l[m] < 0.25f);
        test_assert(audio_l[m] > 0.24f);
        test_flt(audio_peak(audio_l, m + AUDIO_RAMP, 5000 - m - AUDIO_RAMP), 0);
        box_audio_core_free(core);
    }
}

/** A when in the past, 0 or the frame that renders next applies at the start of the next
 * block. */
void Schedule_past_when(void) {
    // audio/schedule
    uint64_t whens[] = { 500, 0, 999, 1000 };
    int32_t k;
    for (k = 0; k < 4; k ++) {
        box_audio_core_t *core = schedule_core();
        schedule_render(core, audio_l, audio_r, 1000, 128);
        test_assert(box_audio_core_clock(core) == 1000);
        schedule_play(core, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, whens[k]);
        test_int(box_audio_core_scheduled(core), 0);
        test_int(box_audio_core_voice_count(core), 1);
        schedule_render(core, audio_l, audio_r, 1000, 128);
        test_int(schedule_first(audio_l, 1000), 0);
        box_audio_core_free(core);
    }
}

/** Commands for the same frame keep the order in which they arrived, in one batch or in several,
 * among many others at other frames. */
void Schedule_same_frame_order(void) {
    // audio/schedule
    box_audio_core_t *core = schedule_core();
    box_audio_writer_t w;
    int32_t i, n = 1000, m = 2000;

    /* A set before the play of its voice finds no voice, after it the volume changes */
    schedule_volume(core, 1, 0.5f, (uint64_t)n);
    schedule_play(core, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, (uint64_t)n);
    schedule_volume(core, 1, 0.25f, (uint64_t)n);
    schedule_render(core, audio_l, audio_r, 1500, 128);
    test_int(schedule_first(audio_l, 1500), n);
    test_assert(audio_l[n + AUDIO_RAMP] == 0.5f * 0.25f);

    /* The last of a hundred sets of the same frame wins, with sets of later frames between */
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    for (i = 0; i < 100; i ++) {
        test_assert(box_audio_write_voice_set(&w, 1, BOX_AUDIO_PARAM_VOLUME,
            (float)i / 100.0f, (uint64_t)m));
        test_assert(box_audio_write_voice_set(&w, 1, BOX_AUDIO_PARAM_PAN, 0,
            (uint64_t)(m + 100 + i * 7 % 13)));
    }

    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 200);
    schedule_render(core, audio_l, audio_r, 2000, 128);
    test_assert(audio_l[m - 1500 + 500] == 0.5f * ((float)99 / 100.0f));
    test_int(box_audio_core_scheduled(core), 0);
    box_audio_core_free(core);

    /* Two plays of the same handle at the same frame: the first one is the voice of the handle,
     * so a set reaches the voice of the first play */
    core = schedule_core();
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    test_assert(box_audio_write_play(&w, 1, 1, 0, 1, -1, 1, BOX_AUDIO_PLAY_LOOP, 300));
    test_assert(box_audio_write_play(&w, 1, 1, 0, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, 300));
    test_assert(box_audio_write_stop(&w, 1, 300));
    box_audio_core_apply(core, w.data, w.size);
    schedule_render(core, audio_l, audio_r, 1000, 128);
    test_int(box_audio_core_voice_count(core), 1);
    test_flt(audio_l[900], 0);
    test_assert(audio_r[900] > 0.5f);
    box_audio_core_free(core);
}

/** A command that finds the schedule full applies at once and reports LATE with its type. */
void Schedule_full_late(void) {
    // audio/schedule
    box_audio_core_t *core = schedule_core();
    box_audio_writer_t w;
    box_audio_event_t e;
    int32_t i;
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    for (i = 0; i < BOX_AUDIO_SCHEDULE_MAX; i ++) {
        if (w.size + 64 > w.capacity) {
            box_audio_core_apply(core, w.data, w.size);
            box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
        }

        test_assert(box_audio_write_voice_set(&w, 9999, BOX_AUDIO_PARAM_PAN, 0,
            (uint64_t)(100000 + i)));
    }

    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), BOX_AUDIO_SCHEDULE_MAX);
    test_int(audio_events(core, BOX_AUDIO_EVENT_LATE, NULL), 0);

    schedule_play(core, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, 1000);
    test_int(box_audio_core_scheduled(core), BOX_AUDIO_SCHEDULE_MAX);
    test_int(box_audio_core_voice_count(core), 1);
    test_int(audio_events(core, BOX_AUDIO_EVENT_LATE, &e), 1);
    test_int(e.id, BOX_AUDIO_CMD_PLAY);

    schedule_render(core, audio_l, audio_r, 1000, 128);
    test_assert(audio_l[0] > 0);
    test_assert(audio_l[999] == 0.5f);

    schedule_stop(core, 1, 1500);
    test_int(audio_events(core, BOX_AUDIO_EVENT_LATE, &e), 1);
    test_int(e.id, BOX_AUDIO_CMD_STOP);
    test_int(box_audio_core_voice_count(core), 0);
    test_int(box_audio_core_scheduled(core), BOX_AUDIO_SCHEDULE_MAX);

    schedule_render(core, audio_l, audio_r, 1000, 128);
    test_assert(audio_l[0] < 0.5f);
    test_assert(audio_l[0] > 0.49f);
    test_flt(audio_peak(audio_l, AUDIO_RAMP, 1000 - AUDIO_RAMP), 0);
    box_audio_core_free(core);
}

/** A stop that applies now drops the scheduled commands of its handle, so a scheduled play never
 * starts; a stop with a future when only stops; the commands of other handles stay. */
void Schedule_stop_cancels(void) {
    // audio/schedule
    box_audio_core_t *core = schedule_core();
    box_audio_writer_t w;
    schedule_play(core, 1, 1, 1, BOX_AUDIO_PLAY_LOOP, 2000);
    schedule_volume(core, 1, 0.5f, 2500);
    schedule_stop(core, 1, 3000);
    schedule_play(core, 2, 1, 1, BOX_AUDIO_PLAY_LOOP, 2000);
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    test_assert(box_audio_write_bus_set(&w, 0, BOX_AUDIO_PARAM_VOLUME, 1, 2500));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 5);

    schedule_render(core, audio_l, audio_r, 1000, 128);
    schedule_stop(core, 1, 0);
    test_int(box_audio_core_scheduled(core), 2);

    /* A stop of voice 2 at a future frame before its play only stops, which finds no voice */
    schedule_stop(core, 2, 1500);
    test_int(box_audio_core_scheduled(core), 3);
    schedule_render(core, audio_l, audio_r, 4000, 128);
    test_int(schedule_first(audio_l, 4000), 1000);
    test_int(box_audio_core_voice_count(core), 1);
    test_assert(audio_l[3999] == 0.5f);
    test_int(box_audio_core_scheduled(core), 0);

    /* A stop in the past cancels as well */
    schedule_play(core, 3, 1, 1, BOX_AUDIO_PLAY_LOOP, 8000);
    schedule_stop(core, 3, 10);
    test_int(box_audio_core_scheduled(core), 0);
    schedule_render(core, audio_l, audio_r, 5000, 128);
    test_int(box_audio_core_voice_count(core), 1);
    box_audio_core_free(core);
}

/** Write a batch of commands at many frames: plays, sets of voices and buses, and stops.
 *
 * @param core The core
 */
static void schedule_workload(
    box_audio_core_t *core)
{
    box_audio_route_t routes[] = { { 1, 0, 255 }, { 0, 255, 255 } };
    box_audio_writer_t w;
    uint32_t i;
    audio_sine(audio_sound_a, 4000, 220, 0.4f);
    audio_sine(audio_sound_b, 3000, 330, 0.3f);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 4000, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 3000, 1, 44100));
    audio_order(core, routes, 2);
    box_audio_writer_init(&w, schedule_bytes, sizeof(schedule_bytes));
    for (i = 1; i <= 120; i ++) {
        uint64_t at = (uint64_t)(i * 157 % 18000) + 1;
        test_assert(box_audio_write_play(&w, i, 1 + i % 2, i % 2, 0.1f + 0.01f * (float)(i % 9),
            -1 + 0.17f * (float)(i % 12), 0.6f + 0.07f * (float)(i % 11),
            i % 3 ? BOX_AUDIO_PLAY_LOOP : 0, at));
        test_assert(box_audio_write_voice_set(&w, i, BOX_AUDIO_PARAM_PITCH, 1.2f, at + 333));
        test_assert(box_audio_write_voice_set(&w, i, BOX_AUDIO_PARAM_VOLUME, 0.05f, at + 77));
        test_assert(box_audio_write_bus_set(&w, i % 2, BOX_AUDIO_PARAM_PAN,
            -0.5f + 0.1f * (float)(i % 10), at + 5));
        test_assert(box_audio_write_stop(&w, i, at + 1000 + i * 13 % 900));
    }

    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 600);
}

/** The output is bit identical whether the core renders one frame at a time, blocks of 128,
 * blocks of 1000 or one large block. */
void Schedule_bit_identical(void) {
    // audio/schedule
    int32_t sizes[] = { 128, 1, 1000, SCHEDULE_FRAMES }, k;
    for (k = 0; k < 4; k ++) {
        box_audio_core_t *core = audio_new();
        schedule_workload(core);
        schedule_render(core, k ? schedule_l : audio_l, k ? schedule_r : audio_r,
            SCHEDULE_FRAMES, sizes[k]);
        test_assert(box_audio_core_clock(core) == SCHEDULE_FRAMES);
        if (k) {
            test_assert(memcmp(audio_l, schedule_l, SCHEDULE_FRAMES * sizeof(float)) == 0);
            test_assert(memcmp(audio_r, schedule_r, SCHEDULE_FRAMES * sizeof(float)) == 0);
        } else {
            test_assert(audio_peak(audio_l, 0, SCHEDULE_FRAMES) > 0.1f);
        }

        box_audio_core_free(core);
    }
}

/** Frames the output of the null transport received */
static int32_t schedule_null_frames;

static void schedule_null_output(
    const float *left,
    const float *right,
    int32_t frames,
    void *ctx)
{
    (void)left;
    (void)right;
    (void)ctx;
    schedule_null_frames += frames;
}

/** The clock of the null transport equals the frames it rendered. */
void Schedule_null_clock(void) {
    // audio/schedule
    box_audio_transport_t *t = box_audio_transport_null_new(&(box_audio_null_desc_t){
        .sample_rate = AUDIO_RATE, .output = schedule_null_output });
    int32_t i;
    test_assert(t != NULL);
    schedule_null_frames = 0;
    test_assert(t->clock(t) == 0);
    for (i = 0; i < 100; i ++) {
        t->update(t, 1.0f / 60.0f + (float)(i % 3) * 0.001f);
        test_assert(t->clock(t) == (uint64_t)schedule_null_frames);
    }

    test_assert(schedule_null_frames > 79000);
    t->update(t, 0);
    test_assert(t->clock(t) == (uint64_t)schedule_null_frames);
    box_audio_transport_free(t);
}

/** AudioStatus and box_audio_module_clock report the clock of the output of a world. */
void Schedule_status_clock(void) {
    // audio/schedule
    ecs_world_t *world = audio_world();
    const AudioStatus *status;
    audio_run(world, 10);
    test_assert(box_audio_module_clock(world) == (uint64_t)audio_tap_count);
    status = ecs_singleton_get(world, AudioStatus);
    test_assert(status != NULL);
    test_assert(status->clock == audio_tap_count ||
        status->clock == audio_tap_count - AUDIO_FRAME);
    test_assert(status->clock > 0);
    ecs_fini(world);
}
