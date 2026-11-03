/**
 * @file audio/core/core.c
 * @brief The audio core: voices into buses, buses in a checked order, a limiter on the master.
 *
 * Every gain is a pair of ramps, left and right, that holds volume, pan, mute and fades, so a
 * change never jumps. Render works in blocks of BOX_AUDIO_CORE_BLOCK frames with per frame
 * ramps, so its output does not depend on how the caller splits the frames.
 */

#include "internal.h"

#ifndef BOX_AUDIO_CORE_WASM
#include <stdlib.h>
#endif

/** Pi, for the sine approximation */
#define BOX_AUDIO_CORE_PI 3.14159265358979323846

/** Square root of 2, the gain of a channel at full pan */
#define BOX_AUDIO_SQRT2 1.41421356237309504880f

#ifndef BOX_AUDIO_CORE_WASM

/** Default allocator of native builds.
 *
 * @param size Bytes
 * @param ctx Unused
 * @return The memory
 */
static void* box_audio_core_malloc(
    size_t size,
    void *ctx)
{
    (void)ctx;

    return malloc(size);
}

/** Default free of native builds.
 *
 * @param ptr The memory
 * @param ctx Unused
 */
static void box_audio_core_mfree(
    void *ptr,
    void *ctx)
{
    (void)ctx;
    free(ptr);
}

#endif

/** Copy bytes, without depending on the memcpy of a libc.
 *
 * @param dst Destination
 * @param src Source
 * @param size Bytes
 */
static void box_audio_copy(
    void *dst,
    const void *src,
    int32_t size)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    int32_t i;
    for (i = 0; i < size; i ++) {
        d[i] = s[i];
    }
}

float box_audio_core_sin(
    double turns)
{
    double x, x2;
    turns -= (double)(int64_t)turns;
    if (turns < 0) {
        turns += 1;
    }

    if (turns >= 0.5) {
        turns -= 1;
    }

    x = turns * 2 * BOX_AUDIO_CORE_PI;
    if (x > BOX_AUDIO_CORE_PI * 0.5) {
        x = BOX_AUDIO_CORE_PI - x;
    } else if (x < -BOX_AUDIO_CORE_PI * 0.5) {
        x = -BOX_AUDIO_CORE_PI - x;
    }

    x2 = x * x;

    return (float)(x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 *
        (1 - x2 / 110 * (1 - x2 / 156)))))));
}

/** Equal power gains of a pan, 1 for both channels at the center and sqrt 2 at a side.
 *
 * @param pan Pan, -1..1
 * @param left Receives the gain of the left channel
 * @param right Receives the gain of the right channel
 */
static void box_audio_pan(
    float pan,
    float *left,
    float *right)
{
    if (pan == 0) {
        *left = 1;
        *right = 1;
    } else if (pan <= -1) {
        *left = BOX_AUDIO_SQRT2;
        *right = 0;
    } else if (pan >= 1) {
        *left = 0;
        *right = BOX_AUDIO_SQRT2;
    } else {
        double turns = (double)(pan + 1) * 0.125;
        *left = BOX_AUDIO_SQRT2 * box_audio_core_sin(turns + 0.25);
        *right = BOX_AUDIO_SQRT2 * box_audio_core_sin(turns);
    }
}

/** Move a ramp to a target over a number of frames.
 *
 * @param ramp The ramp
 * @param target The target
 * @param frames Frames to reach it, at least 1
 */
static void box_audio_ramp_to(
    box_audio_ramp_t *ramp,
    float target,
    int32_t frames)
{
    ramp->target = target;
    if (ramp->value == target) {
        ramp->left = 0;
        ramp->step = 0;
    } else {
        ramp->left = frames > 0 ? frames : 1;
        ramp->step = (target - ramp->value) / (float)ramp->left;
    }
}

/** Set a ramp to a value at once.
 *
 * @param ramp The ramp
 * @param value The value
 */
static void box_audio_ramp_set(
    box_audio_ramp_t *ramp,
    float value)
{
    ramp->value = value;
    ramp->target = value;
    ramp->step = 0;
    ramp->left = 0;
}

/** Advance a ramp by a frame.
 *
 * @param ramp The ramp
 * @return Its value for the frame
 */
