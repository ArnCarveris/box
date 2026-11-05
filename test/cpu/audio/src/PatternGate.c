/**
 * @file PatternGate.c
 * @brief The pattern gate, measured on constant levels and sines in offline renders at the
 * 120 bpm of the beat clock of a new core: the pattern in blocks of 125 ms and the frames of
 * its edges, accents, seeks, tempo changes and loop wraps of the clock, swing, a floor that
 * dips, steps that change ahead, clicks, the gain reduction, rest on silence, render sizes,
 * the component and test/assets/audio/pattern_gate.flecs with an automated step.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, three seconds */
#define PG_FRAMES (AUDIO_RATE * 3)

/** Frames of a sixteenth at 120 bpm, 125 ms */
#define PG_STEP (AUDIO_RATE / 8)

/** Frames of the default attack, 2 ms */
#define PG_ATTACK (AUDIO_RATE / 500)

/** Frames of the default release, 30 ms */
#define PG_RELEASE (AUDIO_RATE * 3 / 100)

/** Level of the sounds */
#define PG_LEVEL 0.25f

/** Frequency of the sines, a whole number of periods in the second of a sound */
#define PG_TONE 250.0

/** Largest gain of a closed step at the default floor of -80 dB, with room for rounding */
#define PG_CLOSED 2e-4

/** Smallest gain of an open step at a level of 1 */
#define PG_OPEN 0.9999

/** Output through the pattern gate */
static float pg_l[PG_FRAMES], pg_r[PG_FRAMES];

/** Output of the same render without it */
static float pg_dry_l[PG_FRAMES], pg_dry_r[PG_FRAMES];

/** Output of a second render that a case compares */
static float pg_b_l[PG_FRAMES], pg_b_r[PG_FRAMES];

/** The sound, a second */
static float pg_sound[AUDIO_RATE];

/** Level in dB of a linear level.
 *
 * @param x The linear level
 * @return The level, -300 for silence
 */
static double pg_db(
    double x)
{
    x = x < 0 ? -x : x;

    return x > 1e-15 ? 20 * log10(x) : -300;
}

/** The default parameters of a pattern gate, those of the component.
 *
 * @return The parameters
 */
static box_audio_pattern_gate_params_t pg_params(void)
{
    box_audio_pattern_gate_params_t p = { .length = 16, .rate = 0.25f, .swing = 0,
        .attack = 0.002f, .release = 0.03f, .gate = 0.5f, .floor = -80, .mix = 1 };
    int32_t i;
    for (i = 0; i < BOX_AUDIO_PATTERN_GATE_STEPS; i ++) {
        p.steps[i] = 1;
    }

    return p;
}

/** Parameters with a pattern of a few steps.
 *
 * @param steps Levels of the steps
 * @param length Number of steps
 * @param gate Share of each step that is open
 * @return The parameters
 */
static box_audio_pattern_gate_params_t pg_pattern(
    const float *steps,
    int32_t length,
    float gate)
{
    box_audio_pattern_gate_params_t p = pg_params();
    int32_t i;
    for (i = 0; i < length; i ++) {
        p.steps[i] = steps[i];
    }

    p.length = (float)length;
    p.gate = gate;

    return p;
}

/** The params of an effect command of a pattern gate.
 *
 * @param p The parameters
 * @param params Receives BOX_AUDIO_EFFECT_PARAMS floats
 */
static void pg_floats(
    const box_audio_pattern_gate_params_t *p,
    float *params)
{
    memset(params, 0, sizeof(float) * BOX_AUDIO_EFFECT_PARAMS);
    memcpy(params, p, sizeof(*p));
}

/** Put a pattern gate into insert slot 0 of bus 1.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param p The parameters
 */
static void pg_effect(
    box_audio_core_t *core,
    uint32_t slot,
    const box_audio_pattern_gate_params_t *p)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS];
    box_audio_writer_t w;
    pg_floats(p, params);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, BOX_AUDIO_EFFECT_PATTERN_GATE, 1, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Schedule new parameters of the pattern gate in slot 0 at a frame.
 *
 * @param core The core
 * @param p The parameters
 * @param when Frame
 */
