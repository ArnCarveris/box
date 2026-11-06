/**
 * @file BeatRepeat.c
 * @brief The beat repeat kind of effect in an insert slot of a bus, in offline renders against
 * a render of the same input without it: the slice from a grid point repeated sample for
 * sample, a grid of 0, the wait for the next grid point, the tempo of the beat clock, a
 * reversed slice, a pitch of -12, a decay of 0.5, the gate, the mix, crossfades that do not
 * click, render sizes, the component, a scene and an automation lane in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, two seconds */
#define BR_MAX (AUDIO_RATE * 2)

/** Frames of a sixteenth at 120 bpm, the default slice */
#define BR_SLICE 6000

/** Frame of beat 2 at 120 bpm */
#define BR_BEAT2 48000

/** Frames around the start of a repeat that its crossfade takes, 2 ms and a margin */
#define BR_FADE 100

/** A frame after the ramp of the volume of the bus, before any case rises */
#define BR_ARMED (AUDIO_RATE / 2)

/** Volume of the bus of the effect, so the limiter of the master never acts */
#define BR_VOLUME 0.1f

/** Output of the render with the effect, and of the one without it */
static float br_l[BR_MAX], br_r[BR_MAX];
static float br_dry_l[BR_MAX], br_dry_r[BR_MAX];

/** Output of a third render that a case compares */
static float br_b_l[BR_MAX], br_b_r[BR_MAX];

/** The input sound, a second that loops */
static float br_sound[AUDIO_RATE];

/** Frames a world case keeps of the tap */
#define BR_WORLD (AUDIO_RATE * 3)

/** The tap of the world with the lane, and of the one without it */
static float br_world_wet[BR_WORLD], br_world_dry[BR_WORLD];

/** A scheduled change of active */
typedef struct br_edge_t {
    int64_t frame;  /**< Frame it applies at, -1 for none */
    float active;   /**< The value */
} br_edge_t;

/** The parameters of a beat repeat that is not active.
 *
 * @param slice Beats
 * @param grid Beats, negative for the slice
 * @param decay Gain per repeat
 * @param pitch Semitones per repeat
 * @param reverse 0 or 1
 * @param gate Share that sounds
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_beat_repeat_params_t br_params(
    float slice,
    float grid,
    float decay,
    float pitch,
    float reverse,
    float gate,
    float mix)
{
    return (box_audio_beat_repeat_params_t){ 0, slice, grid, decay, pitch, reverse, gate,
        mix };
}

/** The default parameters: a sixteenth on its own grid, no decay, pitch or reverse.
 *
 * @return The parameters
 */
static box_audio_beat_repeat_params_t br_default(void)
{
    return br_params(0.25f, -1, 1, 0, 0, 1, 1);
}

/** Fill br_sound with two sines whose periods do not divide a slice, or with one sine.
 *
 * @param frequency Hz of a single sine, 0 for the two sines
 */
static void br_fill(
    double frequency)
{
    int32_t i;
    for (i = 0; i < AUDIO_RATE; i ++) {
        double t = (double)i / AUDIO_RATE;
        br_sound[i] = frequency > 0 ? (float)(0.25 * sin(2 * AUDIO_PI * frequency * t)) :
            (float)(0.3 * sin(2 * AUDIO_PI * 331.3 * t) +
            0.2 * sin(2 * AUDIO_PI * 1234.5 * t + 0.4));
    }
}

/** Render br_sound, looped on bus 1, through a beat repeat in slot 0 whose active changes at
 * up to two frames, in renders of a size. Its memory is not clear: it never reads what it did
 * not write.
 *
 * @param p The parameters, active aside; NULL renders without the effect
 * @param edges The changes of active, two of them, frame -1 for none
 * @param tempo Beats per second of the beat clock from frame 0 on, 0 for 120 bpm
 * @param l Receives the left channel
 * @param r Receives the right channel
 * @param frames Frames to render
 * @param size Frames per render
 */