static inline float box_audio_ramp_next(
    box_audio_ramp_t *ramp)
{
    if (ramp->left > 0) {
        ramp->left --;
        ramp->value = ramp->left ? ramp->value + ramp->step : ramp->target;
    }

    return ramp->value;
}

/** Clamp a value of a command; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_clamp(
    float v,
    float lo,
    float hi)
{
    if (!(v >= lo)) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }

    return v;
}

/** Push an event into the next slot of the ring, which has room.
 *
 * @param core The core
 * @param type A box_audio_event_type_t
 * @param id Id of the event
 * @param value Value of the event
 * @return Slot of the event
 */
static int32_t box_audio_core_push(
    box_audio_core_t *core,
    int32_t type,
    uint32_t id,
    int32_t value)
{
    int32_t at = (core->event_head + core->event_count) % BOX_AUDIO_EVENT_MAX;
    box_audio_event_t *e = &core->events[at];
    e->type = type;
    e->id = id;
    e->value = value;
    core->event_count ++;

    return at;
}

void box_audio_core_event(
    box_audio_core_t *core,
    int32_t type,
    uint32_t id,
    int32_t value)
{
    if (core->event_count < BOX_AUDIO_EVENT_MAX - 1) {
        box_audio_core_push(core, type, id, value);
    } else if (type == BOX_AUDIO_EVENT_SOUND_FREE && id < BOX_AUDIO_SOUND_MAX) {
        if (core->sounds[id].state != BOX_AUDIO_SOUND_PENDING) {
            core->sounds[id].state = BOX_AUDIO_SOUND_PENDING;
            core->free_pending ++;
        }
    } else {
        core->events_lost ++;
        if (core->event_lost_at >= 0) {
            core->events[core->event_lost_at].value = core->events_lost;
        } else {
            core->event_lost_at = box_audio_core_push(core, BOX_AUDIO_EVENT_LOST, 0,
                core->events_lost);
        }
    }
}

/** Push the SOUND_FREE events that waited for room, as many as the ring takes.
 *
 * @param core The core
 */
static void box_audio_core_flush_free(
    box_audio_core_t *core)
{
    uint32_t i;
    for (i = 0; i < BOX_AUDIO_SOUND_MAX && core->free_pending > 0 &&
        core->event_count < BOX_AUDIO_EVENT_MAX - 1; i ++)
    {
        if (core->sounds[i].state == BOX_AUDIO_SOUND_PENDING) {
            core->sounds[i].state = BOX_AUDIO_SOUND_FREE;
            core->free_pending --;
            box_audio_core_push(core, BOX_AUDIO_EVENT_SOUND_FREE, i, 0);
        }
    }
}

/** Set the gain targets of a voice from its volume and pan.
 *
 * @param core The core
 * @param v The voice
 */
static void box_audio_voice_gains(
    box_audio_core_t *core,
    box_audio_voice_t *v)
{
    float l, r;
    box_audio_pan(v->pan, &l, &r);
    box_audio_ramp_to(&v->left, v->volume * l, core->ramp_frames);
    box_audio_ramp_to(&v->right, v->volume * r, core->ramp_frames);
}

/** Set the gain targets of a bus from its volume, pan, mute and send level.
 *
 * @param core The core
 * @param b The bus
 */
static void box_audio_bus_gains(
    box_audio_core_t *core,
    box_audio_bus_t *b)
{
    float l, r, volume = b->mute ? 0.0f : b->volume;
    box_audio_pan(b->pan, &l, &r);
    box_audio_ramp_to(&b->left, volume * l, core->ramp_frames);
    box_audio_ramp_to(&b->right, volume * r, core->ramp_frames);
    box_audio_ramp_to(&b->send, b->send_level, core->ramp_frames);
}

/** The level of a voice for the cap: the larger of its gain targets.
 *
 * @param v The voice
 * @return The level
 */
static float box_audio_voice_level(
    const box_audio_voice_t *v)
{
    return v->left.target > v->right.target ? v->left.target : v->right.target;
}

/** End a voice: report it, then free its slot and give back its source, so the end comes
 * before the SOUND_FREE it can cause.
 *
 * @param core The core
 * @param v The voice
 */
static void box_audio_voice_end(
    box_audio_core_t *core,
    box_audio_voice_t *v)
{
    if (v->state == BOX_AUDIO_VOICE_PLAYING) {
        core->playing --;
    }

    box_audio_core_event(core, BOX_AUDIO_EVENT_VOICE_END, v->handle, 0);
    box_audio_source_end(core, &v->source);
    v->state = BOX_AUDIO_VOICE_FREE;
}

