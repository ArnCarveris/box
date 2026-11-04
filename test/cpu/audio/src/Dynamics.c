/**
 * @file Dynamics.c
 * @brief The compressor and the gate, measured with constant levels and sines in offline
 * renders: the static curve, the knee, the ballistics, ducking under a sidechain, the gate and
 * its hysteresis, the gain reduction, the order of the slots and the components in a world.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, two seconds */
#define DY_FRAMES (AUDIO_RATE * 2)

/** Output of the last render */
static float dy_l[DY_FRAMES], dy_r[DY_FRAMES];

/** The input sounds */
static float dy_sound[DY_FRAMES], dy_key[DY_FRAMES];

/** Output of a second render that a case compares */
static float dy_b_l[DY_FRAMES], dy_b_r[DY_FRAMES];

/** The frame of silence that a core renders before its sound plays */
static float dy_silence[2];

/** Linear level of a level in dB.
 *
 * @param db The level
 * @return The linear level
 */
static float dy_linear(
    double db)
{
    return (float)pow(10.0, db / 20.0);
}

/** Level in dB of a linear level.
 *
 * @param x The linear level
 * @return The level, -300 for silence
 */
static double dy_db(
    double x)
{
    x = x < 0 ? -x : x;

    return x > 1e-15 ? 20 * log10(x) : -300;
}

/** The parameters of a compressor.
 *
 * @param threshold dB
 * @param ratio Ratio
 * @param attack Seconds
 * @param release Seconds
 * @param knee dB
 * @param makeup dB
 * @param mix Wet level
 * @return The parameters
 */
static box_audio_compressor_params_t dy_comp(
    float threshold,
    float ratio,
    float attack,
    float release,
    float knee,
    float makeup,
    float mix)
{
    return (box_audio_compressor_params_t){
        threshold, ratio, attack, release, knee, makeup, mix };
}

/** The parameters of a gate.
 *
 * @param threshold dB
 * @param attack Seconds
 * @param hold Seconds
 * @param release Seconds
 * @param range dB
 * @return The parameters
 */
static box_audio_gate_params_t dy_gate(
    float threshold,
    float attack,
    float hold,
    float release,
    float range)
{
    return (box_audio_gate_params_t){ threshold, attack, hold, release, range };
}

/** Send an effect command.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param kind A box_audio_effect_kind_t
 * @param bus Bus, BOX_AUDIO_BUS_NONE to take it off
 * @param index Insert slot within the bus
 * @param key Key bus, BOX_AUDIO_BUS_NONE for none
 * @param p The parameters
 * @param size Bytes of the parameters
 */
static void dy_effect(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    uint32_t index,
    uint32_t key,
    const void *p,
    size_t size)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, p, size);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, kind, bus, index, key, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Put a compressor into insert slot 0 of a bus.
 *
 * @param core The core
 * @param slot Slot of the effect
 * @param bus Bus
 * @param key Key bus, BOX_AUDIO_BUS_NONE for none
 * @param p The parameters
 */
static void dy_compressor(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t bus,
    uint32_t key,
    box_audio_compressor_params_t p)
{
    dy_effect(core, slot, BOX_AUDIO_EFFECT_COMPRESSOR, bus, 0, key, &p, sizeof(p));
}

/** Render frames into dy_l and dy_r from a frame on, in quanta of the web.
 *
 * @param core The core
 * @param at First frame
 * @param frames Number of frames
 */
static void dy_render(
    box_audio_core_t *core,
    int32_t at,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i += BOX_AUDIO_CORE_QUANTUM) {
        int32_t n = frames - i < BOX_AUDIO_CORE_QUANTUM ? frames - i : BOX_AUDIO_CORE_QUANTUM;
        box_audio_core_render(core, dy_l + at + i, dy_r + at + i, n);
    }
}

/** Create a core with bus 1 into the master, render a frame of silence so that the effects
 * put on before enter at once, and play dy_sound on bus 1 as sound 1.
 *
 * @param core A new core with its effects put on
 * @param frames Frames of dy_sound
 * @param loop Whether it loops
 */
static void dy_play(
    box_audio_core_t *core,
    int32_t frames,
    bool loop)
{
    box_audio_core_render(core, dy_silence, dy_silence + 1, 1);
    test_assert(box_audio_core_sound(core, 1, dy_sound, frames, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, loop ? BOX_AUDIO_PLAY_LOOP : 0);
}

/** A new core with bus 1 into the master.
 *
 * @return The core
 */
static box_audio_core_t* dy_core(void)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    audio_order(core, routes, 2);

    return core;
}

