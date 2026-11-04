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
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
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
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
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
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
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
        AUDIO_OUT(3, 2), AUDIO_OUT(2, 1),
        AUDIO_OUT(1, 0), AUDIO_MASTER
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
        AUDIO_ROUTE(1, 0, 2), AUDIO_OUT(2, 0), AUDIO_MASTER
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
    box_audio_route_t plain[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_route_t sent[] = {
        AUDIO_ROUTE(1, 0, 2), AUDIO_OUT(2, 0), AUDIO_MASTER
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
    box_audio_route_t good[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_route_t cycle[] = {
        AUDIO_OUT(1, 2), AUDIO_OUT(2, 1), AUDIO_MASTER
    };
    box_audio_route_t self_send[] = { AUDIO_ROUTE(1, 0, 1), AUDIO_MASTER };
    box_audio_route_t back_send[] = {
        AUDIO_OUT(1, 2), AUDIO_ROUTE(2, 0, 1), AUDIO_MASTER
    };
    box_audio_route_t master_first[] = { AUDIO_MASTER, AUDIO_OUT(1, 0) };
    box_audio_route_t twice[] = {
        AUDIO_OUT(1, 0), AUDIO_OUT(1, 0), AUDIO_MASTER
    };
    box_audio_route_t master_out[] = { AUDIO_OUT(1, 0), AUDIO_OUT(0, 1) };
    box_audio_route_t far[] = { AUDIO_OUT(200, 0), AUDIO_MASTER };
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
        AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER
    };
    box_audio_route_t through[] = {
        AUDIO_OUT(1, 2), AUDIO_OUT(2, 0), AUDIO_MASTER
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
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
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

/** Left and right level of the looped stereo sound of the width tests */
#define BUS_STEREO_L 0.4f
#define BUS_STEREO_R 0.1f

/** Create a core with a looped stereo sound 1 of constant channels that plays on bus 1 as
 * voice 1, bus 1 into the master.
 *
 * @return The core
 */
static box_audio_core_t* bus_stereo_core(void)
{
    box_audio_route_t routes[] = { AUDIO_OUT(1, 0), AUDIO_MASTER };
    box_audio_core_t *core = audio_new();
    int32_t i;
    for (i = 0; i < 1024; i ++) {
        audio_sound_b[i * 2] = BUS_STEREO_L;
        audio_sound_b[i * 2 + 1] = BUS_STEREO_R;
    }

    test_assert(box_audio_core_sound(core, 1, audio_sound_b, 1024, 2, AUDIO_RATE));
    audio_order(core, routes, 2);
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);

    return core;
}

void Bus_width_mono(void) {
    // audio/bus
    box_audio_core_t *core = bus_stereo_core();
    float l, r, mid = 0.5f * (BUS_STEREO_L + BUS_STEREO_R);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_STEREO_L) < 1e-6f);
    test_assert(fabsf(r - BUS_STEREO_R) < 1e-6f);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_WIDTH, 0);
    bus_settle(core, &l, &r);
    test_assert(l == r);
    test_assert(fabsf(l - mid) < 1e-6f);
    box_audio_core_free(core);
}

void Bus_width_double(void) {
    // audio/bus
    box_audio_core_t *core = bus_stereo_core();
    float l, r, mid = 0.5f * (BUS_STEREO_L + BUS_STEREO_R);
    float side = 0.5f * (BUS_STEREO_L - BUS_STEREO_R);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_WIDTH, 2);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - (mid + 2 * side)) < 1e-6f);
    test_assert(fabsf(r - (mid - 2 * side)) < 1e-6f);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_WIDTH, 5);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - (mid + 2 * side)) < 1e-6f);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_WIDTH, NAN);
    bus_settle(core, &l, &r);
    test_assert(l == r);
    box_audio_core_free(core);
}

void Bus_width_one_identical(void) {
    // audio/bus
    static float l[4096], r[4096];
    box_audio_core_t *core = bus_stereo_core();
    audio_sine(audio_sound_a, 4096, 300, 0.5f);
    test_assert(box_audio_core_sound(core, 2, audio_sound_a, 4096, 1, AUDIO_RATE));
    audio_play(core, 2, 2, 1, 1, 0.3f, 1, 0);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, -0.4f);
    audio_render(core, 0, 4096);
    memcpy(l, audio_l, sizeof(l));
    memcpy(r, audio_r, sizeof(r));
    box_audio_core_free(core);

    core = bus_stereo_core();
    test_assert(box_audio_core_sound(core, 2, audio_sound_a, 4096, 1, AUDIO_RATE));
    audio_play(core, 2, 2, 1, 1, 0.3f, 1, 0);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, -0.4f);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_WIDTH, 1);
    audio_bus_set(core, 0, BOX_AUDIO_PARAM_WIDTH, 1);
    audio_render(core, 0, 4096);
    test_assert(memcmp(l, audio_l, sizeof(l)) == 0);
    test_assert(memcmp(r, audio_r, sizeof(r)) == 0);
    box_audio_core_free(core);
}