static void pg_change(
    box_audio_core_t *core,
    const box_audio_pattern_gate_params_t *p,
    uint64_t when)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS];
    box_audio_writer_t w;
    pg_floats(p, params);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect_params(&w, 0, BOX_AUDIO_EFFECT_PATTERN_GATE, params,
        when));
    box_audio_core_apply(core, w.data, w.size);
}

/** Schedule a BEAT command, as the transport sends at a seek, a loop wrap or a tempo change.
 *
 * @param core The core
 * @param beat The beat at the frame
 * @param tempo Beats per second from the frame on
 * @param when Frame
 */
static void pg_beat(
    box_audio_core_t *core,
    double beat,
    float tempo,
    uint64_t when)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_beat(&w, beat, tempo, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with bus 1 into the master and, when given, a pattern gate on it, render
 * frame 0 in silence, so that the gate starts at once, and play pg_sound on bus 1 from frame 1
 * on as sound 1.
 *
 * @param p The parameters of the gate, NULL for none
 * @param l Receives frame 0 of the left channel
 * @param r Receives frame 0 of the right channel
 * @param loop Whether the sound loops
 * @return The core
 */
static box_audio_core_t* pg_core(
    const box_audio_pattern_gate_params_t *p,
    float *l,
    float *r,
    bool loop)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    audio_order(core, routes, 2);
    if (p) {
        pg_effect(core, 0, p);
    }

    box_audio_core_render(core, l, r, 1);
    test_assert(box_audio_core_sound(core, 1, pg_sound, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, loop ? BOX_AUDIO_PLAY_LOOP : 0);

    return core;
}

/** Render a range of frames into buffers whose index is the frame of the core clock.
 *
 * @param core The core
 * @param l Left channel
 * @param r Right channel
 * @param from First frame, the clock of the core
 * @param to End frame
 * @param size Frames of each render, 0 for sizes that vary
 */
static void pg_render(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t from,
    int32_t to,
    int32_t size)
{
    int32_t k = 0;
    test_assert(box_audio_core_clock(core) == (uint64_t)from);
    while (from < to) {
        int32_t n = size ? size : 1 + (k * 97) % 613;
        n = n < to - from ? n : to - from;
        box_audio_core_render(core, l + from, r + from, n);
        from += n;
        k ++;
    }
}

/** Render pg_sound, looped, through a pattern gate into pg_l and pg_r and without it into
 * pg_dry_l and pg_dry_r, up to a frame.
 *
 * @param p The parameters
 * @param frames End frame
 */
static void pg_run(
    const box_audio_pattern_gate_params_t *p,
    int32_t frames)
{
    box_audio_core_t *core = pg_core(p, pg_l, pg_r, true);
    pg_render(core, pg_l, pg_r, 1, frames, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);
    core = pg_core(NULL, pg_dry_l, pg_dry_r, true);
    pg_render(core, pg_dry_l, pg_dry_r, 1, frames, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);
}

/** Gain of the pattern gate at a frame of a constant level: the output over the dry one.
 *
 * @param frame The frame
 * @return The gain
 */
static double pg_gain(
    int32_t frame)
{
    test_assert(fabsf(pg_dry_l[frame]) > 1e-3f);

    return (double)pg_l[frame] / (double)pg_dry_l[frame];
}

/** Check that the gain at every frame of a range of a constant level lies within bounds.
 *
 * @param from First frame
 * @param to End frame
 * @param lo Smallest gain
 * @param hi Largest gain
 */
static void pg_check(
    int32_t from,
    int32_t to,
    double lo,
    double hi)
{
    int32_t i;
    for (i = from; i < to; i ++) {
        double g = pg_gain(i);
        if (!(g >= lo && g <= hi)) {
            printf("gain %.7f at frame %d, expected %.7f to %.7f over %d to %d\n", g, i, lo,
                hi, from, to);
        }

        test_assert(g >= lo && g <= hi);
    }
}

/** Check that a range of a constant level is open at a level of 1.
 *
 * @param from First frame
 * @param to End frame
 */
static void pg_open(
    int32_t from,
    int32_t to)
{
    pg_check(from, to, PG_OPEN, 1.0001);
}

/** Check that a range of a constant level is closed at the default floor.
 *
 * @param from First frame
 * @param to End frame
 */
static void pg_closed(
    int32_t from,
    int32_t to)
{
    pg_check(from, to, 0, PG_CLOSED);
}

/** A sustained sine through 1, 0, 1, 0 at sixteenths and 120 bpm, open all of each step, is
 * as loud as without the gate and 80 dB down in turns of 125 ms, past the edges. */
void PatternGate_pattern(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0, 1, 0 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 4, 1);
    int32_t k;
    audio_sine(pg_sound, AUDIO_RATE, PG_TONE, PG_LEVEL);
    pg_run(&p, 9 * PG_STEP);
    for (k = 0; k < 9; k ++) {
        int32_t from = k * PG_STEP + PG_RELEASE + 100, to = (k + 1) * PG_STEP;
        double left = pg_db((double)audio_peak(pg_l, from, to) /
            (double)audio_peak(pg_dry_l, from, to));
        double right = pg_db((double)audio_peak(pg_r, from, to) /
            (double)audio_peak(pg_dry_r, from, to));
        printf("step %d: %.3f dB left, %.3f dB right\n", k, left, right);
        if (k % 2 == 0) {
            test_assert(left > -0.01 && right > -0.01);
        } else {
            test_assert(left < -79 && right < -79);
        }

        test_assert(fabs(left - right) < 0.01);
    }
}