/** The gain in dB of a compressor on bus 1 for a constant input level, once settled.
 *
 * @param p The parameters
 * @param level Input level in dB
 * @return The gain in dB
 */
static double dy_static_gain(
    box_audio_compressor_params_t p,
    double level)
{
    box_audio_core_t *core = dy_core();
    int32_t frames = AUDIO_RATE * 3 / 10;
    float in = dy_linear(level);
    double result;
    dy_compressor(core, 0, 1, BOX_AUDIO_BUS_NONE, p);
    audio_fill(dy_sound, AUDIO_RATE, in);
    dy_play(core, AUDIO_RATE, true);
    dy_render(core, 0, frames);
    result = dy_db(dy_l[frames - 1] / in);
    box_audio_core_free(core);

    return result;
}

void Dynamics_static_curve(void) {
    // audio/dynamics
    int32_t m;
    for (m = 0; m < 2; m ++) {
        float makeup = m ? 3.0f : 0.0f;
        box_audio_core_t *core = dy_core();
        double out;
        dy_compressor(core, 0, 1, BOX_AUDIO_BUS_NONE,
            dy_comp(-20, 4, 0.01f, 0.15f, 0, makeup, 1));
        audio_sine(dy_sound, AUDIO_RATE, 1000, dy_linear(-10));
        dy_play(core, AUDIO_RATE, true);
        dy_render(core, 0, AUDIO_RATE / 2);
        out = dy_db(audio_peak(dy_l, AUDIO_RATE * 3 / 10, AUDIO_RATE / 2));
        printf("a -10 dBFS sine at -20 dB 4:1 with %.0f dB makeup: %.3f dB\n",
            (double)makeup, out);
        test_assert(fabs(out - (-17.5 + makeup)) < 0.1);
        test_assert(fabs(dy_db(audio_peak(dy_r, AUDIO_RATE * 3 / 10, AUDIO_RATE / 2)) - out) <
            1e-4);
        box_audio_core_free(core);
    }
}

void Dynamics_knee_smooth(void) {
    // audio/dynamics
    box_audio_compressor_params_t p = dy_comp(-20, 4, 0.001f, 0.05f, 12, 0, 1);
    double out[49], slope, last = 1, most = 0, at;
    int32_t i;
    for (i = 0; i < 49; i ++) {
        double level = -32 + 0.5 * i;
        out[i] = level + dy_static_gain(p, level);
    }

    for (i = 0; i < 48; i ++) {
        slope = (out[i + 1] - out[i]) / 0.5;
        if (i) {
            test_assert(slope <= last + 1e-3);
            most = fabs(slope - last) > most ? fabs(slope - last) : most;
        } else {
            test_assert(slope > 0.99);
        }

        last = slope;
    }

    at = out[24] - (-20);
    printf("knee of 12 dB at -20 dB 4:1: %.3f dB at the threshold, slope from %.3f to %.3f, "
        "largest change of the slope per 0.5 dB %.3f\n", at,
        (out[1] - out[0]) / 0.5, last, most);
    test_assert(last < 0.26 && last > 0.24);
    test_assert(most < 0.05);
    test_assert(fabs(at + 1.125) < 0.05);
    test_assert(fabs(out[12] - out[11] - 0.5) < 0.01);
    test_assert(fabs(out[36] - (-14 - 0.75 * 6)) < 0.05);

    /* A hard knee bends at once */
    p.knee = 0;
    test_assert(fabs(dy_static_gain(p, -21)) < 0.01);
    test_assert(fabs(dy_static_gain(p, -16) + 3) < 0.05);
}

