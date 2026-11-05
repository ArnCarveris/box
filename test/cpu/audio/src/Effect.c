/**
 * @file Effect.c
 * @brief The effect pool of the core and the insert slots of the channels: several effects at
 * once, the order of the slots, the fades of a removal and of a move, the pool of 64, a channel
 * without effects, an entity in two slots and a disabled effect.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, two seconds at 48 kHz */
#define FX_MAX (AUDIO_RATE * 2)

/** Frame of the click in its sound, after the start ramp of the voice */
#define FX_CLICK 300

/** Frames of the fade of a move or a removal, out and in */
#define FX_FADE ((int32_t)(BOX_AUDIO_EFFECT_FADE * AUDIO_RATE))

/** Output of the last render */
static float fx_l[FX_MAX];
static float fx_r[FX_MAX];

/** Output of the render of a second core */
static float fx_l2[FX_MAX];
static float fx_r2[FX_MAX];

/** The click: silence, then one sample of 1 */
static float fx_click[FX_CLICK + 1];

/** A core with buses that output to the master and the memory of its effect slots */
typedef struct fx_core_t {
    box_audio_core_t *core;  /**< The core */
    float *memory[8];        /**< Memory of slots 0 to 7, NULL for none */
} fx_core_t;

/** The parameters of a reverb in the params of an effect command.
 *
 * @param params Receives BOX_AUDIO_EFFECT_PARAMS floats
 * @param decay Seconds until -60 dB
 * @param mix Wet level
 */
static void fx_reverb(
    float *params,
    float decay,
    float mix)
{
    box_audio_reverb_params_t p = { decay, 0.5f, 0.01f, 0.5f, mix };
    memset(params, 0, sizeof(float) * BOX_AUDIO_EFFECT_PARAMS);
    memcpy(params, &p, sizeof(p));
}

/** Send an effect command.
 *
 * @param core The core
 * @param slot Slot
 * @param kind Kind
 * @param bus Bus
 * @param index Insert slot within the bus
 * @param params Parameters
 */
static void fx_command(
    box_audio_core_t *core,
    uint32_t slot,
    uint32_t kind,
    uint32_t bus,
    uint32_t index,
    const float *params)
{
    uint8_t bytes[256];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, slot, kind, bus, index, BOX_AUDIO_BUS_NONE,
        params));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with buses 1 to count into the master, the click as sound 1, a sine of 440 Hz
 * at 0.25 as sound 2, and reverb memory for slots 0 to memories - 1.
 *
 * @param fx Receives the core
 * @param count Buses besides the master
 * @param memories Slots that get memory, at most 8
 */
