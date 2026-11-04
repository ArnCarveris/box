/**
 * @file audio/meter.c
 * @brief The meters of the mixer: the levels the core measures at the output of each bus, read
 * from the transport once per frame, the RMS over a window of output, and the AudioMeter of the
 * entities that have it.
 *
 * Metering is opt-in: the module keeps the levels of every bus, which costs a read of the
 * transport per frame, but writes a component only on the entities with AudioMeter.
 */

#include "module.h"
#include <math.h>

/** Start the window of a bus index over, for a bus entity.
 *
 * @param w The window
 * @param entity The bus entity it measures from now on
 */
static void box_audio_meter_clear(
    box_audio_meter_window_t *w,
    ecs_entity_t entity)
{
    ecs_os_memset_t(w, 0, box_audio_meter_window_t);
    w->entity = entity;
}

/** Add the frames of a meter to a window and compute its RMS over the newest frames of the
 * window, a share of the oldest slice that reaches past it.
 *
 * @param w The window
 * @param m The meter of the frame
 * @param window Frames of the window
 */
static void box_audio_meter_push(
    box_audio_meter_window_t *w,
    const box_audio_meter_t *m,
    double window)
{
    double left = 0, right = 0, got = 0;
    int32_t j;
    w->peak_left = m->peak_left;
    w->peak_right = m->peak_right;
    if (m->frames) {
        w->head = (w->head + 1) % BOX_AUDIO_METER_SLICES;
        w->slices[w->head] = (box_audio_meter_slice_t){
            .square_left = m->square_left,
            .square_right = m->square_right,
            .frames = m->frames
        };
        w->count = w->count < BOX_AUDIO_METER_SLICES ? w->count + 1 : w->count;
    }

    for (j = 0; j < w->count && got < window; j ++) {
        const box_audio_meter_slice_t *s =
            &w->slices[(w->head - j + BOX_AUDIO_METER_SLICES) % BOX_AUDIO_METER_SLICES];
        double share = 1;
        if (got + (double)s->frames > window) {
            share = (window - got) / (double)s->frames;
        }

        left += s->square_left * share;
        right += s->square_right * share;
        got += (double)s->frames * share;
    }

    w->rms_left = got > 0 && left > 0 ? (float)sqrt(left / got) : 0.0f;
    w->rms_right = got > 0 && right > 0 ? (float)sqrt(right / got) : 0.0f;
}

/** Whether a transport renders what the core measures: a device or the worklet that plays,
 * or the main thread. A page that waits, is suspended or failed shows silence.
 *
 * @param t The transport, NULL for none
 * @return Whether its meters follow an output that runs
 */
static bool box_audio_meter_live(
    box_audio_transport_t *t)
{
    box_audio_state_t s = t ? t->state(t) : BOX_AUDIO_STATE_OFF;

    return s == BOX_AUDIO_STATE_NO_DEVICE || s == BOX_AUDIO_STATE_RUNNING ||
        s == BOX_AUDIO_STATE_LOST;
}

/** Write the AudioMeter of the entities that have it.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_meter_write(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->meter_query);
    int32_t i;
    while (ecs_query_next(&it)) {
        AudioMeter *meters = ecs_field(&it, AudioMeter, 0);
        const AudioBusState *buses = ecs_field(&it, AudioBusState, 1);
        const AudioEffectState *effects = ecs_field(&it, AudioEffectState, 2);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            AudioMeter m = {0};
            int32_t index = -1;
            if (e == state->master) {
                index = 0;
            } else if (buses && buses[i].index && state->bus_entity[buses[i].index] == e) {
                index = buses[i].index;
            }

            if (index >= 0) {
                const box_audio_meter_window_t *w = &state->meters[index];
                m.peak_left = w->peak_left;
                m.peak_right = w->peak_right;
                m.rms_left = w->rms_left;
                m.rms_right = w->rms_right;
            }

            if (effects && effects[i].slot &&
                state->effects[effects[i].slot - 1].entity == e)
            {
                m.reduction = state->reduction[effects[i].slot - 1];
            }

            meters[i] = m;
        }
    }
}

void box_audio_meters_update(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    box_audio_meter_t buses[BOX_AUDIO_BUS_MAX];
    box_audio_effect_meter_t effects[BOX_AUDIO_EFFECT_MAX];
    uint64_t in_order = 1;
    bool live = box_audio_meter_live(t);
    double rate = t && t->sample_rate > 0 ? (double)t->sample_rate : 48000.0;
    double window = (double)BOX_AUDIO_METER_WINDOW * rate;
    int32_t i;
    ecs_os_memset_n(buses, 0, box_audio_meter_t, BOX_AUDIO_BUS_MAX);
    ecs_os_memset_n(effects, 0, box_audio_effect_meter_t, BOX_AUDIO_EFFECT_MAX);
    if (t) {
        t->meters(t, buses, effects);
    }

    for (i = 0; i < state->order_count; i ++) {
        in_order |= (uint64_t)1 << state->order[i];
    }

    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        box_audio_meter_window_t *w = &state->meters[i];
        ecs_entity_t e = i ? state->bus_entity[i] : state->master;
        if (w->entity != e || !live || !(in_order & ((uint64_t)1 << i))) {
            box_audio_meter_clear(w, e);
        }

        if (live && e) {
            box_audio_meter_push(w, &buses[i], window);
        }
    }

    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        state->reduction[i] = live ? effects[i].reduction : 0.0f;
    }

    if (ecs_query_is_true(state->meter_query)) {
        box_audio_meter_write(world, state);
    }
}

bool box_audio_module_meter(
    const ecs_world_t *world,
    ecs_entity_t channel,
    AudioMeter *meter)
{
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t index = -1;
    bool result = false;
    *meter = (AudioMeter){0};
    if (!state || !channel) {
        goto done;
    }

    if (channel == state->master) {
        index = 0;
    } else {
        index = box_audio_module_bus_index(world, channel);
        index = index ? index : -1;
    }

    if (index >= 0) {
        const box_audio_meter_window_t *w = &state->meters[index];
        meter->peak_left = w->peak_left;
        meter->peak_right = w->peak_right;
        meter->rms_left = w->rms_left;
        meter->rms_right = w->rms_right;
        result = true;
    }

done:
    return result;
}
