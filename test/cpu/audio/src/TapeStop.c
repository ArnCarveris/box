/**
 * @file TapeStop.c
 * @brief The tape stop kind of effect in an insert slot of a bus, in offline renders of a sine
 * against a render without it: the frequency halfway through a stop, the silence after it, a
 * start that ramps back and returns to the input, the curve, ramps that turn back, no clicks,
 * render sizes and the component.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, two seconds */
#define TS_MAX (AUDIO_RATE * 2)

/** Frame a stop starts at in a case */
#define TS_ON (AUDIO_RATE / 2)

/** Frequency and level of the sine of the cases */
#define TS_TONE 1000.0
#define TS_LEVEL 0.25f

/** Volume of the bus of the effect, so the limiter of the master never acts */
#define TS_VOLUME 0.1f

/** Output of the render with the effect, and of the one without it */
static float ts_l[TS_MAX], ts_r[TS_MAX];
static float ts_dry_l[TS_MAX], ts_dry_r[TS_MAX];

/** Output of a third render that a case compares */
static float ts_b_l[TS_MAX], ts_b_r[TS_MAX];

/** The input sound, a second of the sine that loops */
static float ts_sound[AUDIO_RATE];

/** The parameters of a tape stop that is not active.
 *
 * @param stop_time Seconds
 * @param start_time Seconds
 * @param curve -1..1
 * @return The parameters
 */
static box_audio_tape_stop_params_t ts_params(
    float stop_time,
    float start_time,
    float curve)
{
    return (box_audio_tape_stop_params_t){ 0, stop_time, start_time, curve };
}

/** Render the sine, looped on bus 1, through a tape stop in slot 0 whose active toggles at
 * frames, in renders of a size. Its memory is not clear: it never reads what it did not
 * write.
 *
 * @param p The parameters, active aside; NULL renders without the effect
 * @param toggles Frames at which active rises, falls, rises and so on, ascending
 * @param count Number of toggles
 * @param l Receives the left channel
 * @param r Receives the right channel
 * @param frames Frames to render
 * @param size Frames per render
 */