/** The edges of 1, 0, 1, 0 on a constant level start at the frames of the steps: the gain
 * rises over the 96 frames of the attack and falls over the 1440 of the release, linearly. At
 * a gate of 0.5 it falls in the middle of each step. */
void PatternGate_edges(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0, 1, 0 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 4, 1);
    int32_t up = 2 * PG_STEP, down = 3 * PG_STEP;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);
    pg_run(&p, 5 * PG_STEP);
    printf("rising edge: %.5f %.5f %.5f %.5f\n", pg_gain(up - 1), pg_gain(up),
        pg_gain(up + PG_ATTACK / 2 - 1), pg_gain(up + PG_ATTACK - 1));
    printf("falling edge: %.5f %.5f %.5f %.5f\n", pg_gain(down - 1), pg_gain(down),
        pg_gain(down + PG_RELEASE / 2 - 1), pg_gain(down + PG_RELEASE - 1));
    pg_closed(PG_STEP + PG_RELEASE, up);
    pg_check(up, up + 1, 0.005, 0.02);
    pg_check(up + PG_ATTACK / 2 - 1, up + PG_ATTACK / 2, 0.45, 0.55);
    pg_open(up + PG_ATTACK, down);
    pg_check(down, down + 1, 0.99, 0.9999);
    pg_check(down + PG_RELEASE / 2 - 1, down + PG_RELEASE / 2, 0.45, 0.55);
    pg_closed(down + PG_RELEASE, 4 * PG_STEP);
    pg_open(4 * PG_STEP + PG_ATTACK, 5 * PG_STEP);

    /* Every step open for half of it */
    p = pg_params();
    pg_run(&p, 3 * PG_STEP);
    pg_open(PG_STEP + PG_ATTACK, PG_STEP + PG_STEP / 2);
    pg_check(PG_STEP + PG_STEP / 2, PG_STEP + PG_STEP / 2 + 1, 0.99, 0.9999);
    pg_closed(PG_STEP + PG_STEP / 2 + PG_RELEASE, 2 * PG_STEP);
    pg_check(2 * PG_STEP, 2 * PG_STEP + 1, 0.005, 0.02);
    pg_open(2 * PG_STEP + PG_ATTACK, 2 * PG_STEP + PG_STEP / 2);
}