/** Fade a voice out over a number of frames, after which it ends.
 *
 * @param core The core
 * @param v The voice
 * @param frames Frames of the fade
 */
static void box_audio_voice_fade(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    int32_t frames)
{
    if (v->state == BOX_AUDIO_VOICE_PLAYING) {
        core->playing --;
    }

    v->state = BOX_AUDIO_VOICE_STOPPING;
    box_audio_ramp_to(&v->left, 0, frames);
    box_audio_ramp_to(&v->right, 0, frames);
}

/** Find the playing voice of a handle.
 *
 * @param core The core
 * @param handle The handle
 * @return The voice, NULL when none plays with the handle
 */
static box_audio_voice_t* box_audio_voice_find(
    box_audio_core_t *core,
    uint32_t handle)
{
    box_audio_voice_t *result = NULL;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS && !result; i ++) {
        box_audio_voice_t *v = &core->voices[i];
        if (v->state == BOX_AUDIO_VOICE_PLAYING && v->handle == handle) {
            result = v;
        }
    }

    return result;
}

/** Steal the playing voice with the lowest level, the first of equals, with a short fade.
 *
 * @param core The core
 */
static void box_audio_voice_steal(
    box_audio_core_t *core)
{
    box_audio_voice_t *quietest = NULL;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS; i ++) {
        box_audio_voice_t *v = &core->voices[i];
        if (v->state == BOX_AUDIO_VOICE_PLAYING && (!quietest ||
            box_audio_voice_level(v) < box_audio_voice_level(quietest)))
        {
            quietest = v;
        }
    }

    if (quietest) {
        core->steals ++;
        box_audio_core_event(core, BOX_AUDIO_EVENT_VOICE_STOLEN, quietest->handle,
            core->steals);
        box_audio_voice_fade(core, quietest, core->steal_frames);
    }
}

/** Get a free voice slot; when every slot is taken, end the quietest fading voice.
 *
 * @param core The core
 * @return The slot
 */
static box_audio_voice_t* box_audio_voice_slot(
    box_audio_core_t *core)
{
    box_audio_voice_t *result = NULL, *quietest = NULL;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS && !result; i ++) {
        box_audio_voice_t *v = &core->voices[i];
        if (v->state == BOX_AUDIO_VOICE_FREE) {
            result = v;
        } else if (v->state == BOX_AUDIO_VOICE_STOPPING) {
            float level = v->left.value > v->right.value ? v->left.value : v->right.value;
            float best = quietest ? (quietest->left.value > quietest->right.value ?
                quietest->left.value : quietest->right.value) : 0;
            if (!quietest || level < best) {
                quietest = v;
            }
        }
    }

    if (!result && quietest) {
        box_audio_voice_end(core, quietest);
        result = quietest;
    }

    return result;
}