void Dynamics_attack_release(void) {
    // audio/dynamics
    const int32_t seg = AUDIO_RATE / 2;
    const float attack = 0.01f, release = 0.15f;
    box_audio_core_t *core = dy_core();
    double before, after, done, g;
    int32_t i;
    dy_compressor(core, 0, 1, BOX_AUDIO_BUS_NONE, dy_comp(-40, 4, attack, release, 0, 0, 1));
    for (i = 0; i < seg * 3; i ++) {
        dy_sound[i] = i >= seg && i < 2 * seg ? dy_linear(-10) : dy_linear(-30);
    }

    dy_play(core, seg * 3, false);
    dy_render(core, 0, seg * 3);

    /* Up: -7.5 dB to -22.5 dB */
    before = dy_db(dy_l[seg - 1] / dy_sound[seg - 1]);
    after = dy_db(dy_l[2 * seg - 1] / dy_sound[2 * seg - 1]);
    g = dy_db(dy_l[seg + (int32_t)(attack * AUDIO_RATE)] / dy_sound[seg]);
    done = (g - before) / (after - before);
    printf("attack: %.3f dB to %.3f dB, %.3f of the way after %.0f ms\n", before, after, done,
        (double)attack * 1000);
    test_assert(fabs(before + 7.5) < 0.05);
    test_assert(fabs(after + 22.5) < 0.05);
    test_assert(done > 0.58 && done < 0.68);

    /* Down: the release, after the window of the detector */
    before = after;
    after = -7.5;
    g = dy_db(dy_l[2 * seg + (int32_t)(release * AUDIO_RATE)] / dy_sound[2 * seg]);
    done = (g - before) / (after - before);
    printf("release: %.3f of the way after %.0f ms\n", done, (double)release * 1000);
    test_assert(done > 0.5 && done < 0.68);
    box_audio_core_free(core);
}

/** Render music on bus 1, a constant 0.1 panned left, compressed with bus 2 as its key, which
 * plays a burst of 0.5 panned right from 0.3 s for 0.3 s.
 *
 * @param routes The order: buses 1 and 2 into the master
 * @param count Routes of the order
 * @param p The parameters of the compressor
 * @return The core, rendered for 1.5 s into dy_l and dy_r
 */
static box_audio_core_t* dy_duck(
    const box_audio_route_t *routes,
    int32_t count,
    box_audio_compressor_params_t p)
{
    box_audio_core_t *core = audio_new();
    const int32_t start = AUDIO_RATE * 3 / 10, length = AUDIO_RATE * 3 / 10;
    audio_order(core, routes, count);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, -1);
    audio_bus_set(core, 2, BOX_AUDIO_PARAM_PAN, 1);
    dy_compressor(core, 0, 1, 2, p);
    audio_fill(dy_sound, AUDIO_RATE, 0.1f);
    audio_fill(dy_key, length, 0.5f);
    dy_play(core, AUDIO_RATE, true);
    test_assert(box_audio_core_sound(core, 2, dy_key, length, 1, AUDIO_RATE));
    dy_render(core, 0, start);
    audio_play(core, 2, 2, 2, 1, 0, 1, 0);
    dy_render(core, start, AUDIO_RATE * 3 / 2 - start);

    return core;
}

void Dynamics_sidechain_duck(void) {
    // audio/dynamics
    box_audio_route_t routes[] = { AUDIO_OUT(2, 0), AUDIO_OUT(1, 0), AUDIO_MASTER };
    const int32_t start = AUDIO_RATE * 3 / 10, end = AUDIO_RATE * 6 / 10;
    box_audio_core_t *core = dy_duck(routes, 3, dy_comp(-15, 10, 0.001f, 0.05f, 0, 0, 1));
    double dry = dy_l[start - 1], key, duck, expected, early, late;
    test_assert(dry > 0.1);
    test_flt(dy_r[start - 1], 0);
    key = dy_db(dy_r[end - AUDIO_RATE / 10]);
    expected = (1.0 / 10 - 1) * (key + 15);
    duck = dy_db(dy_l[end - AUDIO_RATE / 10] / dry);
    early = dy_db(dy_l[end + AUDIO_RATE / 50] / dry);
    late = dy_db(dy_l[end + AUDIO_RATE * 4 / 10] / dry);
    printf("a key of %.2f dB ducks the music by %.3f dB (%.3f expected), %.3f dB 20 ms after "
        "it, %.3f dB 400 ms after\n", key, duck, expected, early, late);
    test_assert(fabs(duck - expected) < 0.2);
    test_assert(fabs(dy_db(dy_l[start - 1] / dry)) < 1e-4);
    test_assert(early < -1);
    test_assert(fabs(late) < 0.05);
    box_audio_core_free(core);
}