/** A step at a level of 0.5 is an accent 6 dB down. */
void PatternGate_accent(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0.5f };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 2, 1);
    double accent;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);
    pg_run(&p, 4 * PG_STEP);
    accent = pg_db(pg_gain(PG_STEP + PG_STEP / 2));
    printf("accent: %.4f dB\n", accent);
    test_assert(fabs(accent + 6.0206) < 0.01);
    pg_check(PG_STEP + PG_RELEASE, 2 * PG_STEP, 0.5, 0.5002);
    pg_open(2 * PG_STEP + PG_ATTACK, 3 * PG_STEP);
    pg_check(3 * PG_STEP + PG_RELEASE, 4 * PG_STEP, 0.5, 0.5002);
}

/** A seek of the beat clock into the middle of a step moves the pattern there at once, and a
 * change of the tempo shortens the steps from its frame on: the edges follow the new grid. */
void PatternGate_seek(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0, 1, 0 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 4, 1);
    const int32_t seek = 33000, tempo = 60000, fast = AUDIO_RATE / 12, end = 78000;
    box_audio_core_t *core;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);

    /* Beat 3.125 is half way into step 12, which is open, while step 5 of the clock was
     * closed; then beat 4.25 at 180 bpm, step 17, closed, with steps of 4000 frames */
    core = pg_core(&p, pg_l, pg_r, true);
    pg_beat(core, 3.125, 2.0f, (uint64_t)seek);
    pg_beat(core, 4.25, 3.0f, (uint64_t)tempo);
    pg_render(core, pg_l, pg_r, 1, end, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);
    core = pg_core(NULL, pg_dry_l, pg_dry_r, true);
    pg_render(core, pg_dry_l, pg_dry_r, 1, end, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);

    pg_closed(5 * PG_STEP + PG_RELEASE, seek);
    pg_open(seek + PG_ATTACK, seek + PG_STEP / 2);
    pg_closed(seek + PG_STEP / 2 + PG_RELEASE, seek + PG_STEP * 3 / 2);
    pg_open(seek + PG_STEP * 3 / 2 + PG_ATTACK, seek + PG_STEP * 5 / 2);
    pg_closed(tempo + PG_RELEASE, tempo + fast);
    pg_open(tempo + fast + PG_ATTACK, tempo + 2 * fast);
    pg_closed(tempo + 2 * fast + PG_RELEASE, tempo + 3 * fast);
    pg_open(tempo + 3 * fast + PG_ATTACK, tempo + 4 * fast);
}

/** A loop of 1.5 beats, 6 steps, restarts the pattern of 1, 0, 0, 0 at each wrap: the step
 * after the wrap is the first, open, where step 6 of a clock without the loop is closed. */
void PatternGate_loop_wrap(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0, 0, 0 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 4, 1);
    const int32_t loop = 6 * PG_STEP, end = 2 * loop + 2 * PG_STEP;
    box_audio_core_t *core;
    int32_t k;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);
    core = pg_core(&p, pg_l, pg_r, true);
    pg_beat(core, 0, 2.0f, (uint64_t)loop);
    pg_beat(core, 0, 2.0f, (uint64_t)(2 * loop));
    pg_render(core, pg_l, pg_r, 1, end, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);
    core = pg_core(NULL, pg_dry_l, pg_dry_r, true);
    pg_render(core, pg_dry_l, pg_dry_r, 1, end, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);

    for (k = 0; k < 3; k ++) {
        int32_t at = k * loop;
        if (k) {
            pg_closed(at - PG_STEP + PG_RELEASE, at);
        }

        pg_open(at + (k ? PG_ATTACK : 1000), at + PG_STEP);
        if (k < 2) {
            pg_closed(at + PG_STEP + PG_RELEASE, at + 4 * PG_STEP);
            pg_open(at + 4 * PG_STEP + PG_ATTACK, at + 5 * PG_STEP);
        }
    }
}