static void br_run(
    const box_audio_beat_repeat_params_t *p,
    const br_edge_t *edges,
    float tempo,
    float *l,
    float *r,
    int32_t frames,
    int32_t size)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_BEAT_REPEAT, AUDIO_RATE), i;
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0}, *memory;
    box_audio_core_t *core = box_audio_core_new(&desc);
    uint8_t bytes[512];
    box_audio_writer_t w;
    test_assert(core != NULL);
    test_assert(floats > 0);
    test_assert(frames <= BR_MAX);
    memory = malloc((size_t)floats * sizeof(float));
    test_assert(memory != NULL);

    audio_fill(memory, floats, 0.75f);
    test_assert(box_audio_core_effect_memory(core, 0, memory, floats));
    audio_order(core, routes, 2);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, BR_VOLUME);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    if (tempo > 0) {
        test_assert(box_audio_write_beat(&w, 0, tempo, 0));
    }

    if (p) {
        memcpy(params, p, sizeof(*p));
        params[0] = 0;
        test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_BEAT_REPEAT, 1, 0,
            BOX_AUDIO_BUS_NONE, params));
        for (i = 0; edges && i < 2; i ++) {
            if (edges[i].frame >= 0) {
                params[0] = edges[i].active;
                test_assert(box_audio_write_effect_params(&w, 0, BOX_AUDIO_EFFECT_BEAT_REPEAT,
                    params, (uint64_t)edges[i].frame));
            }
        }
    }

    box_audio_core_apply(core, w.data, w.size);
    test_assert(box_audio_core_sound(core, 1, br_sound, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    for (i = 0; i < frames; i += size) {
        int32_t n = frames - i < size ? frames - i : size;
        box_audio_core_render(core, l + i, r + i, n);
    }

    box_audio_core_free(core);
    free(memory);
}

/** Render the input without the effect into br_dry_l and br_dry_r, and with it into br_l and
 * br_r, in quanta of the web.
 *
 * @param p The parameters
 * @param on Frame active rises at
 * @param off Frame it falls at, -1 for never
 * @param frames Frames to render
 */
static void br_pair(
    const box_audio_beat_repeat_params_t *p,
    int64_t on,
    int64_t off,
    int32_t frames)
{
    br_edge_t edges[2] = { { on, 1 }, { off, 0 } };
    br_run(NULL, NULL, 0, br_dry_l, br_dry_r, frames, BOX_AUDIO_CORE_QUANTUM);
    br_run(p, edges, 0, br_l, br_r, frames, BOX_AUDIO_CORE_QUANTUM);
}

/** Count the frames of a range of the output that differ from frames of the dry render: frame
 * at + j of the output against frame from + j of the dry one, or from + back - j with a back
 * of at least 0, for a reversed read.
 *
 * @param at First frame of the output
 * @param from First frame of the dry render
 * @param back Frames back of the first frame of a reversed read, -1 for a forward one
 * @param count Frames
 * @param gain Gain of the output against the dry render
 * @return Frames that differ by more than a rounding of the gain
 */
static int32_t br_mismatch(
    int32_t at,
    int32_t from,
    int32_t back,
    int32_t count,
    float gain)
{
    int32_t j, result = 0;
    for (j = 0; j < count; j ++) {
        int32_t d = back >= 0 ? from + back - j : from + j;
        double el = (double)br_l[at + j] - (double)br_dry_l[d] * gain;
        double er = (double)br_r[at + j] - (double)br_dry_r[d] * gain;
        double tol = gain == 1.0f ? 0.0 : 1e-7;
        if (fabs(el) > tol || fabs(er) > tol) {
            if (!result) {
                printf("frame %d: %.9f against dry frame %d: %.9f\n", at + j,
                    (double)br_l[at + j], d, (double)br_dry_l[d]);
            }

            result ++;
        }
    }

    return result;
}

/** RMS of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param count Frames
 * @return The RMS
 */
static double br_rms(
    const float *x,
    int32_t from,
    int32_t count)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        sum += (double)x[from + i] * (double)x[from + i];
    }

    return sqrt(sum / count);
}

/** Activated before beat 2, the effect captures from beat 2, the next grid point of its
 * sixteenth, and repeats the 6000 frames from there sample for sample away from the
 * crossfades. The first repeat is the input itself; after the fall the input returns at once.
 */