/** Apply a play command.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_play(
    box_audio_core_t *core,
    const box_audio_cmd_play_t *cmd)
{
    box_audio_voice_t *v = NULL;
    if (cmd->sound >= BOX_AUDIO_SOUND_MAX || cmd->bus >= BOX_AUDIO_BUS_MAX ||
        core->sounds[cmd->sound].state != BOX_AUDIO_SOUND_READY)
    {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_PLAY, 0);
        goto done;
    }

    if (core->playing >= BOX_AUDIO_VOICE_MAX) {
        box_audio_voice_steal(core);
    }

    v = box_audio_voice_slot(core);
    if (!v) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_PLAY, 0);
        goto done;
    }

    v->state = BOX_AUDIO_VOICE_PLAYING;
    v->handle = cmd->voice;
    v->bus = cmd->bus;
    v->volume = box_audio_clamp(cmd->volume, 0, BOX_AUDIO_VOLUME_MAX);
    v->pan = box_audio_clamp(cmd->pan, -1, 1);
    v->pitch = box_audio_clamp(cmd->pitch, BOX_AUDIO_PITCH_MIN, BOX_AUDIO_PITCH_MAX);
    box_audio_ramp_set(&v->left, 0);
    box_audio_ramp_set(&v->right, 0);
    box_audio_voice_gains(core, v);
    box_audio_source_sample(core, &v->source, cmd->sound, v->pitch,
        (cmd->flags & BOX_AUDIO_PLAY_LOOP) != 0);
    core->playing ++;

done:
    return;
}

/** Apply a set command of a voice.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_voice_set(
    box_audio_core_t *core,
    const box_audio_cmd_set_t *cmd)
{
    box_audio_voice_t *v = box_audio_voice_find(core, cmd->target);
    float value = cmd->value;
    if (!v) {
        goto done;
    }

    if (cmd->param == BOX_AUDIO_PARAM_VOLUME) {
        v->volume = box_audio_clamp(value, 0, BOX_AUDIO_VOLUME_MAX);
        box_audio_voice_gains(core, v);
    } else if (cmd->param == BOX_AUDIO_PARAM_PAN) {
        v->pan = box_audio_clamp(value, -1, 1);
        box_audio_voice_gains(core, v);
    } else if (cmd->param == BOX_AUDIO_PARAM_PITCH) {
        v->pitch = box_audio_clamp(value, BOX_AUDIO_PITCH_MIN, BOX_AUDIO_PITCH_MAX);
        box_audio_source_pitch(&v->source, v->pitch);
    } else if (cmd->param == BOX_AUDIO_PARAM_BUS && value >= 0 && value < BOX_AUDIO_BUS_MAX) {
        v->bus = (uint32_t)value;
    } else {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_VOICE_SET, 0);
    }

done:
    return;
}

/** Apply a set command of a bus.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_bus_set(
    box_audio_core_t *core,
    const box_audio_cmd_set_t *cmd)
{
    box_audio_bus_t *b;
    float value = cmd->value;
    bool known = true;
    if (cmd->target >= BOX_AUDIO_BUS_MAX) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_BUS_SET, 0);
        goto done;
    }

    b = &core->buses[cmd->target];
    if (cmd->param == BOX_AUDIO_PARAM_VOLUME) {
        b->volume = box_audio_clamp(value, 0, BOX_AUDIO_VOLUME_MAX);
    } else if (cmd->param == BOX_AUDIO_PARAM_PAN) {
        b->pan = box_audio_clamp(value, -1, 1);
    } else if (cmd->param == BOX_AUDIO_PARAM_MUTE) {
        b->mute = value != 0 && value == value;
    } else if (cmd->param == BOX_AUDIO_PARAM_SEND) {
        b->send_level = box_audio_clamp(value, 0, BOX_AUDIO_VOLUME_MAX);
    } else {
        known = false;
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_BUS_SET, 0);
    }

    if (known) {
        box_audio_bus_gains(core, b);
    }

done:
    return;
}

/** Check and apply an order command, or refuse it with an event.
 *
 * @param core The core
 * @param bytes The command, header included
 * @param size Bytes of the command
 */
static void box_audio_cmd_order(
    box_audio_core_t *core,
    const uint8_t *bytes,
    int32_t size)
{
    box_audio_cmd_order_t cmd;
    box_audio_route_t routes[BOX_AUDIO_BUS_MAX];
    int32_t position[BOX_AUDIO_BUS_MAX];
    int32_t i, count;
    bool valid = true;
    box_audio_copy(&cmd, bytes, (int32_t)sizeof(cmd));
    count = cmd.count > BOX_AUDIO_BUS_MAX ? -1 : (int32_t)cmd.count;
    if (count < 1 || size < (int32_t)sizeof(cmd) + count * (int32_t)sizeof(box_audio_route_t)) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_ORDER_REFUSED, 0, (int32_t)cmd.count);
        goto done;
    }

    box_audio_copy(routes, bytes + sizeof(cmd), count * (int32_t)sizeof(box_audio_route_t));
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        position[i] = -1;
    }

    for (i = 0; i < count && valid; i ++) {
        if (routes[i].bus >= BOX_AUDIO_BUS_MAX || position[routes[i].bus] >= 0) {
            valid = false;
        } else {
            position[routes[i].bus] = i;
        }
    }

    valid = valid && routes[count - 1].bus == 0 &&
        routes[count - 1].output == BOX_AUDIO_BUS_NONE &&
        routes[count - 1].send == BOX_AUDIO_BUS_NONE;
    for (i = 0; i < count - 1 && valid; i ++) {
        uint8_t out = routes[i].output, send = routes[i].send;
        valid = out < BOX_AUDIO_BUS_MAX && position[out] > i &&
            (send == BOX_AUDIO_BUS_NONE || (send < BOX_AUDIO_BUS_MAX && position[send] > i));
    }

    if (!valid) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_ORDER_REFUSED, 0, count);
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        core->bus_active[i] = position[i] >= 0;
    }

    box_audio_copy(core->order, routes, count * (int32_t)sizeof(box_audio_route_t));
    core->order_count = count;