void Dynamics_key_after_own_input(void) {
    // audio/dynamics
    box_audio_route_t after[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    box_audio_route_t gone[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_compressor_params_t p = dy_comp(-15, 10, 0.001f, 0.05f, 0, 0, 1);
    const int32_t end = AUDIO_RATE * 6 / 10;
    box_audio_core_t *core = dy_duck(after, 3, p);
    double dry = dy_l[AUDIO_RATE * 3 / 10 - 1];
    test_assert(dy_r[end - AUDIO_RATE / 10] > 0.5f);
    test_assert(fabs(dy_db(dy_l[end - AUDIO_RATE / 10] / dry)) < 1e-4);
    box_audio_core_free(core);

    /* A key bus that left the order, as a deleted bus does, keys on the own input too */
    core = dy_duck(gone, 2, p);
    test_assert(fabs(dy_db(dy_l[end - AUDIO_RATE / 10] / dy_l[AUDIO_RATE * 3 / 10 - 1])) <
        1e-4);
    test_flt(dy_r[end - AUDIO_RATE / 10], 0);
    box_audio_core_free(core);
}

void Dynamics_gate_closes_opens(void) {
    // audio/dynamics
    const int32_t open = AUDIO_RATE * 3 / 10, closed = AUDIO_RATE / 2;
    const float attack = 0.001f;
    box_audio_core_t *core = dy_core();
    box_audio_gate_params_t p = dy_gate(-40, attack, 0.05f, 0.02f, -60);
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    double held, shut, rise, back;
    int32_t i, at;
    dy_effect(core, 0, BOX_AUDIO_EFFECT_GATE, 1, 0, BOX_AUDIO_BUS_NONE, &p, sizeof(p));
    for (i = 0; i < open * 2 + closed; i ++) {
        dy_sound[i] = i >= open && i < open + closed ? dy_linear(-60) : dy_linear(-20);
    }

    dy_play(core, open * 2 + closed, false);
    dy_render(core, 0, open + closed);
    test_assert(box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX) > 0);
    dy_render(core, open + closed, open);
    test_assert(fabs(dy_db(dy_l[open - 1] / dy_sound[open - 1])) < 1e-4);
    held = dy_db(dy_l[open + AUDIO_RATE * 4 / 100] / dy_sound[open]);
    shut = dy_db(dy_l[open + AUDIO_RATE * 25 / 100] / dy_sound[open]);
    at = open + closed;
    rise = dy_l[at + (int32_t)(attack * AUDIO_RATE)] / dy_sound[at];
    back = dy_db(dy_l[at + AUDIO_RATE / 100] / dy_sound[at]);
    printf("gate: %.3f dB in its hold, %.2f dB closed, %.3f of open after its attack, %.4f dB "
        "10 ms after, reduction %.2f dB\n", held, shut, rise, back, (double)meters[0].reduction);
    test_assert(fabs(held) < 0.01);
    test_assert(fabs(shut + 60) < 1);
    test_assert(rise > 0.55 && rise < 0.7);
    test_assert(fabs(back) < 0.01);
    test_assert(fabs(meters[0].reduction - 60) < 1);
    box_audio_core_free(core);
}

void Dynamics_gate_no_chatter(void) {
    // audio/dynamics
    box_audio_gate_params_t p = dy_gate(-30, 0.001f, 0.01f, 0.01f, -80);
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    box_audio_core_t *core;
    int32_t i, pass;
    for (i = 0; i < AUDIO_RATE; i ++) {
        double t = (double)i / AUDIO_RATE;
        double level = -30 + 1.5 * cos(2 * AUDIO_PI * 5 * t);
        dy_sound[i] = dy_linear(level) * (float)sin(2 * AUDIO_PI * 1000 * t);
    }

    for (pass = 0; pass < 2; pass ++) {
        core = dy_core();
        if (pass) {
            dy_effect(core, 0, BOX_AUDIO_EFFECT_GATE, 1, 0, BOX_AUDIO_BUS_NONE, &p, sizeof(p));
        }

        dy_play(core, AUDIO_RATE, false);
        dy_render(core, 0, AUDIO_RATE);
        if (!pass) {
            memcpy(dy_b_l, dy_l, sizeof(float) * AUDIO_RATE);
        } else {
            box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
        }

        box_audio_core_free(core);
    }

    printf("a sine that wanders 1.5 dB around the threshold: reduction %.4f dB\n",
        (double)meters[0].reduction);
    test_assert(audio_hash(dy_l, AUDIO_RATE) == audio_hash(dy_b_l, AUDIO_RATE));
    test_flt(meters[0].reduction, 0);
}

void Dynamics_reduction(void) {
    // audio/dynamics
    box_audio_core_t *core = dy_core();
    box_audio_effect_meter_t meters[BOX_AUDIO_EFFECT_MAX];
    float none = 0;
    dy_compressor(core, 3, 1, BOX_AUDIO_BUS_NONE, dy_comp(-20, 4, 0.001f, 0.05f, 0, 6, 1));
    audio_fill(dy_sound, AUDIO_RATE / 2, dy_linear(-10));
    dy_play(core, AUDIO_RATE / 2, false);
    dy_render(core, 0, AUDIO_RATE / 4);
    test_int(box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX),
        BOX_AUDIO_EFFECT_MAX);
    printf("reduction of 4:1 at 10 dB over: %.3f dB\n", (double)meters[3].reduction);
    test_assert(fabs(meters[3].reduction - 7.5) < 0.05);
    test_flt(meters[0].reduction, 0);
    dy_render(core, 0, AUDIO_RATE / 10);
    box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    test_assert(fabs(meters[3].reduction - 7.5) < 0.05);

    /* After the sound ended and the level fell under the threshold */
    dy_render(core, 0, AUDIO_RATE);
    box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    dy_render(core, 0, AUDIO_RATE / 10);
    box_audio_core_effect_meters(core, meters, BOX_AUDIO_EFFECT_MAX);
    test_flt(meters[3].reduction, none);
    box_audio_core_free(core);
}