/** A swing of 0.5 starts the second step of each pair half a step late and keeps the first
 * on the beat: 0, 1 opens at 9000 frames instead of 6000 and closes at 12000. */
void PatternGate_swing(void) {
    // audio/pattern_gate
    const float steps[] = { 0, 1 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 2, 1);
    const int32_t late = PG_STEP * 3 / 2;
    p.swing = 0.5f;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);
    pg_run(&p, 4 * PG_STEP);
    pg_closed(1000, late);
    pg_check(late, late + 1, 0.005, 0.02);
    pg_open(late + PG_ATTACK, 2 * PG_STEP);
    pg_closed(2 * PG_STEP + PG_RELEASE, 2 * PG_STEP + late);
    pg_open(2 * PG_STEP + late + PG_ATTACK, 4 * PG_STEP);

    /* A gate of 0.5 of the swung steps: the long first step of a pair open 4500 frames, the
     * short second one 1500 */
    p = pg_params();
    p.swing = 0.5f;
    pg_run(&p, 2 * PG_STEP);
    pg_open(1000, late / 2);
    pg_closed(late / 2 + PG_RELEASE, late);
    pg_open(late + PG_ATTACK, late + PG_STEP / 4);
    pg_check(late + PG_STEP / 4, late + PG_STEP / 4 + 1, 0.99, 0.9999);
}

/** A floor of -6 dB pumps instead of chopping: the gain only dips, to -6 dB. */
void PatternGate_floor_dips(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 2, 1);
    double low = pow(10.0, -6.0 / 20.0);
    p.floor = -6;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);
    pg_run(&p, 4 * PG_STEP);
    pg_check(1000, 4 * PG_STEP, low * 0.9999, 1.0001);
    pg_check(PG_STEP + PG_RELEASE, 2 * PG_STEP, low * 0.9999, low * 1.0001);
    pg_open(2 * PG_STEP + PG_ATTACK, 3 * PG_STEP);
    printf("floor: %.4f dB\n", pg_db(pg_gain(PG_STEP + PG_STEP / 2)));
}

/** Steps that change ahead, as automation schedules them: step 2 of 1, 1, 0, 1 opens from the
 * second pass on and is an accent from the third, and the gain moves at the speed of the
 * edges. */
void PatternGate_automate_step(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 1, 0, 1 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 4, 1);
    const int32_t end = 12 * PG_STEP;
    box_audio_core_t *core;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);
    core = pg_core(&p, pg_l, pg_r, true);
    p.steps[2] = 1;
    pg_change(core, &p, 4 * PG_STEP + PG_STEP / 2);
    p.steps[2] = 0.5f;
    pg_change(core, &p, 8 * PG_STEP + PG_STEP / 2);
    pg_render(core, pg_l, pg_r, 1, end, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);
    core = pg_core(NULL, pg_dry_l, pg_dry_r, true);
    pg_render(core, pg_dry_l, pg_dry_r, 1, end, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_free(core);

    pg_open(1000, 2 * PG_STEP);
    pg_closed(2 * PG_STEP + PG_RELEASE, 3 * PG_STEP);
    pg_open(3 * PG_STEP + PG_ATTACK, 10 * PG_STEP);
    pg_check(10 * PG_STEP + PG_RELEASE, 11 * PG_STEP, 0.5, 0.5002);
    pg_open(11 * PG_STEP + PG_ATTACK, 12 * PG_STEP);
}

/** The default attack and release do not click: a sine pulsed by every step at a gate of 0.5
 * changes from one sample to the next by little more than the sine itself does. */