void BeatRepeat_repeats_slice(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_default();
    int32_t off = BR_BEAT2 + 4 * BR_SLICE + 3000, n, j, differ = 0;
    br_fill(0);
    br_pair(&p, 47000, off, BR_MAX);
    test_int(br_mismatch(0, 0, -1, BR_BEAT2 + BR_SLICE + 1, 1), 0);
    for (j = 0; j < BR_SLICE; j ++) {
        differ += fabsf(br_dry_l[BR_BEAT2 + BR_SLICE + j] - br_dry_l[BR_BEAT2 + j]) > 0.001f;
    }

    test_assert(differ > 1000);
    for (n = 1; n < 4; n ++) {
        test_int(br_mismatch(BR_BEAT2 + n * BR_SLICE + BR_FADE, BR_BEAT2 + BR_FADE, -1,
            BR_SLICE - BR_FADE, 1), 0);
    }

    test_int(br_mismatch(BR_BEAT2 + 4 * BR_SLICE + BR_FADE, BR_BEAT2 + BR_FADE, -1,
        3000 - BR_FADE, 1), 0);
    test_int(br_mismatch(off + BR_FADE, off + BR_FADE, -1, BR_MAX - off - BR_FADE, 1), 0);
}

/** A grid of 0 captures at the frame active rises, off the beat. */
void BeatRepeat_grid_at_once(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_params(0.25f, 0, 1, 0, 0, 1, 1);
    int32_t on = 47123, n;
    br_fill(0);
    br_pair(&p, on, -1, BR_MAX);
    test_int(br_mismatch(0, 0, -1, on + BR_SLICE + 1, 1), 0);
    for (n = 1; n < 4; n ++) {
        test_int(br_mismatch(on + n * BR_SLICE + BR_FADE, on + BR_FADE, -1,
            BR_SLICE - BR_FADE, 1), 0);
    }
}

/** A rise just after beat 2 waits for beat 2.25, and a grid of half a beat for beat 2.5 with a
 * slice of a sixteenth. */
void BeatRepeat_waits_for_grid(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_default();
    int32_t at = BR_BEAT2 + BR_SLICE, n;
    br_fill(0);
    br_pair(&p, BR_BEAT2 + 1000, -1, BR_MAX);
    test_int(br_mismatch(0, 0, -1, at + BR_SLICE + 1, 1), 0);
    for (n = 1; n < 3; n ++) {
        test_int(br_mismatch(at + n * BR_SLICE + BR_FADE, at + BR_FADE, -1,
            BR_SLICE - BR_FADE, 1), 0);
    }

    p = br_params(0.25f, 0.5f, 1, 0, 0, 1, 1);
    at = BR_BEAT2 + 2 * BR_SLICE;
    br_pair(&p, BR_BEAT2 + 1000, -1, BR_MAX);
    test_int(br_mismatch(0, 0, -1, at + BR_SLICE + 1, 1), 0);
    for (n = 1; n < 3; n ++) {
        test_int(br_mismatch(at + n * BR_SLICE + BR_FADE, at + BR_FADE, -1,
            BR_SLICE - BR_FADE, 1), 0);
    }
}

/** At 150 bpm from a BEAT command the slice is 0.1 seconds and beat 2 at frame 38400, not
 * the slice of 120 bpm. */
void BeatRepeat_tempo(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_default();
    br_edge_t edges[2] = { { 37000, 1 }, { -1, 0 } };
    int32_t at = 38400, slice = 4800, n, j, bad;
    br_fill(0);
    br_run(NULL, NULL, 2.5f, br_dry_l, br_dry_r, BR_MAX, BOX_AUDIO_CORE_QUANTUM);
    br_run(&p, edges, 2.5f, br_l, br_r, BR_MAX, BOX_AUDIO_CORE_QUANTUM);
    test_int(br_mismatch(0, 0, -1, at + slice + 1, 1), 0);
    for (n = 1; n < 5; n ++) {
        test_int(br_mismatch(at + n * slice + BR_FADE, at + BR_FADE, -1, slice - BR_FADE, 1),
            0);
    }

    bad = 0;
    for (j = BR_FADE; j < BR_SLICE - BR_FADE; j ++) {
        bad += br_l[at + BR_SLICE + j] != br_dry_l[at + j];
    }

    test_assert(bad > 1000);
}

/** Reverse plays the repeats after the first from the end of the slice back to its start,
 * not forward. */
void BeatRepeat_reverse(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_params(0.25f, -1, 1, 0, 1, 1, 1);
    int32_t n;
    br_fill(0);
    br_pair(&p, 47000, -1, BR_MAX);
    test_int(br_mismatch(0, 0, -1, BR_BEAT2 + BR_SLICE + 1, 1), 0);
    for (n = 1; n < 4; n ++) {
        test_int(br_mismatch(BR_BEAT2 + n * BR_SLICE + BR_FADE, BR_BEAT2, BR_SLICE - 1 -
            BR_FADE, BR_SLICE - 2 * BR_FADE, 1), 0);
    }

    test_assert(br_mismatch(BR_BEAT2 + BR_SLICE + BR_FADE, BR_BEAT2 + BR_FADE, -1,
        BR_SLICE - 2 * BR_FADE, 1) > 1000);
}