/** Render a -20 dBFS sine of 1 kHz through a compressor and a peak of +12 dB at 1 kHz on bus
 * 1, in an order of their insert slots.
 *
 * @param compressor_first Whether the compressor has insert slot 0
 * @return The peak level of the output in dB, once settled
 */
static double dy_slots(
    bool compressor_first)
{
    box_audio_core_t *core = dy_core();
    box_audio_compressor_params_t c = dy_comp(-20, 4, 0.001f, 0.05f, 0, 0, 1);
    box_audio_eq_band_t band = { (float)BOX_AUDIO_EQ_PEAK, 1000, 12, 1 };
    double result;
    dy_effect(core, 0, BOX_AUDIO_EFFECT_COMPRESSOR, 1, compressor_first ? 0 : 1,
        BOX_AUDIO_BUS_NONE, &c, sizeof(c));
    dy_effect(core, 1, BOX_AUDIO_EFFECT_EQ, 1, compressor_first ? 1 : 0, BOX_AUDIO_BUS_NONE,
        &band, sizeof(band));
    audio_sine(dy_sound, AUDIO_RATE, 1000, dy_linear(-20));
    dy_play(core, AUDIO_RATE, true);
    dy_render(core, 0, AUDIO_RATE / 2);
    result = dy_db(audio_peak(dy_l, AUDIO_RATE * 3 / 10, AUDIO_RATE / 2));
    box_audio_core_free(core);

    return result;
}

void Dynamics_slot_order(void) {
    // audio/dynamics
    double first = dy_slots(true), second = dy_slots(false);
    printf("a -20 dB sine: compressor then +12 dB peak %.3f dB, peak then compressor %.3f dB\n",
        first, second);
    test_assert(fabs(first + 8) < 0.3);
    test_assert(fabs(second + 17) < 0.3);
}

/** Render a scene of a keyed compressor and a gate in pieces of a size, after a frame of
 * silence in which the effects enter.
 *
 * @param left Receives the left channel
 * @param right Receives the right channel
 * @param size Frames of each render, 0 for sizes that vary
 */