done:
    return;
}

/** Apply a release command.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_release(
    box_audio_core_t *core,
    const box_audio_cmd_release_t *cmd)
{
    box_audio_sound_t *s;
    if (cmd->sound >= BOX_AUDIO_SOUND_MAX ||
        core->sounds[cmd->sound].state != BOX_AUDIO_SOUND_READY)
    {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_SOUND_RELEASE, 0);
        goto done;
    }

    s = &core->sounds[cmd->sound];
    if (s->users > 0) {
        s->state = BOX_AUDIO_SOUND_RELEASED;
    } else {
        s->state = BOX_AUDIO_SOUND_FREE;
        s->data = NULL;
        box_audio_core_event(core, BOX_AUDIO_EVENT_SOUND_FREE, cmd->sound, 0);
    }

done:
    return;
}

/** Apply one command whose bytes are all within the batch.
 *
 * @param core The core
 * @param bytes The command, header included
 * @param type Its type
 * @param size Its size
 */
static void box_audio_cmd(
    box_audio_core_t *core,
    const uint8_t *bytes,
    uint16_t type,
    int32_t size)
{
    box_audio_cmd_play_t play;
    box_audio_cmd_stop_t stop;
    box_audio_cmd_set_t set;
    box_audio_cmd_release_t release;
    box_audio_voice_t *v;
    if (type == BOX_AUDIO_CMD_PLAY && size >= (int32_t)sizeof(play)) {
        box_audio_copy(&play, bytes, (int32_t)sizeof(play));
        box_audio_cmd_play(core, &play);
    } else if (type == BOX_AUDIO_CMD_STOP && size >= (int32_t)sizeof(stop)) {
        box_audio_copy(&stop, bytes, (int32_t)sizeof(stop));
        v = box_audio_voice_find(core, stop.voice);
        if (v) {
            box_audio_voice_fade(core, v, core->ramp_frames);
        }
    } else if (type == BOX_AUDIO_CMD_VOICE_SET && size >= (int32_t)sizeof(set)) {
        box_audio_copy(&set, bytes, (int32_t)sizeof(set));
        box_audio_cmd_voice_set(core, &set);
    } else if (type == BOX_AUDIO_CMD_BUS_SET && size >= (int32_t)sizeof(set)) {
        box_audio_copy(&set, bytes, (int32_t)sizeof(set));
        box_audio_cmd_bus_set(core, &set);
    } else if (type == BOX_AUDIO_CMD_ORDER && size >= (int32_t)sizeof(box_audio_cmd_order_t)) {
        box_audio_cmd_order(core, bytes, size);
    } else if (type == BOX_AUDIO_CMD_SOUND_RELEASE && size >= (int32_t)sizeof(release)) {
        box_audio_copy(&release, bytes, (int32_t)sizeof(release));
        box_audio_cmd_release(core, &release);
    } else {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, type, size);
    }
}

void box_audio_core_apply(
    box_audio_core_t *core,
    const void *commands,
    int32_t size)
{
    const uint8_t *bytes = commands;
    int32_t at = 0;
    if (!core || !bytes) {
        goto done;
    }

    while (at + (int32_t)sizeof(box_audio_cmd_header_t) <= size) {
        box_audio_cmd_header_t header;
        box_audio_copy(&header, bytes + at, (int32_t)sizeof(header));
        if (header.size < sizeof(header) || at + (int32_t)header.size > size) {
            box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, header.type, header.size);
            break;
        }

        box_audio_cmd(core, bytes + at, header.type, header.size);
        at += header.size;
    }

done:
    return;
}