/** A pitch of -12 plays the second repeat at half speed, the first half of the slice an
 * octave down, and the third at a quarter: on a sine of 1000 Hz they read 500 and 250 Hz, and
 * a frame of the second repeat at an even offset is the frame of the slice at half of it. */
void BeatRepeat_pitch_down(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_params(0.25f, -1, 1, -12, 0, 1, 1);
    int32_t r1 = BR_BEAT2 + BR_SLICE, r2 = BR_BEAT2 + 2 * BR_SLICE, j;
    double f0, f1, f2, worst = 0;
    br_fill(1000);
    br_pair(&p, 47000, -1, BR_MAX);
    test_int(br_mismatch(0, 0, -1, BR_BEAT2 + BR_SLICE + 1, 1), 0);
    f0 = audio_frequency(br_l, BR_BEAT2 + BR_FADE, r1);
    f1 = audio_frequency(br_l, r1 + BR_FADE, r2);
    f2 = audio_frequency(br_l, r2 + BR_FADE, r2 + BR_SLICE);
    printf("pitch -12: %.1f Hz, then %.1f Hz, then %.1f Hz\n", f0, f1, f2);
    test_assert(fabs(f0 - 1000) < 5);
    test_assert(fabs(f1 - 500) < 5);
    test_assert(fabs(f2 - 250) < 5);
    for (j = BR_FADE; j < BR_SLICE; j += 2) {
        double e = fabs((double)br_l[r1 + j] - (double)br_dry_l[BR_BEAT2 + j / 2]);
        worst = e > worst ? e : worst;
    }

    printf("largest difference of the second repeat to the slice: %g\n", worst);
    test_assert(worst < 1e-5);
}

/** A decay of 0.5 drops each repeat by 6 dB against the one before it, and each is the slice
 * at its gain. */
void BeatRepeat_decay(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_params(0.25f, -1, 0.5f, 0, 0, 1, 1);
    double prev = 0, rms, db;
    float gain = 1;
    int32_t n;
    br_fill(0);
    br_pair(&p, 47000, -1, BR_MAX);
    for (n = 0; n < 5; n ++) {
        int32_t at = BR_BEAT2 + n * BR_SLICE + BR_FADE;
        rms = br_rms(br_l, at, BR_SLICE - 2 * BR_FADE);
        if (n) {
            db = 20 * log10(rms / prev);
            printf("repeat %d: %.2f dB against the one before\n", n, db);
            test_assert(fabs(db + 6.02) < 0.05);
            test_int(br_mismatch(at, BR_BEAT2 + BR_FADE, -1, BR_SLICE - 2 * BR_FADE, gain), 0);
        }

        prev = rms;
        gain *= 0.5f;
    }
}

/** A gate of 0.5 sounds the first half of each repeat and leaves the second silent. */
void BeatRepeat_gate(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_params(0.25f, -1, 1, 0, 0, 0.5f, 1);
    int32_t n, half = BR_SLICE / 2;
    br_fill(0);
    br_pair(&p, 47000, -1, BR_MAX);
    for (n = 0; n < 4; n ++) {
        int32_t at = BR_BEAT2 + n * BR_SLICE;
        test_int(br_mismatch(at + BR_FADE, BR_BEAT2 + BR_FADE, -1, half - BR_FADE, 1), 0);
        test_assert(audio_peak(br_l, at + half + BR_FADE, at + BR_SLICE) == 0);
        test_assert(audio_peak(br_r, at + half + BR_FADE, at + BR_SLICE) == 0);
    }
}

/** At a mix of 0.5 the output is half the input and half the repeats. */
void BeatRepeat_mix(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_params(0.25f, -1, 1, 0, 0, 1, 0.5f);
    int32_t at = BR_BEAT2 + 2 * BR_SLICE, j;
    double worst = 0;
    br_fill(0);
    br_pair(&p, 47000, -1, BR_MAX);
    test_int(br_mismatch(0, 0, -1, BR_BEAT2 + BR_SLICE + 1, 1), 0);
    for (j = BR_FADE; j < BR_SLICE; j ++) {
        double expect = 0.5 * (double)br_dry_l[at + j] + 0.5 * (double)br_dry_l[BR_BEAT2 + j];
        double e = fabs((double)br_l[at + j] - expect);
        worst = e > worst ? e : worst;
    }

    test_assert(worst < 1e-6);
}