static void dy_sizes(
    float *left,
    float *right,
    int32_t size)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(2, 0), AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_gate_params_t g = dy_gate(-30, 0.002f, 0.02f, 0.03f, -40);
    int32_t at = 0, k = 0;
    audio_order(core, routes, 3);
    dy_compressor(core, 0, 1, 2, dy_comp(-30, 6, 0.003f, 0.08f, 6, 4, 0.8f));
    dy_effect(core, 1, BOX_AUDIO_EFFECT_GATE, 2, 0, BOX_AUDIO_BUS_NONE, &g, sizeof(g));
    box_audio_core_render(core, dy_silence, dy_silence + 1, 1);
    audio_sine(dy_sound, AUDIO_RATE, 220, 0.3f);
    audio_sine(dy_key, AUDIO_RATE / 3, 90, 0.6f);
    test_assert(box_audio_core_sound(core, 1, dy_sound, AUDIO_RATE, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, dy_key, AUDIO_RATE / 3, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 2, 2, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    while (at < AUDIO_RATE) {
        int32_t n = size ? size : 1 + (k * 97) % 613;
        n = n < AUDIO_RATE - at ? n : AUDIO_RATE - at;
        box_audio_core_render(core, left + at, right + at, n);
        at += n;
        k ++;
    }

    box_audio_core_free(core);
}

void Dynamics_render_sizes(void) {
    // audio/dynamics
    dy_sizes(dy_l, dy_r, BOX_AUDIO_CORE_QUANTUM);
    dy_sizes(dy_b_l, dy_b_r, 0);
    test_assert(audio_hash(dy_l, AUDIO_RATE) == audio_hash(dy_b_l, AUDIO_RATE));
    test_assert(audio_hash(dy_r, AUDIO_RATE) == audio_hash(dy_b_r, AUDIO_RATE));
    test_assert(audio_peak(dy_l, 0, AUDIO_RATE) > 0.01f);
}

void Dynamics_component(void) {
    // audio/dynamics
    ecs_world_t *world = audio_world();
    ecs_entity_t comp = ecs_new(world), gate = ecs_new(world);
    const Compressor *c;
    const Gate *g;
    ecs_add(world, comp, Compressor);
    ecs_add(world, gate, Gate);
    c = ecs_get(world, comp, Compressor);
    test_flt(c->threshold, -18);
    test_flt(c->ratio, 4);
    test_flt(c->attack, 0.01f);
    test_flt(c->release, 0.15f);
    test_flt(c->knee, 6);
    test_flt(c->makeup, 0);
    test_flt(c->mix, 1);
    test_assert(c->sidechain == 0);
    g = ecs_get(world, gate, Gate);
    test_flt(g->threshold, -50);
    test_flt(g->attack, 0.001f);
    test_flt(g->hold, 0.05f);
    test_flt(g->release, 0.1f);
    test_flt(g->range, -80);
    test_assert(ecs_has(world, comp, AudioEffectState));
    test_assert(ecs_has(world, gate, AudioEffectState));
    test_int(box_audio_module_effect_kind(world, comp, NULL), BOX_AUDIO_EFFECT_COMPRESSOR);
    test_int(box_audio_module_effect_kind(world, gate, NULL), BOX_AUDIO_EFFECT_GATE);
    ecs_fini(world);
}

/** The key sent for a compressor entity.
 *
 * @param world The world
 * @param e The compressor entity
 * @return The key bus sent, BOX_AUDIO_BUS_NONE for none
 */
static uint32_t dy_sent_key(
    ecs_world_t *world,
    ecs_entity_t e)
{
    int32_t slot = box_audio_module_effect_slot(world, e);
    test_assert(slot >= 0);

    return box_audio_module_get(world)->effects[slot].key;
}

void Dynamics_world_duck(void) {
    // audio/dynamics
    ecs_world_t *world = audio_world();
    ecs_entity_t group = audio_bus(world, "music_group", 0, 1);
    ecs_entity_t music = audio_bus(world, "music", group, 1);
    ecs_entity_t sfx = audio_bus(world, "sfx", 0, 1);
    ecs_entity_t duck = ecs_entity(world, { .name = "music_duck" });
    ecs_entity_t bed = audio_dc(world, "bed", 0.1f, AUDIO_RATE);
    ecs_entity_t burst = audio_dc(world, "burst", 0.5f, AUDIO_RATE / 2);
    const AudioMeter *m;
    float dry, ducked, back;
    ecs_set(world, duck, Compressor, { .threshold = -15, .ratio = 10, .attack = 0.001f,
        .release = 0.05f, .knee = 0, .makeup = 0, .mix = 1, .sidechain = sfx });
    ecs_add(world, duck, AudioMeter);
    ecs_add(world, music, AudioMeter);
    audio_insert(world, music, 0, duck);
    audio_source(world, bed, 1, true, music);
    audio_run(world, 10);
    test_assert(audio_position(world, sfx) < audio_position(world, music));
    test_assert(dy_sent_key(world, duck) == box_audio_bus_of(world,
        box_audio_module_get(world), sfx));
    m = ecs_get(world, music, AudioMeter);
    dry = m->peak_left;
    test_assert(fabs(dry - 0.1f) < 1e-3f);
    test_flt(ecs_get(world, duck, AudioMeter)->reduction, 0);

    audio_source(world, burst, 1, false, sfx);
    audio_run(world, 10);
    ducked = ecs_get(world, music, AudioMeter)->peak_left;
    printf("music under a burst of -6 dB on the sfx bus: %.3f dB, reduction %.3f dB\n",
        dy_db(ducked / dry), (double)ecs_get(world, duck, AudioMeter)->reduction);
    test_assert(fabs(dy_db(ducked / dry) - 0.9 * (-15 - dy_db(0.5))) < 0.3);
    test_assert(fabs(ecs_get(world, duck, AudioMeter)->reduction + dy_db(ducked / dry)) <
        0.3);

    audio_run(world, 60);
    back = ecs_get(world, music, AudioMeter)->peak_left;
    test_assert(fabs(dy_db(back / dry)) < 0.05);
    test_assert(ecs_get(world, duck, AudioMeter)->reduction < 0.01f);

    /* A deleted key falls back to the own input, without an error */
    audio_log_start();
    ecs_delete(world, sfx);
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(dy_sent_key(world, duck), BOX_AUDIO_BUS_NONE);
    ecs_fini(world);
}

void Dynamics_sidechain_cycle(void) {
    // audio/dynamics
    ecs_world_t *world = audio_world();
    ecs_entity_t sub = audio_bus(world, "dialog_bus", 0, 1);
    ecs_entity_t dialog = audio_bus(world, "voice_bus", sub, 1);
    ecs_entity_t duck = ecs_entity(world, { .name = "voice_duck" });
    ecs_set(world, duck, Compressor, { .threshold = -15, .ratio = 10, .attack = 0.001f,
        .release = 0.05f, .knee = 0, .makeup = 0, .mix = 1, .sidechain = sub });
    audio_insert(world, dialog, 0, duck);
    audio_log_start();
    audio_run(world, 5);
    audio_log_stop();
    printf("%s\n", audio_log_last);
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "voice_bus") != NULL);
    test_assert(strstr(audio_log_last, "dialog_bus") != NULL);
    test_int(dy_sent_key(world, duck), BOX_AUDIO_BUS_NONE);
    test_assert(audio_position(world, dialog) < audio_position(world, sub));

    /* Another change of the order does not log it again */
    audio_bus(world, "other_bus", 0, 1);
    audio_log_start();
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 0);

    /* Once the route goes, the sidechain applies */
    ecs_set(world, dialog, AudioBus, { .output = 0, .volume = 1, .width = 1,
        .effects = { duck } });
    audio_log_start();
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(dy_sent_key(world, duck), box_audio_bus_of(world, box_audio_module_get(world),
        sub));
    test_assert(audio_position(world, sub) < audio_position(world, dialog));
    ecs_fini(world);
}