void PatternGate_no_click(void) {
    // audio/pattern_gate
    box_audio_pattern_gate_params_t p = pg_params();
    float gated, dry;
    audio_sine(pg_sound, AUDIO_RATE, PG_TONE, PG_LEVEL);
    pg_run(&p, 8 * PG_STEP);
    gated = audio_max_step(pg_l, 1000, 8 * PG_STEP);
    dry = audio_max_step(pg_dry_l, 1000, 8 * PG_STEP);
    printf("largest step: %.5f gated, %.5f dry\n", (double)gated, (double)dry);
    test_assert(gated < dry * 1.5f);
    test_assert(audio_peak(pg_l, PG_STEP / 2 + PG_RELEASE, PG_STEP) < audio_peak(pg_dry_l,
        PG_STEP / 2 + PG_RELEASE, PG_STEP) * 1e-3f);
}

/** The meter of the effect reads the gain reduction of a closed step, the floor; without a
 * closed step it reads 0. */
void PatternGate_reduction(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 2, 1);
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    box_audio_core_t *core;
    p.floor = -40;
    audio_sine(pg_sound, AUDIO_RATE, PG_TONE, PG_LEVEL);
    core = pg_core(&p, pg_l, pg_r, true);
    pg_render(core, pg_l, pg_r, 1, 2 * PG_STEP, BOX_AUDIO_CORE_QUANTUM);
    test_int(box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX),
        BOX_AUDIO_EFFECT_MAX);
    printf("reduction of a closed step at -40 dB: %.3f dB\n", (double)meters[0].reduction);
    test_assert(fabs(meters[0].reduction - 40) < 0.05);
    test_flt(meters[1].reduction, 0);
    box_audio_core_free(core);

    p = pg_params();
    p.gate = 1;
    core = pg_core(&p, pg_l, pg_r, true);
    pg_render(core, pg_l, pg_r, 1, 2 * PG_STEP, BOX_AUDIO_CORE_QUANTUM);
    box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    test_assert(meters[0].reduction < 1e-3f);
    box_audio_core_free(core);
}

/** A pattern gate stops while its input is silent and keeps its gain on the grid meanwhile:
 * a sound that starts again later is gated as the pattern of the clock says. */
void PatternGate_rest(void) {
    // audio/pattern_gate
    const float steps[] = { 1, 0 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 2, 1);
    const int32_t again = 17 * PG_STEP + 1000, end = 21 * PG_STEP;
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_core_t *core;
    int32_t pass;
    audio_fill(pg_sound, AUDIO_RATE, PG_LEVEL);
    for (pass = 0; pass < 2; pass ++) {
        float *l = pass ? pg_dry_l : pg_l, *r = pass ? pg_dry_r : pg_r;
        core = pg_core(pass ? NULL : &p, l, r, false);
        box_audio_writer_init(&w, bytes, sizeof(bytes));
        test_assert(box_audio_write_play(&w, 2, 1, 1, 1, 0, 1, 0, (uint64_t)again));
        box_audio_core_apply(core, w.data, w.size);
        pg_render(core, l, r, 1, again - 1000, BOX_AUDIO_CORE_QUANTUM);
        if (!pass) {
            test_assert(!box_audio_core_effect_running(core, 0));
        }

        pg_render(core, l, r, again - 1000, end, BOX_AUDIO_CORE_QUANTUM);
        if (!pass) {
            test_assert(box_audio_core_effect_running(core, 0));
        }

        box_audio_core_free(core);
    }

    pg_closed(again + 1000, 18 * PG_STEP);
    pg_open(18 * PG_STEP + PG_ATTACK, 19 * PG_STEP);
    pg_closed(19 * PG_STEP + PG_RELEASE, 20 * PG_STEP);
    pg_open(20 * PG_STEP + PG_ATTACK, 21 * PG_STEP);
}

/** Render a scene of a pattern gate with swing and accents on a sine that stops and starts
 * again, seeks, a tempo change and steps that change ahead, in pieces of a size.
 *
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @param size Frames of each render, 0 for sizes that vary
 */