/** On a sine whose period does not divide the slice, no boundary steps further than the sine
 * does: not the start of a repeat, forward, reversed, pitched or gated, nor the fall. */
void BeatRepeat_no_click(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t cases[] = {
        br_params(0.25f, -1, 1, 0, 0, 1, 1),
        br_params(0.25f, -1, 1, 0, 1, 1, 1),
        br_params(0.25f, -1, 0.8f, -5, 0, 1, 1),
        br_params(0.125f, -1, 1, 0, 0, 0.6f, 1),
        br_params(0.25f, 0, 1, -7, 1, 0.7f, 0.5f)
    };
    double natural = 2 * AUDIO_PI * 450.0 / AUDIO_RATE * 0.25 * BR_VOLUME;
    int32_t c;
    br_fill(450);
    for (c = 0; c < (int32_t)(sizeof(cases) / sizeof(cases[0])); c ++) {
        br_edge_t edges[2] = { { 47000, 1 }, { BR_BEAT2 + 5 * BR_SLICE + 2345, 0 } };
        float step;
        br_run(&cases[c], edges, 0, br_l, br_r, BR_MAX, BOX_AUDIO_CORE_QUANTUM);
        step = audio_max_step(br_l, BR_ARMED, BR_MAX);
        printf("case %d: largest step %.5f, the sine steps %.5f\n", c, (double)step, natural);
        test_assert(step < 2 * natural);
        test_assert(audio_max_step(br_r, BR_ARMED, BR_MAX) < 2 * natural);
    }
}

/** Renders of 1, 37, 128 and 1000 frames are bit identical, and two renders of one size. */
void BeatRepeat_render_sizes(void) {
    // audio/beat_repeat
    box_audio_beat_repeat_params_t p = br_params(0.125f, -1, 0.7f, -3, 1, 0.8f, 0.9f);
    br_edge_t edges[2] = { { 47000, 1 }, { BR_BEAT2 + 9 * 3000 + 1234, 0 } };
    int32_t sizes[] = { 1, 37, 128, 1000 }, s, frames = AUDIO_RATE + AUDIO_RATE / 2;
    uint64_t hash;
    br_fill(0);
    br_run(&p, edges, 0, br_b_l, br_b_r, frames, BOX_AUDIO_CORE_QUANTUM);
    hash = audio_hash(br_b_l, frames) ^ audio_hash(br_b_r, frames);
    test_assert(audio_peak(br_b_l, BR_BEAT2, frames) > 0.01f);
    for (s = 0; s < 4; s ++) {
        br_run(&p, edges, 0, br_l, br_r, frames, sizes[s]);
        test_assert(memcmp(br_l, br_b_l, (size_t)frames * sizeof(float)) == 0);
        test_assert(memcmp(br_r, br_b_r, (size_t)frames * sizeof(float)) == 0);
    }

    br_run(&p, edges, 0, br_l, br_r, frames, BOX_AUDIO_CORE_QUANTUM);
    test_assert((audio_hash(br_l, frames) ^ audio_hash(br_r, frames)) == hash);
}

/** The component defaults, its kind and its params, and the memory of the kind. */
void BeatRepeat_component(void) {
    // audio/beat_repeat
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    float params[BOX_AUDIO_EFFECT_PARAMS];
    const BeatRepeat *b;
    ecs_add(world, e, BeatRepeat);
    b = ecs_get(world, e, BeatRepeat);
    test_flt(b->active, 0);
    test_flt(b->slice, 0.25f);
    test_flt(b->grid, -1);
    test_flt(b->decay, 1);
    test_flt(b->pitch, 0);
    test_flt(b->reverse, 0);
    test_flt(b->gate, 1);
    test_flt(b->mix, 1);
    test_assert(ecs_has(world, e, AudioEffectState));
    ecs_set(world, e, BeatRepeat, { .active = 1, .slice = 0.5f, .grid = 1, .decay = 0.5f,
        .pitch = -12, .reverse = 1, .gate = 0.5f, .mix = 0.75f });
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_BEAT_REPEAT);
    test_flt(params[0], 1);
    test_flt(params[1], 0.5f);
    test_flt(params[2], 1);
    test_flt(params[3], 0.5f);
    test_flt(params[4], -12);
    test_flt(params[5], 1);
    test_flt(params[6], 0.5f);
    test_flt(params[7], 0.75f);
    test_assert(box_audio_effect_floats(BOX_AUDIO_EFFECT_BEAT_REPEAT, AUDIO_RATE) >=
        2 * (int32_t)(BOX_AUDIO_BEAT_REPEAT_MAX * AUDIO_RATE));
    ecs_fini(world);
}

