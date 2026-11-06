/**
 * @file MacroCore.c
 * @brief The macros of the audio core, rendered offline: the value of a map, a scheduled value
 * on exactly its frame and a cancel, and renders bit identical across render sizes.
 */

#include "util.h"
#include <internal.h>
#include <stdio.h>

/** Level of the constant sounds */
#define MC_LEVEL 0.5f

/** Cutoff of a map of 200..8000 Hz in octaves at a value of 0.5 */
#define MC_CUTOFF_HALF 1264.911f

/** Output of the core renders that a case compares */
static float mc_a_l[AUDIO_FRAMES], mc_a_r[AUDIO_FRAMES];
static float mc_b_l[AUDIO_FRAMES], mc_b_r[AUDIO_FRAMES];

/** Noise of the cases */
static float mc_noise[AUDIO_RATE];

/** Write a batch of one macro value into a core.
 *
 * @param core The core
 * @param macro Slot of the macro
 * @param value The value
 * @param when Frame, 0 for now
 */
static void mc_macro(
    box_audio_core_t *core,
    uint32_t macro,
    float value,
    uint64_t when)
{
    uint8_t bytes[64];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_macro(&w, macro, value, when));
    box_audio_core_apply(core, w.data, w.size);
}

/** Write a map command into a core.
 *
 * @param core The core
 * @param cmd The command
 */
static void mc_map(
    box_audio_core_t *core,
    const box_audio_cmd_macro_map_t *cmd)
{
    uint8_t bytes[128];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_macro_map(&w, cmd));
    box_audio_core_apply(core, w.data, w.size);
}

/** Write a modulator command into a core.
 *
 * @param core The core
 * @param cmd The command
 */
static void mc_modulator(
    box_audio_core_t *core,
    const box_audio_cmd_modulator_t *cmd)
{
    uint8_t bytes[512];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_modulator(&w, cmd));
    box_audio_core_apply(core, w.data, w.size);
}

/** Create a core with a lowpass at 1000 Hz in slot 0 on bus 1, whose send 0 goes to bus 2.
 *
 * @return The core
 */
static box_audio_core_t* mc_core(void)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_ROUTE(1, 0, 2), AUDIO_OUT(2, 0), AUDIO_MASTER };
    float params[BOX_AUDIO_EFFECT_PARAMS] = { BOX_AUDIO_SYNTH_LOWPASS, 1000, 0.2f, 0, 1, 0 };
    uint8_t bytes[512];
    box_audio_writer_t w;
    float silence[2];
    audio_order(core, routes, 3);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_FILTER, 1, 0,
        BOX_AUDIO_BUS_NONE, params));
    box_audio_core_apply(core, w.data, w.size);
    box_audio_core_render(core, silence, silence + 1, 1);

    return core;
}

/** The maps of a macro at 0.5 in the core: the cutoff of the lowpass at about 1265 Hz and the
 * send of bus 1 at 0.3, while the values of the commands stay; released, the maps fade out and
 * the parameters return to those values. */
void MacroCore_value(void) {
    // audio/macros
    box_audio_core_t *core = mc_core();
    box_audio_cmd_macro_map_t cutoff = { .slot = 0, .macro = 3,
        .target = BOX_AUDIO_MOD_TARGET_EFFECT, .index = 0, .param = 1,
        .kind = BOX_AUDIO_EFFECT_FILTER, .scale = BOX_AUDIO_MOD_OCTAVES, .min = 200,
        .max = 8000 };
    box_audio_cmd_macro_map_t send = { .slot = 7, .macro = 3,
        .target = BOX_AUDIO_MOD_TARGET_BUS, .index = 1, .param = BOX_AUDIO_PARAM_SEND,
        .min = 0, .max = 0.6f };
    box_audio_cmd_macro_map_t none = { .target = BOX_AUDIO_MOD_TARGET_NONE };
    mc_macro(core, 3, 0.5f, 0);
    mc_map(core, &cutoff);
    mc_map(core, &send);
    test_int(box_audio_core_macro_maps(core), 2);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    test_assert(fabsf(core->effects[0].params[1] - MC_CUTOFF_HALF) < 0.05f);
    test_flt(core->effects[0].base[1], 1000.0f);
    test_assert(fabsf(core->buses[1].send[0].value - 0.3f) < 1e-4f);
    test_flt(core->buses[1].send_level[0], 0.0f);
    mc_macro(core, 3, 0.0f, 0);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    test_assert(fabsf(core->effects[0].params[1] - 200.0f) < 0.01f);
    test_assert(fabsf(core->buses[1].send[0].value) < 1e-4f);
    none.slot = 0;
    mc_map(core, &none);
    none.slot = 7;
    mc_map(core, &none);
    test_int(box_audio_core_macro_maps(core), 2);
    audio_render(core, 0, AUDIO_FRAMES / 4);
    test_int(box_audio_core_macro_maps(core), 0);
    test_flt(core->effects[0].params[1], 1000.0f);
    test_assert(fabsf(core->buses[1].send[0].value) < 1e-6f);
    box_audio_core_free(core);
}