void Dynamics_script(void) {
    // audio/dynamics
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/dynamics.flecs");
    ecs_entity_t music, sfx, duck, gate;
    const Compressor *c;
    test_assert(ecs_script_run(world, "dynamics", code, NULL) == 0);
    ecs_os_free(code);
    music = ecs_lookup(world, "music");
    sfx = ecs_lookup(world, "sfx");
    duck = ecs_lookup(world, "music_duck");
    gate = ecs_lookup(world, "dialog_gate");
    test_assert(music && sfx && duck && gate);
    c = ecs_get(world, duck, Compressor);
    test_assert(c != NULL);
    test_assert(c->sidechain == sfx);
    test_flt(c->threshold, -30);
    test_flt(c->ratio, 8);
    test_flt(c->knee, 6);
    test_flt(ecs_get(world, gate, Gate)->hold, 0.08f);
    test_flt(ecs_get(world, gate, Gate)->range, -80);

    audio_source(world, audio_dc(world, NULL, 0.05f, AUDIO_RATE), 1, true, music);
    audio_source(world, audio_dc(world, NULL, 0.3f, AUDIO_RATE), 1, true, sfx);
    audio_run(world, 20);
    test_assert(box_audio_module_effect_slot(world, duck) >= 0);
    test_assert(box_audio_module_effect_slot(world, gate) >= 0);
    test_assert(dy_sent_key(world, duck) != BOX_AUDIO_BUS_NONE);
    printf("the music ducks under the sfx bus by %.2f dB\n",
        (double)ecs_get(world, duck, AudioMeter)->reduction);
    test_assert(fabs(ecs_get(world, duck, AudioMeter)->reduction -
        (1 - 1.0 / 8) * (dy_db(0.3) + 30)) < 0.5);
    ecs_fini(world);
}