/** The beat repeat and the tape stop of test/assets/audio/beat_repeat.flecs. */
void BeatRepeat_script(void) {
    // audio/beat_repeat
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/beat_repeat.flecs");
    ecs_entity_t stutter, drop;
    const BeatRepeat *b;
    const TapeStop *t;
    test_assert(ecs_script_run(world, "beat_repeat", code, NULL) == 0);
    ecs_os_free(code);
    stutter = ecs_lookup(world, "stutter");
    drop = ecs_lookup(world, "drop");
    test_assert(stutter != 0 && drop != 0);
    b = ecs_get(world, stutter, BeatRepeat);
    t = ecs_get(world, drop, TapeStop);
    test_assert(b != NULL && t != NULL);
    test_flt(b->slice, 0.25f);
    test_flt(b->grid, -1);
    test_flt(b->active, 0);
    test_flt(t->stop_time, 0.75f);
    test_flt(t->start_time, 0.3f);
    test_flt(t->curve, 0.5f);
    test_int(box_audio_module_effect_kind(world, drop, NULL), BOX_AUDIO_EFFECT_TAPE_STOP);
    ecs_fini(world);
}

/** Run test/assets/audio/beat_repeat.flecs with a sine on the stutter bus, with its lane or
 * without it, through 4 beats of the Transport, and keep the tap from beat 0 on.
 *
 * @param lane Whether the lane of active plays
 * @param out Receives BR_WORLD frames of the left channel
 */
static void br_world(
    bool lane,
    float *out)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/beat_repeat.flecs");
    ecs_entity_t stutter, fill;
    int64_t c0;
    test_assert(ecs_script_run(world, "beat_repeat", code, NULL) == 0);
    ecs_os_free(code);
    stutter = ecs_lookup(world, "stutter");
    fill = ecs_lookup(world, "lanes.fill_lane");
    test_assert(stutter != 0 && fill != 0);
    if (!lane) {
        ecs_delete(world, fill);
    }

    audio_source(world, audio_tone(world, NULL, 441.7, 0.4f, AUDIO_RATE), 1.0f, true, stutter);
    audio_run(world, 30);
    c0 = audio_tap_count;
    ecs_singleton_ensure(world, Transport)->playing = true;
    ecs_singleton_modified(world, Transport);
    while (audio_tap_count <= c0 + BR_WORLD) {
        audio_run(world, 1);
    }

    memcpy(out, audio_tap_l + c0, BR_WORLD * sizeof(float));
    ecs_fini(world);
}

/** An automation lane raises active at beat 2 and drops it at beat 3: the output repeats the
 * 125 ms from beat 2 four times, the input away from the crossfades sample for sample, which
 * the input itself does not, and the input returns after beat 3. */
void BeatRepeat_automation(void) {
    // audio/beat_repeat
    int32_t n, j, bad = 0, c = BR_BEAT2;
    br_world(false, br_world_dry);
    br_world(true, br_world_wet);
    for (j = 0; j < c + BR_SLICE; j ++) {
        bad += br_world_wet[j] != br_world_dry[j];
    }

    test_int(bad, 0);
    test_assert(audio_peak(br_world_dry, c, c + BR_SLICE) > 0.05f);
    for (n = 1; n < 4; n ++) {
        int32_t end = n < 3 ? BR_SLICE : BR_SLICE - 50;
        for (j = BR_FADE; j < end; j ++) {
            bad += br_world_wet[c + n * BR_SLICE + j] != br_world_dry[c + j];
        }

        test_int(bad, 0);
    }

    for (j = c + 4 * BR_SLICE + 200; j < BR_WORLD; j ++) {
        bad += br_world_wet[j] != br_world_dry[j];
    }

    test_int(bad, 0);

    for (j = BR_FADE; j < BR_SLICE; j ++) {
        bad += br_world_dry[c + BR_SLICE + j] != br_world_dry[c + j];
    }

    test_assert(bad > 1000);
}