box_audio_core_t* box_audio_core_new(
    const box_audio_core_desc_t *desc)
{
    box_audio_core_t *core = NULL;
    void* (*alloc)(size_t, void*) = desc ? desc->alloc : NULL;
    void (*release)(void*, void*) = desc ? desc->free : NULL;
    float sample_rate = desc ? desc->sample_rate : 0;
    int32_t i;
#ifndef BOX_AUDIO_CORE_WASM
    if (!alloc || !release) {
        alloc = box_audio_core_malloc;
        release = box_audio_core_mfree;
    }
#endif

    if (!alloc || !release) {
        goto done;
    }

    core = alloc(sizeof(box_audio_core_t), desc ? desc->ctx : NULL);
    if (!core) {
        goto done;
    }

    for (i = 0; i < (int32_t)sizeof(box_audio_core_t); i ++) {
        ((uint8_t*)core)[i] = 0;
    }

    core->alloc = alloc;
    core->free = release;
    core->ctx = desc ? desc->ctx : NULL;
    core->sample_rate = sample_rate > 0 && sample_rate < 1e7f ? sample_rate : 48000.0f;
    core->ramp_frames = (int32_t)(BOX_AUDIO_CORE_RAMP * core->sample_rate);
    core->steal_frames = (int32_t)(BOX_AUDIO_CORE_STEAL_FADE * core->sample_rate);
    core->ramp_frames = core->ramp_frames > 0 ? core->ramp_frames : 1;
    core->steal_frames = core->steal_frames > 0 ? core->steal_frames : 1;
    core->limiter_gain = 1;
    core->limiter_step = 1.0f / (BOX_AUDIO_LIMITER_RELEASE * core->sample_rate);
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        box_audio_bus_t *b = &core->buses[i];
        b->volume = 1;
        box_audio_ramp_set(&b->left, 1);
        box_audio_ramp_set(&b->right, 1);
        box_audio_ramp_set(&b->send, 0);
    }

    core->order[0].bus = 0;
    core->order[0].output = BOX_AUDIO_BUS_NONE;
    core->order[0].send = BOX_AUDIO_BUS_NONE;
    core->order_count = 1;
    core->bus_active[0] = true;
    core->event_lost_at = -1;

done:
    return core;
}

void box_audio_core_free(
    box_audio_core_t *core)
{
    if (core) {
        core->free(core, core->ctx);
    }
}

float box_audio_core_sample_rate(
    const box_audio_core_t *core)
{
    return core->sample_rate;
}

int32_t box_audio_core_voice_count(
    const box_audio_core_t *core)
{
    return core->playing;
}

bool box_audio_core_sound(
    box_audio_core_t *core,
    uint32_t id,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    bool result = false;
    box_audio_sound_t *s;
    if (!core || id >= BOX_AUDIO_SOUND_MAX || !data || frames <= 0 ||
        (channels != 1 && channels != 2) || core->sounds[id].state != BOX_AUDIO_SOUND_FREE)
    {
        goto done;
    }

    s = &core->sounds[id];
    s->data = data;
    s->frames = frames;
    s->channels = channels;
    s->rate = sample_rate > 0 && sample_rate < 1e7f ?
        (double)sample_rate / (double)core->sample_rate : 1.0;
    s->users = 0;
    s->state = BOX_AUDIO_SOUND_READY;
    result = true;

done:
    return result;
}

int32_t box_audio_core_read_events(
    box_audio_core_t *core,
    box_audio_event_t *events,
    int32_t max)
{
    int32_t count = 0;
    while (core && events && count < max) {
        if (core->event_count == 0) {
            box_audio_core_flush_free(core);
        }

        if (core->event_count == 0) {
            break;
        }

        events[count] = core->events[core->event_head];
        if (core->event_head == core->event_lost_at) {
            core->event_lost_at = -1;
            core->events_lost = 0;
        }

        core->event_head = (core->event_head + 1) % BOX_AUDIO_EVENT_MAX;
        core->event_count --;
        count ++;
    }

    if (core) {
        box_audio_core_flush_free(core);
    }

    return count;
}

int32_t box_audio_core_events_lost(
    const box_audio_core_t *core)
{
    return core->events_lost;
}

/** Render a block of a voice into its bus, and end it when its source ends or its fade does.
 *
 * @param core The core
 * @param v The voice
 * @param frames Frames of the block
 */