static void pg_sizes(
    float *left,
    float *right,
    int32_t size)
{
    const float steps[] = { 1, 0.3f, 0, 1, 0.7f, 0, 1 };
    box_audio_pattern_gate_params_t p = pg_pattern(steps, 7, 0.6f);
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_core_t *core;
    p.swing = 0.3f;
    p.rate = 1.0f / 6;
    p.floor = -30;
    p.mix = 0.9f;
    core = pg_core(&p, left, right, false);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play(&w, 2, 1, 1, 0.7f, 0.3f, 1.5f, 0, 70001));
    box_audio_core_apply(core, w.data, w.size);
    pg_beat(core, 7.3, 2.5f, 20011);
    pg_beat(core, 0, 1.7f, 90007);
    p.steps[2] = 0.8f;
    p.gate = 0.3f;
    pg_change(core, &p, 33333);
    p.floor = -80;
    pg_change(core, &p, 100003);
    pg_render(core, left, right, 1, 2 * AUDIO_RATE, size);
    box_audio_core_free(core);
}

/** The render does not depend on where the blocks end, and two renders are the same. */
void PatternGate_render_sizes(void) {
    // audio/pattern_gate
    audio_sine(pg_sound, AUDIO_RATE, PG_TONE, PG_LEVEL);
    pg_sizes(pg_l, pg_r, BOX_AUDIO_CORE_QUANTUM);
    pg_sizes(pg_b_l, pg_b_r, 0);
    test_assert(audio_hash(pg_l, 2 * AUDIO_RATE) == audio_hash(pg_b_l, 2 * AUDIO_RATE));
    test_assert(audio_hash(pg_r, 2 * AUDIO_RATE) == audio_hash(pg_b_r, 2 * AUDIO_RATE));
    pg_sizes(pg_b_l, pg_b_r, 4096);
    test_assert(audio_hash(pg_l, 2 * AUDIO_RATE) == audio_hash(pg_b_l, 2 * AUDIO_RATE));
    pg_sizes(pg_b_l, pg_b_r, BOX_AUDIO_CORE_QUANTUM);
    test_assert(audio_hash(pg_l, 2 * AUDIO_RATE) == audio_hash(pg_b_l, 2 * AUDIO_RATE));
    test_assert(audio_hash(pg_r, 2 * AUDIO_RATE) == audio_hash(pg_b_r, 2 * AUDIO_RATE));
    test_assert(audio_peak(pg_l, 0, AUDIO_RATE) > 0.01f);
    test_assert(audio_peak(pg_l, AUDIO_RATE * 3 / 2, 2 * AUDIO_RATE) > 0.01f);
}

/** The component has the defaults of a trance gate on sixteenths, is an effect of its own
 * kind and sends its steps as the first params, so that automation and the modulators reach
 * each step. */
void PatternGate_component(void) {
    // audio/pattern_gate
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    float params[BOX_AUDIO_EFFECT_PARAMS];
    const PatternGate *g;
    PatternGate *m;
    int32_t i;
    ecs_add(world, e, PatternGate);
    g = ecs_get(world, e, PatternGate);
    for (i = 0; i < 16; i ++) {
        test_flt(g->steps[i], 1);
    }

    test_int(g->length, 16);
    test_flt(g->rate, 0.25f);
    test_flt(g->swing, 0);
    test_flt(g->attack, 0.002f);
    test_flt(g->release, 0.03f);
    test_flt(g->gate, 0.5f);
    test_flt(g->floor, -80);
    test_flt(g->mix, 1);
    test_assert(ecs_has(world, e, AudioEffectState));
    m = ecs_ensure(world, e, PatternGate);
    m->steps[3] = 0.25f;
    m->length = 8;
    m->swing = 0.2f;
    ecs_modified(world, e, PatternGate);
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_PATTERN_GATE);
    test_flt(params[0], 1);
    test_flt(params[3], 0.25f);
    test_flt(params[BOX_AUDIO_PATTERN_GATE_STEPS], 8);
    test_flt(params[BOX_AUDIO_PATTERN_GATE_STEPS + 1], 0.25f);
    test_flt(params[BOX_AUDIO_PATTERN_GATE_STEPS + 2], 0.2f);
    test_flt(params[BOX_AUDIO_PATTERN_GATE_STEPS + 6], -80);
    test_flt(params[BOX_AUDIO_PATTERN_GATE_STEPS + 7], 1);
    for (i = BOX_AUDIO_PATTERN_GATE_STEPS + 8; i < BOX_AUDIO_EFFECT_PARAMS; i ++) {
        test_flt(params[i], 0);
    }

    ecs_fini(world);
}

