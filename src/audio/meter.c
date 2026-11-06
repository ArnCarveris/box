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

/** Add the meters of a read to the measurement over a span that runs.
 *
 * @param state The state
 * @param buses The meters of the buses by index
 * @param effects The meters of the effects by slot
 * @param live Whether the output runs
 * @param rate Frames per second of the output
 */
static void box_audio_meter_job_add(
    box_audio_module_t *state,
    const box_audio_meter_t *buses,
    const box_audio_effect_meter_t *effects,
    bool live,
    double rate)
{
    box_audio_meter_job_t *job = &state->meter_job;
    uint32_t frames = buses[0].frames;
    float peak;
    int32_t i;
    if (!job->running) {
        goto done;
    }

    if (!live || !frames) {
        job->stalls ++;
        goto done;
    }

    if (job->skip > 0) {
        job->skip --;
        goto done;
    }

    job->sample_rate = (float)rate;
    for (i = 0; i < job->count; i ++) {
        ecs_entity_t e = job->channels[i];
        int32_t index = -1, k;
        if (e == state->master) {
            index = 0;
        } else {
            for (k = 1; k < BOX_AUDIO_BUS_MAX && index < 0; k ++) {
                index = state->bus_entity[k] == e ? k : -1;
            }
        }

        if (index < 0) {
            job->lost[i] = true;
        } else {
            box_audio_span_level_add(&job->levels[i], &buses[index], rate);
        }
    }

    for (i = 0; i < BOX_AUDIO_EFFECT_MAX; i ++) {
        ecs_entity_t e = state->effects[i].entity;
        if (!e) {
            continue;
        }

        if (job->effects[i] != e) {
            job->effects[i] = e;
            job->reduction[i] = (box_audio_span_reduction_t){0};
        }

        box_audio_span_reduction_add(&job->reduction[i], effects[i].reduction, frames);
    }

    peak = buses[0].peak_left > buses[0].peak_right ? buses[0].peak_left : buses[0].peak_right;
    if (peak > BOX_AUDIO_LIMITER_THRESHOLD) {
        float db = (float)(20.0 * log10((double)peak / (double)BOX_AUDIO_LIMITER_THRESHOLD));
        job->limiter = db > job->limiter ? db : job->limiter;
    }

    job->frames += (double)frames;
    if (job->frames >= job->target) {
        job->running = false;
        job->done = true;
    }

done:
    return;
}

void box_audio_meter_job_sync(
    box_audio_module_t *state,
    uint32_t serial)
{
    box_audio_meter_job_t *job = &state->meter_job;
    uint64_t wanted = 0;
    int32_t i, k;
    if (job->loudness_serial != serial) {
        job->loudness = 0;
        job->loudness_serial = serial;
    }

    for (i = 0; job->running && i < job->count; i ++) {
        if (job->channels[i] == state->master) {
            wanted |= 1;
            continue;
        }

        for (k = 1; k < BOX_AUDIO_BUS_MAX; k ++) {
            if (state->bus_entity[k] == job->channels[i]) {
                wanted |= (uint64_t)1 << k;
            }
        }
    }

    for (k = 0; k < BOX_AUDIO_BUS_MAX; k ++) {
        uint64_t bit = (uint64_t)1 << k;
        if ((wanted & bit) != (job->loudness & bit) && box_audio_reserve(state)) {
            box_audio_write_bus_set(&state->writer, (uint32_t)k, BOX_AUDIO_PARAM_LOUDNESS,
                (wanted & bit) ? 1.0f : 0.0f, 0);
            job->loudness ^= bit;
        }
    }
}

void box_audio_meter_job_fini(
    box_audio_module_t *state)
{
    box_audio_meter_job_t *job = &state->meter_job;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_METER_JOB_MAX; i ++) {
        box_audio_span_level_fini(&job->levels[i]);
    }

    job->running = false;
    job->done = false;
    job->count = 0;
}

int box_audio_meter_job_start(
    ecs_world_t *world,
    const ecs_entity_t *channels,
    int32_t count,
    double seconds,
    int32_t skip)
{
    box_audio_module_t *state = box_audio_module_get(world);
    box_audio_transport_t *t = box_audio_output_transport(world);
    box_audio_meter_job_t *job;
    uint64_t loudness;
    uint32_t serial;
    double rate;
    int32_t i, k;
    int result = -1;
    if (!state) {
        ecs_err("box.audio is not imported");
        goto done;
    }

    if (!t || !box_audio_meter_live(t)) {
        ecs_err("box.audio: the output does not run, there is nothing to measure");
        goto done;
    }

    if (!(seconds > 0) || seconds > BOX_AUDIO_METER_JOB_SECONDS) {
        ecs_err("box.audio: a measurement takes more than 0 and at most %.0f seconds",
            BOX_AUDIO_METER_JOB_SECONDS);
        goto done;
    }

    job = &state->meter_job;
    loudness = job->loudness;
    serial = job->loudness_serial;
    box_audio_meter_job_fini(state);
    ecs_os_memset_t(job, 0, box_audio_meter_job_t);
    job->loudness = loudness;
    job->loudness_serial = serial;
    rate = t->sample_rate > 0 ? (double)t->sample_rate : 48000.0;
    for (i = 0; i < count && job->count < BOX_AUDIO_METER_JOB_MAX - 1; i ++) {
        if (channels[i] == state->master) {
            job->master_listed = true;
            continue;
        }

        for (k = 0; k < job->count && job->channels[k] != channels[i]; k ++) { }
        if (k == job->count) {
            job->channels[job->count ++] = channels[i];
        }
    }

    job->channels[job->count ++] = state->master;
    job->target = seconds * rate;
    job->sample_rate = (float)rate;
    job->skip = skip > 2 ? skip : 2;
    job->running = true;
    result = 0;
done:
    return result;
}

const box_audio_meter_job_t* box_audio_meter_job_get(
    const ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);

    return state ? &state->meter_job : NULL;
}

void box_audio_meter_job_clear(
    ecs_world_t *world)
{
    box_audio_module_t *state = box_audio_module_get(world);
    if (state) {
        box_audio_meter_job_fini(state);
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

    box_audio_meter_job_add(state, buses, effects, live, rate);

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