static void box_audio_voice_render(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    int32_t frames)
{
    bool stereo, active = v->bus < BOX_AUDIO_BUS_MAX && core->bus_active[v->bus];
    int32_t done = box_audio_source_render(core, &v->source, frames, &stereo), i;
    const float *sl = core->src_l, *sr = stereo ? core->src_r : core->src_l;
    box_audio_bus_t *b = &core->buses[active ? v->bus : 0];
    for (i = 0; i < frames; i ++) {
        float gl = box_audio_ramp_next(&v->left);
        float gr = box_audio_ramp_next(&v->right);
        if (active) {
            b->l[i] += sl[i] * gl;
            b->r[i] += sr[i] * gr;
        }
    }

    if (done < frames || (v->state == BOX_AUDIO_VOICE_STOPPING && v->left.left == 0 &&
        v->right.left == 0 && v->left.value == 0 && v->right.value == 0))
    {
        box_audio_voice_end(core, v);
    }
}

/** Run the master through the limiter into the output: the gain drops at once to keep the
 * peak at the threshold and rises back linearly, and a signal under it passes as is.
 *
 * @param core The core
 * @param l Left samples of the master, scaled by its gain
 * @param r Right samples of the master
 * @param left Receives the left output
 * @param right Receives the right output
 * @param frames Frames of the block
 */
static void box_audio_limit(
    box_audio_core_t *core,
    const float *l,
    const float *r,
    float *left,
    float *right,
    int32_t frames)
{
    float gain = core->limiter_gain, step = core->limiter_step;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float a = l[i] < 0 ? -l[i] : l[i];
        float b = r[i] < 0 ? -r[i] : r[i];
        float peak = a > b ? a : b, x, y;
        gain = gain + step < 1 ? gain + step : 1;
        if (peak > BOX_AUDIO_LIMITER_THRESHOLD && peak * gain > BOX_AUDIO_LIMITER_THRESHOLD) {
            gain = BOX_AUDIO_LIMITER_THRESHOLD / peak;
        }

        x = l[i] * gain;
        y = r[i] * gain;
        left[i] = x > 1 ? 1 : (x < -1 ? -1 : x);
        right[i] = y > 1 ? 1 : (y < -1 ? -1 : y);
    }

    core->limiter_gain = gain;
}

/** Run the buses of the order over a block: gains, then output and send, the master last.
 *
 * @param core The core
 * @param frames Frames of the block
 */
static void box_audio_buses_render(
    box_audio_core_t *core,
    int32_t frames)
{
    int32_t k, i;
    for (k = 0; k < core->order_count; k ++) {
        const box_audio_route_t *route = &core->order[k];
        box_audio_bus_t *b = &core->buses[route->bus];
        box_audio_bus_t *out = route->output != BOX_AUDIO_BUS_NONE ?
            &core->buses[route->output] : NULL;
        box_audio_bus_t *send = route->send != BOX_AUDIO_BUS_NONE ?
            &core->buses[route->send] : NULL;
        for (i = 0; i < frames; i ++) {
            float l = box_audio_clean(b->l[i] * box_audio_ramp_next(&b->left));
            float r = box_audio_clean(b->r[i] * box_audio_ramp_next(&b->right));
            float s = box_audio_ramp_next(&b->send);
            b->l[i] = l;
            b->r[i] = r;
            if (out) {
                out->l[i] += l;
                out->r[i] += r;
            }

            if (send) {
                send->l[i] += l * s;
                send->r[i] += r * s;
            }
        }
    }
}

void box_audio_core_render(
    box_audio_core_t *core,
    float *left,
    float *right,
    int32_t frames)
{
    int32_t at = 0, i, k;
    if (!core || !left || !right) {
        goto done;
    }

    while (at < frames) {
        int32_t n = frames - at < BOX_AUDIO_CORE_BLOCK ? frames - at : BOX_AUDIO_CORE_BLOCK;
        box_audio_bus_t *master = &core->buses[0];
        for (k = 0; k < core->order_count; k ++) {
            box_audio_bus_t *b = &core->buses[core->order[k].bus];
            for (i = 0; i < n; i ++) {
                b->l[i] = 0;
                b->r[i] = 0;
            }
        }

        for (k = 0; k < BOX_AUDIO_VOICE_SLOTS; k ++) {
            if (core->voices[k].state != BOX_AUDIO_VOICE_FREE) {
                box_audio_voice_render(core, &core->voices[k], n);
            }
        }

        box_audio_buses_render(core, n);
        box_audio_limit(core, master->l, master->r, left + at, right + at, n);
        at += n;
    }

done:
    return;
}
