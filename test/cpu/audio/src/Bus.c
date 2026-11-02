/**
 * @file Bus.c
 * @brief Buses of the audio core: volume, mute, pan, chains, sends and the processing order.
 */

#include "util.h"

/** Level of the looped constant sound of the bus tests */
#define BUS_LEVEL 0.25f

/** Create a core with a looped constant sound 1 that plays on a bus as voice 1.
 *
 * @param bus Bus of the voice
 * @return The core
 */
static box_audio_core_t* bus_core(
    uint32_t bus)
{
    box_audio_core_t *core = audio_new();
    audio_fill(audio_sound_a, 1024, BUS_LEVEL);
    test_assert(box_audio_core_sound(core, 1, audio_sound_a, 1024, 1, AUDIO_RATE));
    audio_play(core, 1, 1, bus, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);

    return core;
}

/** Render until every ramp has settled and get the last frame.
 *
 * @param core The core
 * @param l Receives the left sample
 * @param r Receives the right sample
 */
static void bus_settle(
    box_audio_core_t *core,
    float *l,
    float *r)
{
    audio_render(core, 0, 2048);
    *l = audio_l[2047];
    *r = audio_r[2047];
}

void Bus_master_default(void) {
    // audio/bus
    box_audio_core_t *core = bus_core(0);
    float l, r;
    bus_settle(core, &l, &r);
    test_assert(l == BUS_LEVEL);
    test_assert(r == BUS_LEVEL);
    box_audio_core_free(core);
}

void Bus_volume(void) {
    // audio/bus
    box_audio_route_t routes[] = { { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 } };
    box_audio_core_t *core = bus_core(1);
    float l, r;
    audio_order(core, routes, 2);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);
    test_assert(fabsf(r - BUS_LEVEL * 0.5f) < 1e-6f);
    audio_bus_set(core, 0, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.25f) < 1e-6f);
    box_audio_core_free(core);
}

void Bus_mute(void) {
    // audio/bus
    box_audio_route_t routes[] = { { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 } };
    box_audio_core_t *core = bus_core(1);
    float l, r;
    audio_order(core, routes, 2);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_MUTE, 1);
    bus_settle(core, &l, &r);
    test_flt(audio_peak(audio_l, AUDIO_RAMP, 2048), 0);
    test_flt(audio_peak(audio_r, AUDIO_RAMP, 2048), 0);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_MUTE, 0);
    bus_settle(core, &l, &r);
    test_assert(l == BUS_LEVEL);
    test_assert(r == BUS_LEVEL);
    box_audio_core_free(core);
}

void Bus_pan(void) {
    // audio/bus
    box_audio_route_t routes[] = { { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 } };
    box_audio_core_t *core = bus_core(1);
    float pans[] = { -1, -0.5f, 0, 0.5f, 1 };
    float l[5], r[5], power = 2 * BUS_LEVEL * BUS_LEVEL;
    int32_t i;
    audio_bus_set(core, 0, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    audio_order(core, routes, 2);
    for (i = 0; i < 5; i ++) {
        audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, pans[i]);
        bus_settle(core, &l[i], &r[i]);
        l[i] *= 2;
        r[i] *= 2;
        test_assert(fabsf(l[i] * l[i] + r[i] * r[i] - power) < 1e-5f);
    }

    test_assert(fabsf(l[0] - BUS_LEVEL * sqrtf(2)) < 1e-6f);
    test_flt(r[0], 0);
    test_assert(fabsf(l[2] - BUS_LEVEL) < 1e-6f);
    test_assert(fabsf(r[2] - BUS_LEVEL) < 1e-6f);
    test_flt(l[4], 0);
    test_assert(fabsf(r[4] - BUS_LEVEL * sqrtf(2)) < 1e-6f);
    test_assert(l[1] > r[1]);
    test_assert(r[3] > l[3]);
    test_assert(fabsf(l[1] - r[3]) < 1e-6f);
    box_audio_core_free(core);
}

void Bus_voice_pan(void) {
    // audio/bus
    box_audio_core_t *core = bus_core(0);
    float l, r;
    audio_bus_set(core, 0, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PAN, -1);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - 0.5f * BUS_LEVEL * sqrtf(2)) < 1e-6f);
    test_flt(r, 0);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_PAN, 0.3f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(4 * (l * l + r * r) - 2 * BUS_LEVEL * BUS_LEVEL) < 1e-5f);
    test_assert(r > l);
    box_audio_core_free(core);
}