static void fx_open(
    fx_core_t *fx,
    int32_t count,
    int32_t memories)
{
    box_audio_core_desc_t desc = { .sample_rate = AUDIO_RATE };
    box_audio_route_t routes[8];
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_REVERB, AUDIO_RATE), i;
    memset(fx, 0, sizeof(*fx));
    fx->core = box_audio_core_new(&desc);
    test_assert(fx->core != NULL);
    for (i = 0; i < count; i ++) {
        routes[i] = (box_audio_route_t)AUDIO_OUT((uint8_t)(i + 1), 0);
    }

    routes[count] = (box_audio_route_t)AUDIO_MASTER;
    audio_order(fx->core, routes, count + 1);
    for (i = 0; i < memories; i ++) {
        fx->memory[i] = calloc((size_t)floats, sizeof(float));
        test_assert(fx->memory[i] != NULL);
        test_assert(box_audio_core_effect_memory(fx->core, (uint32_t)i, fx->memory[i], floats));
    }

    memset(fx_click, 0, sizeof(fx_click));
    fx_click[FX_CLICK] = 1;
    test_assert(box_audio_core_sound(fx->core, 1, fx_click, FX_CLICK + 1, 1, AUDIO_RATE));
    audio_sine(audio_sound_a, AUDIO_RATE, 440, 0.25f);
    test_assert(box_audio_core_sound(fx->core, 2, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
}

/** Free the core, then the memory of its slots.
 *
 * @param fx The core
 */
static void fx_close(
    fx_core_t *fx)
{
    int32_t i;
    box_audio_core_free(fx->core);
    for (i = 0; i < 8; i ++) {
        free(fx->memory[i]);
    }
}

/** Render frames from a frame on, in quanta of the web.
 *
 * @param core The core
 * @param l Left output
 * @param r Right output
 * @param at First frame
 * @param frames Number of frames
 */
static void fx_render(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t at,
    int32_t frames)
{
    int32_t i;
    test_assert(at + frames <= FX_MAX);
    for (i = 0; i < frames; i += BOX_AUDIO_CORE_QUANTUM) {
        int32_t n = frames - i < BOX_AUDIO_CORE_QUANTUM ? frames - i : BOX_AUDIO_CORE_QUANTUM;
        box_audio_core_render(core, l + at + i, r + at + i, n);
    }
}

/** Largest change between neighbouring samples of both channels in a range.
 *
 * @param from First frame, above 0
 * @param to End frame
 * @return The change
 */
static float fx_step(
    int32_t from,
    int32_t to)
{
    float l = audio_max_step(fx_l, from, to), r = audio_max_step(fx_r, from, to);

    return l > r ? l : r;
}

void Effect_four_reverbs_run(void) {
    // audio/effect
    fx_core_t fx;
    float params[BOX_AUDIO_EFFECT_PARAMS];
    int32_t i;
    fx_open(&fx, 4, 4);
    for (i = 0; i < 4; i ++) {
        fx_reverb(params, 0.5f + 0.5f * (float)i, 1);
        fx_command(fx.core, (uint32_t)i, BOX_AUDIO_EFFECT_REVERB, (uint32_t)(i + 1), 0, params);
        test_assert(!box_audio_core_effect_running(fx.core, (uint32_t)i));
        audio_play(fx.core, (uint32_t)(i + 1), 1, (uint32_t)(i + 1), 0.25f, 0, 1, 0);
    }

    fx_render(fx.core, fx_l, fx_r, 0, AUDIO_RATE / 4);
    for (i = 0; i < 4; i ++) {
        test_assert(box_audio_core_effect_running(fx.core, (uint32_t)i));
    }

    // Every bus is fully wet, so the click reaches the output only through the reverbs
    test_flt(fx_l[FX_CLICK], 0);
    test_assert(audio_peak(fx_l, FX_CLICK + 1, AUDIO_RATE / 4) > 0.001f);
    test_assert(audio_peak(fx_r, FX_CLICK + 1, AUDIO_RATE / 4) > 0.001f);
    test_int(audio_events(fx.core, BOX_AUDIO_EVENT_IGNORED, NULL), 0);
    fx_close(&fx);
}

void Effect_empty_slots_skipped(void) {
    // audio/effect
    ecs_world_t *world = audio_world();
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t verb = ecs_entity(world, { .name = "verb" });
    ecs_entity_t click;
    double tail = 0;
    int32_t i, start;
    ecs_set(world, verb, Reverb, { .decay = 1, .damping = 0.5f, .predelay = 0.01f,
        .size = 0.5f, .mix = 1 });
    audio_insert(world, hall, 2, verb);
    audio_run(world, 2);
    test_int(box_audio_module_effect_slot(world, verb), 0);
    test_assert(ecs_get(world, verb, AudioEffectState)->index == 2);

    memset(fx_click, 0, sizeof(fx_click));
    fx_click[FX_CLICK] = 0.5f;
    click = box_audio_sound_from_pcm(world, 0, fx_click, FX_CLICK + 1, 1, AUDIO_RATE);
    start = audio_tap_count;
    audio_source(world, click, 1, false, hall);
    audio_run(world, 20);
    for (i = start + FX_CLICK + 1; i < audio_tap_count; i ++) {
        tail += (double)audio_tap_l[i] * audio_tap_l[i];
    }

    printf("tail energy of the reverb in slot 2: %g\n", tail);
    test_assert(tail > 0);
    ecs_fini(world);
}

void Effect_remove_fades_tail(void) {
    // audio/effect
    fx_core_t fx;
    float params[BOX_AUDIO_EFFECT_PARAMS], before, after;
    int32_t i, at = AUDIO_RATE * 5 / 4;
    fx_open(&fx, 1, 1);
    fx_reverb(params, 3, 1);
    fx_command(fx.core, 0, BOX_AUDIO_EFFECT_REVERB, 1, 0, params);
    audio_play(fx.core, 1, 2, 1, 1, 0, 1, 0);
    fx_render(fx.core, fx_l, fx_r, 0, at);
    before = fx_step(AUDIO_RATE + AUDIO_RATE / 20, at);
    test_assert(audio_peak(fx_l, at - 1000, at) > 0.001f);

    // The sine of a second ended and the bus is silent, the tail rings: taking the reverb off
    // fades the tail out
    fx_command(fx.core, 0, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0, NULL);
    fx_render(fx.core, fx_l, fx_r, at, AUDIO_RATE / 2);
    after = fx_step(at, at + AUDIO_RATE / 2);
    printf("largest step: %.4f before, %.4f after the removal\n", (double)before,
        (double)after);
    test_assert(audio_peak(fx_l, at, at + FX_FADE / 4) > 0.0001f);
    test_assert(after <= before * 1.5f);
    test_assert(!box_audio_core_effect_running(fx.core, 0));
    for (i = at + FX_FADE; i < at + AUDIO_RATE / 2; i ++) {
        test_flt(fx_l[i], 0);
        test_flt(fx_r[i], 0);
    }

    fx_close(&fx);
}

void Effect_move_fades(void) {
    // audio/effect
    fx_core_t fx;
    float params[BOX_AUDIO_EFFECT_PARAMS], before, after;
    float dry = (float)(0.25 * 2 * AUDIO_PI * 440 / AUDIO_RATE);
    int32_t at = AUDIO_RATE / 2;
    fx_open(&fx, 2, 1);
    fx_reverb(params, 1.5f, 1);
    fx_command(fx.core, 0, BOX_AUDIO_EFFECT_REVERB, 1, 0, params);
    audio_play(fx.core, 1, 2, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(fx.core, 2, 2, 2, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    fx_render(fx.core, fx_l, fx_r, 0, at);
    before = fx_step(at / 2, at);
    fx_command(fx.core, 0, BOX_AUDIO_EFFECT_REVERB, 2, 0, params);
    fx_render(fx.core, fx_l, fx_r, at, AUDIO_RATE);
    after = fx_step(at, at + AUDIO_RATE);
    printf("largest step: %.4f before, %.4f after a move between buses\n", (double)before,
        (double)after);
    test_assert(after < before + dry + 0.01f);
    test_assert(box_audio_core_effect_running(fx.core, 0));
    test_assert(audio_peak(fx_l, at + AUDIO_RATE / 2, at + AUDIO_RATE) > 0.01f);
    fx_close(&fx);
}

void Effect_pool_full(void) {
    // audio/effect
    ecs_world_t *world = audio_world();
    ecs_entity_t buses[17], last = 0;
    int32_t b, j, n = 0;
    char name[32];
    for (b = 0; b < 17; b ++) {
        snprintf(name, sizeof(name), "bus_%d", b);
        buses[b] = audio_bus(world, name, 0, 1);
    }

    for (b = 0; b < 17 && n < BOX_AUDIO_EFFECT_MAX + 1; b ++) {
        for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS && n < BOX_AUDIO_EFFECT_MAX + 1; j ++) {
            snprintf(name, sizeof(name), "fx_%d", n);
            last = ecs_entity(world, { .name = name });
            ecs_add(world, last, Reverb);
            audio_insert(world, buses[b], j, last);
            n ++;
        }
    }

    test_int(n, 65);
    audio_log_start();
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "more than 64 effects") != NULL);
    test_assert(strstr(audio_log_last, "fx_64") != NULL);
    test_int(box_audio_module_effect_slot(world, last), -1);
    test_assert(box_audio_module_effect_slot(world, ecs_lookup(world, "fx_63")) >= 0);

    ecs_delete(world, ecs_lookup(world, "fx_0"));
    audio_log_start();
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_assert(box_audio_module_effect_slot(world, last) >= 0);
    ecs_fini(world);
}