/** Mean absolute level of the left channel of the tap over the middle half of a step.
 *
 * @param c0 Frame of beat 0
 * @param step The step, a sixteenth at 120 bpm
 * @return The level
 */
static double pg_tap_level(
    int64_t c0,
    int32_t step)
{
    int32_t from = (int32_t)(c0 + (int64_t)step * PG_STEP + PG_STEP / 4), i;
    double sum = 0;
    test_assert(from + PG_STEP / 2 <= audio_tap_count);
    for (i = 0; i < PG_STEP / 2; i ++) {
        sum += fabs((double)audio_tap_l[from + i]);
    }

    return sum / (double)(PG_STEP / 2);
}

/** test/assets/audio/pattern_gate.flecs: the trance gate on a pad plays its pattern on the
 * grid of the Transport, with its accents; the lane on steps[2] opens step 2 over the first
 * four beats, after which it is closed again as the component says; the meter reads the
 * reduction of the floor. */
void PatternGate_script(void) {
    // audio/pattern_gate
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    char *code = audio_read("test/assets/audio/pattern_gate.flecs");
    ecs_entity_t pad, gate;
    double open, closed, accent, automated, after;
    float reduction = 0;
    int64_t c0;
    test_assert(ecs_script_run(world, "pattern_gate", code, NULL) == 0);
    ecs_os_free(code);
    pad = ecs_lookup(world, "pad");
    gate = ecs_lookup(world, "pad_gate");
    test_assert(pad && gate);
    test_int(ecs_get(world, gate, PatternGate)->length, 16);
    test_flt(ecs_get(world, gate, PatternGate)->steps[2], 0);
    test_flt(ecs_get(world, gate, PatternGate)->steps[6], 0.5f);
    audio_source(world, audio_dc(world, NULL, PG_LEVEL, AUDIO_RATE), 1.0f, true, pad);
    audio_run(world, 30);
    test_assert(box_audio_module_effect_slot(world, gate) >= 0);
    test_assert(state->timeline.beat_wanted);
    ecs_singleton_ensure(world, Transport)->playing = true;
    ecs_singleton_modified(world, Transport);
    c0 = audio_tap_count;
    while (audio_tap_count <= c0 + 20 * PG_STEP + AUDIO_FRAME) {
        audio_run(world, 1);
        reduction = ecs_get(world, gate, AudioMeter)->reduction > reduction ?
            ecs_get(world, gate, AudioMeter)->reduction : reduction;
    }

    open = pg_tap_level(c0, 0);
    closed = pg_tap_level(c0, 1);
    automated = pg_tap_level(c0, 2);
    accent = pg_tap_level(c0, 6);
    after = pg_tap_level(c0, 18);
    printf("open %.5f, closed %.5f, automated %.5f, accent %.5f, after the lane %.5f\n",
        open, closed, automated, accent, after);
    test_assert(open > 0.05);
    test_assert(closed < open * 1e-3);
    test_assert(fabs(automated - open) < open * 1e-3);
    test_assert(fabs(pg_db(accent / open) + 6.02) < 0.05);
    test_assert(after < open * 1e-3);
    test_assert(pg_tap_level(c0, 16) > open * 0.999);
    printf("largest reduction: %.2f dB\n", (double)reduction);
    test_assert(reduction > 40);
    test_int(state->automation.stats.errors, 0);
    ecs_fini(world);
}