/** A value of a macro scheduled ahead moves the volume of its map from exactly its frame on,
 * and a cancel drops the values scheduled for a macro. */
void MacroCore_scheduled(void) {
    // audio/macros
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_cmd_macro_map_t volume = { .slot = 5, .macro = 0,
        .target = BOX_AUDIO_MOD_TARGET_BUS, .index = 1, .param = BOX_AUDIO_PARAM_VOLUME,
        .min = 0, .max = 1 };
    uint8_t bytes[64];
    box_audio_writer_t w;
    int32_t i;
    audio_order(core, routes, 2);
    audio_fill(audio_sound_a, AUDIO_RATE, MC_LEVEL);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    mc_macro(core, 0, 0.0f, 0);
    mc_map(core, &volume);
    audio_render(core, 0, 9984);
    mc_macro(core, 0, 1.0f, 10007);
    mc_macro(core, 0, 0.25f, 20011);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_cancel(&w, BOX_AUDIO_CMD_MACRO, 1, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 2);
    audio_render(core, 9984, 5000);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_cancel(&w, BOX_AUDIO_CMD_MACRO, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    test_int(box_audio_core_scheduled(core), 0);
    audio_render(core, 14984, 15000);
    for (i = 9000; i < 10007; i ++) {
        test_assert(fabsf(audio_l[i]) < 1e-6f);
    }

    test_assert(audio_l[10008] > 1e-5f);
    test_assert(fabsf(audio_l[14000] - MC_LEVEL) < 1e-4f);
    test_assert(fabsf(audio_l[25000] - MC_LEVEL) < 1e-4f);
    test_assert(fabsf(audio_l[29000] - MC_LEVEL) < 1e-4f);
    box_audio_core_free(core);
}

/** Set up a core with noise through the lowpass of bus 1 and a constant level on bus 2, maps
 * of two macros on the cutoff, the pan of bus 1 and the volume of bus 2, modulators on both
 * macros and values scheduled ahead.
 *
 * @return The core
 */
static box_audio_core_t* mc_core_busy(void)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = { AUDIO_OUT(2, 0), AUDIO_OUT(1, 0), AUDIO_MASTER };
    float params[BOX_AUDIO_EFFECT_PARAMS] = { BOX_AUDIO_SYNTH_LOWPASS, 1000, 0.2f, 0, 1, 0 };
    box_audio_cmd_macro_map_t map;
    box_audio_cmd_modulator_t m;
    uint8_t bytes[512];
    box_audio_writer_t w;
    float silence[2];
    uint32_t seed = 0x2468aceu;
    int32_t i;
    audio_order(core, routes, 3);
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_FILTER, 1, 0,
        BOX_AUDIO_BUS_NONE, params));
    test_assert(box_audio_write_beat(&w, 1.5, 2.0f, 0));
    box_audio_core_apply(core, w.data, w.size);
    box_audio_core_render(core, silence, silence + 1, 1);
    for (i = 0; i < AUDIO_RATE; i ++) {
        seed = seed * 1664525u + 1013904223u;
        mc_noise[i] = 0.25f * ((float)(seed >> 8) / 8388608.0f - 1.0f);
    }

    audio_fill(audio_sound_b, AUDIO_RATE, 0.3f);
    test_assert(box_audio_core_sound(core, 1, mc_noise, AUDIO_RATE, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, AUDIO_RATE, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 2, 2, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    mc_macro(core, 0, 0.3f, 0);
    mc_macro(core, 1, 0.6f, 0);
    map = (box_audio_cmd_macro_map_t){ .slot = 0, .macro = 0,
        .target = BOX_AUDIO_MOD_TARGET_EFFECT, .index = 0, .param = 1,
        .kind = BOX_AUDIO_EFFECT_FILTER, .scale = BOX_AUDIO_MOD_OCTAVES, .min = 300,
        .max = 6000, .curve = 0.3f };
    mc_map(core, &map);
    map = (box_audio_cmd_macro_map_t){ .slot = 1, .macro = 0,
        .target = BOX_AUDIO_MOD_TARGET_BUS, .index = 1, .param = BOX_AUDIO_PARAM_PAN,
        .min = 0.8f, .max = -0.8f, .curve = -0.5f };
    mc_map(core, &map);
    map = (box_audio_cmd_macro_map_t){ .slot = 2, .macro = 1,
        .target = BOX_AUDIO_MOD_TARGET_BUS, .index = 2, .param = BOX_AUDIO_PARAM_VOLUME,
        .min = 1, .max = 0.1f };
    mc_map(core, &map);
    m = (box_audio_cmd_modulator_t){ .slot = 0, .target = BOX_AUDIO_MOD_TARGET_MACRO,
        .index = 0, .source = BOX_AUDIO_MOD_LFO, .wave = BOX_AUDIO_MOD_SQUARE,
        .depth = 0.2f, .sync = 0.25f, .step_count = 1 };
    mc_modulator(core, &m);
    m = (box_audio_cmd_modulator_t){ .slot = 1, .target = BOX_AUDIO_MOD_TARGET_MACRO,
        .index = 1, .source = BOX_AUDIO_MOD_STEPS, .depth = 0.3f, .rate = 9,
        .step_count = 3, .smooth = 0.2f, .steps = { 1, -1, 0.5f } };
    mc_modulator(core, &m);
    mc_macro(core, 0, 0.9f, 7001);
    mc_macro(core, 1, 0.1f, 19013);
    mc_macro(core, 0, 0.05f, 30011);

    return core;
}

/** Render a core in blocks of a size into buffers.
 *
 * @param core The core
 * @param l Left output
 * @param r Right output
 * @param frames Frames
 * @param block Frames per render
 */
static void mc_render_blocks(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t frames,
    int32_t block)
{
    int32_t i;
    for (i = 0; i < frames; i += block) {
        int32_t n = frames - i < block ? frames - i : block;
        box_audio_core_render(core, l + i, r + i, n);
    }
}

/** Maps of two macros with modulators on both and values scheduled ahead: renders in blocks of
 * 128, 1, 37 and 1000 frames are bit identical. */
void MacroCore_render_sizes(void) {
    // audio/macros
    const int32_t blocks[] = { 1, 37, 1000 };
    box_audio_core_t *core = mc_core_busy();
    uint64_t a, b;
    int32_t k;
    test_int(box_audio_core_macro_maps(core), 3);
    mc_render_blocks(core, mc_a_l, mc_a_r, AUDIO_FRAMES, BOX_AUDIO_CORE_QUANTUM);
    a = audio_hash(mc_a_l, AUDIO_FRAMES) ^ (audio_hash(mc_a_r, AUDIO_FRAMES) * 31);
    test_assert(audio_peak(mc_a_l, 0, AUDIO_FRAMES) > 0.01f);
    box_audio_core_free(core);
    for (k = 0; k < 3; k ++) {
        core = mc_core_busy();
        mc_render_blocks(core, mc_b_l, mc_b_r, AUDIO_FRAMES, blocks[k]);
        b = audio_hash(mc_b_l, AUDIO_FRAMES) ^ (audio_hash(mc_b_r, AUDIO_FRAMES) * 31);
        if (a != b) {
            int32_t i;
            for (i = 0; i < AUDIO_FRAMES && mc_a_l[i] == mc_b_l[i] && mc_a_r[i] == mc_b_r[i];
                i ++) { }
            printf("blocks of %d differ from frame %d\n", blocks[k], i);
        }

        test_assert(a == b);
        box_audio_core_free(core);
    }
}