static void ts_run(
    const box_audio_tape_stop_params_t *p,
    const int32_t *toggles,
    int32_t count,
    float *l,
    float *r,
    int32_t frames,
    int32_t size)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_TAPE_STOP, AUDIO_RATE), i;
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0}, *memory;
    box_audio_core_t *core = box_audio_core_new(&desc);
    uint8_t bytes[2048];
    box_audio_writer_t w;
    test_assert(core != NULL);
    test_assert(floats > 0);
    test_assert(frames <= TS_MAX);
    memory = malloc((size_t)floats * sizeof(float));
    test_assert(memory != NULL);

    audio_fill(memory, floats, 0.75f);
    test_assert(box_audio_core_effect_memory(core, 0, memory, floats));
    audio_order(core, routes, 2);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, TS_VOLUME);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    if (p) {
        memcpy(params, p, sizeof(*p));
        params[0] = 0;
        test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_TAPE_STOP, 1, 0,
            BOX_AUDIO_BUS_NONE, params));
        for (i = 0; i < count; i ++) {
            params[0] = i % 2 ? 0.0f : 1.0f;
            test_assert(box_audio_write_effect_params(&w, 0, BOX_AUDIO_EFFECT_TAPE_STOP,
                params, (uint64_t)toggles[i]));
        }
    }

    box_audio_core_apply(core, w.data, w.size);
    audio_sine(ts_sound, AUDIO_RATE, TS_TONE, TS_LEVEL);
    test_assert(box_audio_core_sound(core, 1, ts_sound, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    for (i = 0; i < frames; i += size) {
        int32_t n = frames - i < size ? frames - i : size;
        box_audio_core_render(core, l + i, r + i, n);
    }

    box_audio_core_free(core);
    free(memory);
}

/** Render without the effect into ts_dry_l and ts_dry_r and with it into ts_l and ts_r.
 *
 * @param p The parameters
 * @param toggles Frames at which active rises, falls and so on
 * @param count Number of toggles
 */
static void ts_pair(
    const box_audio_tape_stop_params_t *p,
    const int32_t *toggles,
    int32_t count)
{
    ts_run(NULL, NULL, 0, ts_dry_l, ts_dry_r, TS_MAX, BOX_AUDIO_CORE_QUANTUM);
    ts_run(p, toggles, count, ts_l, ts_r, TS_MAX, BOX_AUDIO_CORE_QUANTUM);
}

/** Count the frames of a range where the output differs from the dry render.
 *
 * @param from First frame
 * @param to End frame
 * @return Frames that differ
 */
static int32_t ts_mismatch(
    int32_t from,
    int32_t to)
{
    int32_t i, result = 0;
    for (i = from; i < to; i ++) {
        result += ts_l[i] != ts_dry_l[i] || ts_r[i] != ts_dry_r[i];
    }

    return result;
}

/** Frequency of the output in a window of frames around a frame.
 *
 * @param x Samples
 * @param frame The middle of the window
 * @param half Frames either side
 * @return Hz
 */
static double ts_frequency(
    const float *x,
    int32_t frame,
    int32_t half)
{
    return audio_frequency(x, frame - half, frame + half);
}

/** A stop of 0.5 seconds halves the frequency of the sine halfway through, at a speed of 0.5,
 * at its level; until it starts the input passes unchanged. */
void TapeStop_halves_frequency(void) {
    // audio/tape_stop
    box_audio_tape_stop_params_t p = ts_params(0.5f, 0.2f, 0);
    int32_t toggles[] = { TS_ON }, half = AUDIO_RATE / 4;
    double f1, f2, f3;
    ts_pair(&p, toggles, 1);
    test_int(ts_mismatch(0, TS_ON), 0);
    f1 = ts_frequency(ts_l, TS_ON + half / 2, 480);
    f2 = ts_frequency(ts_l, TS_ON + half, 480);
    f3 = ts_frequency(ts_l, TS_ON + half + half / 2, 960);
    printf("a quarter through %.1f Hz, halfway %.1f Hz, three quarters %.1f Hz\n", f1, f2, f3);
    test_assert(fabs(f1 - 750) < 20);
    test_assert(fabs(f2 - 500) < 15);
    test_assert(fabs(f3 - 250) < 15);
    test_assert(audio_peak(ts_l, TS_ON + half - 480, TS_ON + half + 480) >
        0.9f * TS_LEVEL * TS_VOLUME);
}

/** After stop_time the output is silent until active falls, whatever comes in. */
void TapeStop_silent_after_stop(void) {
    // audio/tape_stop
    box_audio_tape_stop_params_t p = ts_params(0.5f, 0.2f, 0);
    int32_t toggles[] = { TS_ON }, end = TS_ON + AUDIO_RATE / 2;
    ts_pair(&p, toggles, 1);
    test_assert(audio_peak(ts_l, end, TS_MAX) == 0);
    test_assert(audio_peak(ts_r, end, TS_MAX) == 0);
    test_assert(audio_peak(ts_l, end - 4800, end - 2400) > 0);
}

/** A fall of active after a stop spins the sine back up over start_time, at half its
 * frequency halfway, then crossfades to the input and passes it unchanged: the input of the
 * stop is skipped. */
void TapeStop_restart(void) {
    // audio/tape_stop
    box_audio_tape_stop_params_t p = ts_params(0.5f, 0.2f, 0);
    int32_t off = TS_ON + AUDIO_RATE, start = AUDIO_RATE / 5;
    int32_t toggles[] = { TS_ON, off }, up = off + start;
    int32_t settled = up + (int32_t)(BOX_AUDIO_TAPE_STOP_CROSSFADE * AUDIO_RATE) + 2;
    double f1, f2;
    ts_pair(&p, toggles, 2);
    test_assert(audio_peak(ts_l, TS_ON + AUDIO_RATE / 2, off) == 0);
    f1 = ts_frequency(ts_l, off + start / 2, 480);
    f2 = ts_frequency(ts_l, settled + 2400, 1200);
    printf("halfway through the start %.1f Hz, after it %.1f Hz\n", f1, f2);
    test_assert(fabs(f1 - 500) < 20);
    test_assert(fabs(f2 - TS_TONE) < 5);
    test_int(ts_mismatch(settled, TS_MAX), 0);
}

/** A curve of 1 holds the speed halfway at 1 - 0.5^4, a curve of -1 drops it to
 * 1 - 0.5^0.25. */
void TapeStop_curve(void) {
    // audio/tape_stop
    box_audio_tape_stop_params_t hold = ts_params(0.5f, 0.2f, 1);
    box_audio_tape_stop_params_t drop = ts_params(0.5f, 0.2f, -1);
    int32_t toggles[] = { TS_ON }, half = AUDIO_RATE / 4;
    double f1, f2;
    ts_run(&hold, toggles, 1, ts_l, ts_r, TS_MAX, BOX_AUDIO_CORE_QUANTUM);
    f1 = ts_frequency(ts_l, TS_ON + half, 480);
    ts_run(&drop, toggles, 1, ts_l, ts_r, TS_MAX, BOX_AUDIO_CORE_QUANTUM);
    f2 = ts_frequency(ts_l, TS_ON + half, 1440);
    printf("halfway at a curve of 1 %.1f Hz, of -1 %.1f Hz\n", f1, f2);
    test_assert(fabs(f1 - 937.5) < 20);
    test_assert(fabs(f2 - 1000 * (1 - pow(0.5, 0.25))) < 15);
}

/** Neither the stop, the halt, the start, the crossfade after it nor ramps that turn back
 * halfway step further than the sine does. */
void TapeStop_no_click(void) {
    // audio/tape_stop
    box_audio_tape_stop_params_t cases[] = {
        ts_params(0.5f, 0.2f, 0), ts_params(0.3f, 0.05f, 0.7f), ts_params(1, 0.5f, -0.6f)
    };
    int32_t toggles[] = { TS_ON, TS_ON + 6000, TS_ON + 9000, TS_ON + 40000, TS_ON + 70000,
        TS_ON + 72000 };
    double natural = 2 * AUDIO_PI * TS_TONE / AUDIO_RATE * TS_LEVEL * TS_VOLUME;
    int32_t c;
    for (c = 0; c < 3; c ++) {
        float step;
        ts_run(&cases[c], toggles, 6, ts_l, ts_r, TS_MAX, BOX_AUDIO_CORE_QUANTUM);
        step = audio_max_step(ts_l, TS_ON / 2, TS_MAX);
        printf("case %d: largest step %.5f, the sine steps %.5f\n", c, (double)step, natural);
        test_assert(step < 1.5 * natural);
        test_assert(audio_max_step(ts_r, TS_ON / 2, TS_MAX) < 1.5 * natural);
    }
}

/** Renders of 1, 37, 128 and 1000 frames are bit identical, and two renders of one size. */
void TapeStop_render_sizes(void) {
    // audio/tape_stop
    box_audio_tape_stop_params_t p = ts_params(0.4f, 0.15f, 0.5f);
    int32_t toggles[] = { TS_ON, TS_ON + 7001, TS_ON + 9999, TS_ON + 40000 };
    int32_t sizes[] = { 1, 37, 128, 1000 }, s;
    uint64_t hash;
    ts_run(&p, toggles, 4, ts_b_l, ts_b_r, TS_MAX, BOX_AUDIO_CORE_QUANTUM);
    hash = audio_hash(ts_b_l, TS_MAX) ^ audio_hash(ts_b_r, TS_MAX);
    for (s = 0; s < 4; s ++) {
        ts_run(&p, toggles, 4, ts_l, ts_r, TS_MAX, sizes[s]);
        test_assert(memcmp(ts_l, ts_b_l, sizeof(ts_l)) == 0);
        test_assert(memcmp(ts_r, ts_b_r, sizeof(ts_r)) == 0);
    }

    ts_run(&p, toggles, 4, ts_l, ts_r, TS_MAX, BOX_AUDIO_CORE_QUANTUM);
    test_assert((audio_hash(ts_l, TS_MAX) ^ audio_hash(ts_r, TS_MAX)) == hash);
}

/** The component defaults, its kind and its params, and the memory of the kind. */
void TapeStop_component(void) {
    // audio/tape_stop
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    float params[BOX_AUDIO_EFFECT_PARAMS];
    const TapeStop *t;
    ecs_add(world, e, TapeStop);
    t = ecs_get(world, e, TapeStop);
    test_flt(t->active, 0);
    test_flt(t->stop_time, 0.5f);
    test_flt(t->start_time, 0.2f);
    test_flt(t->curve, 0);
    test_assert(ecs_has(world, e, AudioEffectState));
    ecs_set(world, e, TapeStop, { .active = 1, .stop_time = 2, .start_time = 0.1f,
        .curve = -0.5f });
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_TAPE_STOP);
    test_flt(params[0], 1);
    test_flt(params[1], 2);
    test_flt(params[2], 0.1f);
    test_flt(params[3], -0.5f);
    test_assert(box_audio_effect_floats(BOX_AUDIO_EFFECT_TAPE_STOP, AUDIO_RATE) >=
        2 * (int32_t)(BOX_AUDIO_TAPE_STOP_MAX * AUDIO_RATE));
    ecs_fini(world);
}