void Effect_no_effects_identical(void) {
    // audio/effect
    fx_core_t a, b;
    float params[BOX_AUDIO_EFFECT_PARAMS];
    int32_t i;
    fx_open(&a, 2, 0);
    fx_open(&b, 2, 1);
    fx_reverb(params, 1.5f, 0.5f);
    fx_command(b.core, 0, BOX_AUDIO_EFFECT_REVERB, 2, 3, params);
    fx_command(b.core, 1, BOX_AUDIO_EFFECT_NONE, BOX_AUDIO_BUS_NONE, 0, NULL);
    audio_play(a.core, 1, 2, 1, 0.5f, 0.3f, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(b.core, 1, 2, 1, 0.5f, 0.3f, 1, BOX_AUDIO_PLAY_LOOP);
    audio_bus_set(a.core, 1, BOX_AUDIO_PARAM_WIDTH, 0.5f);
    audio_bus_set(b.core, 1, BOX_AUDIO_PARAM_WIDTH, 0.5f);
    fx_render(a.core, fx_l, fx_r, 0, AUDIO_RATE);
    fx_render(b.core, fx_l2, fx_r2, 0, AUDIO_RATE);
    for (i = 0; i < AUDIO_RATE; i ++) {
        test_assert(fx_l[i] == fx_l2[i]);
        test_assert(fx_r[i] == fx_r2[i]);
    }

    test_assert(!box_audio_core_effect_running(b.core, 0));
    fx_close(&a);
    fx_close(&b);
}

void Effect_two_slots_error(void) {
    // audio/effect
    ecs_world_t *world = audio_world();
    ecs_entity_t first = audio_bus(world, "first_channel", 0, 1);
    ecs_entity_t second = audio_bus(world, "second_channel", 0, 1);
    ecs_entity_t verb = ecs_entity(world, { .name = "verb" });
    const AudioEffectState *es;
    ecs_add(world, verb, Reverb);
    audio_insert(world, first, 0, verb);
    audio_insert(world, second, 1, verb);
    audio_log_start();
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "first_channel") != NULL);
    test_assert(strstr(audio_log_last, "second_channel") != NULL);
    test_assert(strstr(audio_log_last, "verb") != NULL);
    test_int(box_audio_module_effect_slot(world, verb), 0);
    es = ecs_get(world, verb, AudioEffectState);
    test_assert(es->channel == first);
    test_int(es->index, 0);

    audio_insert(world, first, 0, 0);
    audio_log_start();
    audio_run(world, 2);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    es = ecs_get(world, verb, AudioEffectState);
    test_assert(es->channel == second);
    test_int(es->index, 1);
    ecs_fini(world);
}