void Bus_chain(void) {
    // audio/bus
    box_audio_route_t routes[] = {
        { 3, 2, BOX_AUDIO_BUS_NONE }, { 2, 1, BOX_AUDIO_BUS_NONE },
        { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_core_t *core = bus_core(3);
    float l, r;
    audio_order(core, routes, 4);
    audio_bus_set(core, 3, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    audio_bus_set(core, 2, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.125f) < 1e-6f);
    test_assert(fabsf(r - BUS_LEVEL * 0.125f) < 1e-6f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 0);
    box_audio_core_free(core);
}

void Bus_send(void) {
    // audio/bus
    box_audio_route_t routes[] = {
        { 1, 0, 2 }, { 2, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_core_t *core = bus_core(1);
    float l, r;
    audio_order(core, routes, 3);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 1.5f) < 1e-6f);
    test_assert(fabsf(r - BUS_LEVEL * 1.5f) < 1e-6f);
    audio_bus_set(core, 2, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 1.25f) < 1e-6f);
    box_audio_core_free(core);
}

void Bus_send_zero(void) {
    // audio/bus
    static float l[4096], r[4096];
    box_audio_route_t plain[] = { { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 } };
    box_audio_route_t sent[] = {
        { 1, 0, 2 }, { 2, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_core_t *core = bus_core(1);
    audio_sine(audio_sound_b, 4096, 300, 0.5f);
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 4096, 1, AUDIO_RATE));
    audio_play(core, 2, 2, 1, 1, 0.2f, 1, 0);
    audio_order(core, plain, 2);
    audio_render(core, 0, 4096);
    memcpy(l, audio_l, sizeof(l));
    memcpy(r, audio_r, sizeof(r));
    box_audio_core_free(core);

    core = bus_core(1);
    test_assert(box_audio_core_sound(core, 2, audio_sound_b, 4096, 1, AUDIO_RATE));
    audio_play(core, 2, 2, 1, 1, 0.2f, 1, 0);
    audio_order(core, sent, 3);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, 0);
    audio_render(core, 0, 4096);
    test_assert(memcmp(l, audio_l, sizeof(l)) == 0);
    test_assert(memcmp(r, audio_r, sizeof(r)) == 0);
    box_audio_core_free(core);
}

void Bus_cycle_refused(void) {
    // audio/bus
    box_audio_route_t good[] = { { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 } };
    box_audio_route_t cycle[] = {
        { 1, 2, BOX_AUDIO_BUS_NONE }, { 2, 1, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_route_t self_send[] = { { 1, 0, 1 }, { 0, 255, 255 } };
    box_audio_route_t back_send[] = {
        { 1, 2, BOX_AUDIO_BUS_NONE }, { 2, 0, 1 }, { 0, 255, 255 }
    };
    box_audio_route_t master_first[] = { { 0, 255, 255 }, { 1, 0, BOX_AUDIO_BUS_NONE } };
    box_audio_route_t twice[] = {
        { 1, 0, BOX_AUDIO_BUS_NONE }, { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_route_t master_out[] = { { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 1, 255 } };
    box_audio_route_t far[] = { { 40, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 } };
    box_audio_core_t *core = bus_core(1);
    float l, r;
    audio_order(core, good, 2);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 0);

    audio_order(core, cycle, 3);
    audio_order(core, self_send, 2);
    audio_order(core, back_send, 3);
    audio_order(core, master_first, 2);
    audio_order(core, twice, 3);
    audio_order(core, master_out, 2);
    audio_order(core, far, 2);
    audio_order(core, good, 0);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 8);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);
    test_assert(fabsf(r - BUS_LEVEL * 0.5f) < 1e-6f);
    box_audio_core_free(core);
}

void Bus_order_one_block(void) {
    // audio/bus
    box_audio_route_t direct[] = {
        { 1, 0, BOX_AUDIO_BUS_NONE }, { 2, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_route_t through[] = {
        { 1, 2, BOX_AUDIO_BUS_NONE }, { 2, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 }
    };
    box_audio_core_t *core = bus_core(1);
    int32_t i;
    audio_order(core, direct, 3);
    audio_bus_set(core, 2, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    audio_render(core, 0, 2048);
    audio_order(core, through, 3);
    audio_render(core, 2048, 128);
    audio_order(core, direct, 3);
    audio_render(core, 2176, 128);
    for (i = 1024; i < 2048; i ++) {
        test_assert(audio_l[i] == BUS_LEVEL);
    }

    for (i = 2048; i < 2176; i ++) {
        test_assert(fabsf(audio_l[i] - BUS_LEVEL * 0.5f) < 1e-6f);
    }

    for (i = 2176; i < 2304; i ++) {
        test_assert(audio_l[i] == BUS_LEVEL);
    }

    box_audio_core_free(core);
}

void Bus_not_in_order_silent(void) {
    // audio/bus
    box_audio_core_t *core = bus_core(5);
    float l, r;
    bus_settle(core, &l, &r);
    test_flt(audio_peak(audio_l, 0, 2048), 0);
    test_flt(audio_peak(audio_r, 0, 2048), 0);
    test_int(box_audio_core_voice_count(core), 1);
    box_audio_core_free(core);
}

void Bus_voice_bus_change(void) {
    // audio/bus
    box_audio_route_t routes[] = { { 1, 0, BOX_AUDIO_BUS_NONE }, { 0, 255, 255 } };
    box_audio_core_t *core = bus_core(0);
    float l, r;
    audio_order(core, routes, 2);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(l == BUS_LEVEL);
    audio_voice_set(core, 1, BOX_AUDIO_PARAM_BUS, 1);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);
    box_audio_core_free(core);
}