void Bus_four_sends(void) {
    // audio/bus
    box_audio_route_t routes[] = {
        { 1, 6, { 2, 3, 4, 5 }, 0, 0 }, AUDIO_OUT(2, 0), AUDIO_OUT(3, 0), AUDIO_OUT(4, 0),
        AUDIO_OUT(5, 0), AUDIO_OUT(6, 0), AUDIO_MASTER
    };
    float levels[] = { 0.1f, 0.2f, 0.3f, 0.4f };
    box_audio_core_t *core = bus_core(1);
    float l, r;
    int32_t j, k;
    audio_order(core, routes, 7);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 0);
    audio_bus_set(core, 6, BOX_AUDIO_PARAM_MUTE, 1);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, levels[0]);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND_1, levels[1]);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND_2, levels[2]);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND_3, levels[3]);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 1.0f) < 1e-5f);
    test_assert(fabsf(r - BUS_LEVEL * 1.0f) < 1e-5f);
    for (j = 0; j < 4; j ++) {
        for (k = 0; k < 4; k ++) {
            audio_bus_set(core, (uint32_t)(2 + k), BOX_AUDIO_PARAM_MUTE, k != j);
        }

        bus_settle(core, &l, &r);
        test_assert(fabsf(l - BUS_LEVEL * levels[j]) < 1e-6f);
        test_assert(fabsf(r - BUS_LEVEL * levels[j]) < 1e-6f);
    }

    box_audio_core_free(core);
}

void Bus_pre_fader_send(void) {
    // audio/bus
    box_audio_route_t pre[] = {
        { 1, 0, { 2, BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE }, 1, 0 },
        AUDIO_OUT(2, 0), AUDIO_MASTER
    };
    box_audio_route_t post[] = { AUDIO_ROUTE(1, 0, 2), AUDIO_OUT(2, 0), AUDIO_MASTER };
    box_audio_core_t *core = bus_core(1);
    float l, r;
    audio_order(core, pre, 3);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 0);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.25f);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_PAN, 1);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_SEND, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);
    test_assert(fabsf(r - BUS_LEVEL * (0.5f + 0.25f * 1.41421356f)) < 1e-5f);

    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 1);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);

    audio_bus_set(core, 1, BOX_AUDIO_PARAM_MUTE, 1);
    bus_settle(core, &l, &r);
    test_flt(audio_peak(audio_l, AUDIO_RAMP, 2048), 0);
    test_flt(audio_peak(audio_r, AUDIO_RAMP, 2048), 0);

    audio_bus_set(core, 1, BOX_AUDIO_PARAM_MUTE, 0);
    audio_order(core, post, 3);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l) < 1e-6f);
    test_assert(fabsf(r - BUS_LEVEL * 1.41421356f * 1.5f) < 1e-5f);
    box_audio_core_free(core);
}

void Bus_send_earlier_refused(void) {
    // audio/bus
    box_audio_route_t good[] = { AUDIO_OUT(1, 0), AUDIO_OUT(2, 0), AUDIO_MASTER };
    box_audio_route_t earlier[] = {
        AUDIO_OUT(2, 0),
        { 1, 0, { BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, 2, BOX_AUDIO_BUS_NONE }, 0, 0 },
        AUDIO_MASTER
    };
    box_audio_route_t master_send[] = {
        AUDIO_OUT(1, 0), { 0, BOX_AUDIO_BUS_NONE, { BOX_AUDIO_BUS_NONE, 1, BOX_AUDIO_BUS_NONE,
            BOX_AUDIO_BUS_NONE }, 0, 0 }
    };
    box_audio_route_t far[] = {
        { 1, 0, { BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, 200 }, 0, 0 },
        AUDIO_MASTER
    };
    box_audio_core_t *core = bus_core(1);
    float l, r;
    audio_order(core, good, 3);
    audio_bus_set(core, 1, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 0);

    audio_order(core, earlier, 3);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 1);
    audio_order(core, master_send, 2);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 1);
    audio_order(core, far, 2);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 1);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.5f) < 1e-6f);
    box_audio_core_free(core);
}

void Bus_buses_64(void) {
    // audio/bus
    box_audio_route_t routes[BOX_AUDIO_BUS_MAX];
    box_audio_core_t *core;
    float l, r;
    int32_t i;
    test_int(BOX_AUDIO_BUS_MAX, 64);
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        uint8_t bus = (uint8_t)(BOX_AUDIO_BUS_MAX - 1 - i);
        box_audio_route_t route = AUDIO_OUT(bus, (uint8_t)(bus ? bus - 1 : BOX_AUDIO_BUS_NONE));
        routes[i] = route;
    }

    core = bus_core(BOX_AUDIO_BUS_MAX - 1);
    audio_order(core, routes, BOX_AUDIO_BUS_MAX);
    test_int(audio_events(core, BOX_AUDIO_EVENT_ORDER_REFUSED, NULL), 0);
    bus_settle(core, &l, &r);
    test_assert(l == BUS_LEVEL);
    test_assert(r == BUS_LEVEL);

    audio_bus_set(core, BOX_AUDIO_BUS_MAX - 1, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    audio_bus_set(core, 32, BOX_AUDIO_PARAM_VOLUME, 0.5f);
    bus_settle(core, &l, &r);
    test_assert(fabsf(l - BUS_LEVEL * 0.25f) < 1e-6f);

    audio_play(core, 2, 1, BOX_AUDIO_BUS_MAX, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    test_int(box_audio_core_voice_count(core), 1);
    box_audio_core_free(core);
}