void Effect_self_slot_script(void) {
    // audio/effect
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/reverb.flecs");
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "effect_scene" }),
        .code = code
    });
    ecs_entity_t hall;
    ecs_os_free(code);
    test_assert(script != 0);
    hall = ecs_lookup(world, "hall");
    test_assert(hall != 0);
    test_assert(ecs_get(world, hall, AudioBus)->effects[0] == hall);
    audio_run(world, 2);
    test_int(box_audio_module_effect_slot(world, hall), 0);
    ecs_fini(world);
}

void Effect_unnamed_does_nothing(void) {
    // audio/effect
    ecs_world_t *world = audio_world();
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t click;
    int32_t i, start;
    ecs_add(world, hall, Reverb);
    audio_run(world, 2);
    test_int(box_audio_module_effect_slot(world, hall), -1);
    test_int(box_audio_module_stats(world).effects, 0);

    memset(fx_click, 0, sizeof(fx_click));
    fx_click[FX_CLICK] = 0.5f;
    click = box_audio_sound_from_pcm(world, 0, fx_click, FX_CLICK + 1, 1, AUDIO_RATE);
    start = audio_tap_count;
    audio_source(world, click, 1, false, hall);
    audio_run(world, 10);
    test_flt(audio_tap_l[start + FX_CLICK], 0.5f);
    for (i = start + FX_CLICK + 1; i < audio_tap_count; i ++) {
        test_flt(audio_tap_l[i], 0);
    }

    ecs_fini(world);
}

void Effect_disabled_bypassed(void) {
    // audio/effect
    ecs_world_t *world = audio_world();
    ecs_entity_t hall = audio_bus(world, "hall", 0, 1);
    ecs_entity_t verb = ecs_entity(world, { .name = "verb" });
    ecs_entity_t tone = audio_tone(world, "tone", 440, 0.25f, AUDIO_RATE);
    float before, after, peak, dry = (float)(0.25 * 2 * AUDIO_PI * 440 / AUDIO_RATE);
    int32_t at;
    ecs_set(world, verb, Reverb, { .decay = 2, .damping = 0.5f, .predelay = 0.01f,
        .size = 0.5f, .mix = 1 });
    audio_source(world, tone, 1, true, hall);
    audio_run(world, 10);
    peak = audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME * 4, audio_tap_count);
    audio_insert(world, hall, 0, verb);
    audio_run(world, 30);
    at = audio_tap_count;
    before = audio_max_step(audio_tap_l, at - AUDIO_FRAME * 10, at);
    ecs_enable(world, verb, false);
    audio_run(world, 30);
    after = audio_max_step(audio_tap_l, at, audio_tap_count);
    printf("largest step: %.4f before, %.4f after the effect was disabled\n", (double)before,
        (double)after);
    test_assert(after < before + dry + 0.01f);
    test_int(box_audio_module_effect_slot(world, verb), 0);

    // Bypassed, the bus passes the sine as it did without the effect
    test_assert(fabsf(audio_peak(audio_tap_l, audio_tap_count - AUDIO_FRAME * 4,
        audio_tap_count) - peak) < 0.001f);
    // Enabled again, it fades back in from the dry signal
    at = audio_tap_count;
    ecs_enable(world, verb, true);
    audio_run(world, 30);
    after = audio_max_step(audio_tap_l, at, audio_tap_count);
    printf("largest step: %.4f after the effect was enabled\n", (double)after);
    test_assert(after < before + dry + 0.01f);
    test_int(box_audio_module_effect_slot(world, verb), 0);
    ecs_fini(world);
}
