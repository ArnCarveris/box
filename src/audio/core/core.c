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

/** Set the gain targets of a bus from its volume, pan, width, mute and send levels. A
 * pre-fader send follows the mute, a post-fader one gets it through the gains of the bus.
 *
 * @param core The core
 * @param b The bus
 */
static void box_audio_bus_gains(
    box_audio_core_t *core,
    box_audio_bus_t *b)
{
    box_audio_bus_gains_to(core, b, NULL, core->ramp_frames);
    box_audio_mods_changed(core, -1, (int32_t)(b - core->buses));
}

void box_audio_bus_gains_to(
    box_audio_core_t *core,
    box_audio_bus_t *b,
    const float *add,
    int32_t frames)
{
    float l, r, volume = b->volume, pan = b->pan, width = b->width, level;
    int32_t j;
    if (add) {
        volume = box_audio_clamp(volume + add[BOX_AUDIO_MOD_BUS_VOLUME], 0,
            BOX_AUDIO_VOLUME_MAX);
        pan = box_audio_clamp(pan + add[BOX_AUDIO_MOD_BUS_PAN], -1, 1);
        width = box_audio_clamp(width + add[BOX_AUDIO_MOD_BUS_WIDTH], 0, 2);
    }

    volume = b->mute ? 0.0f : volume;
    box_audio_pan(pan, &l, &r);
    box_audio_ramp_to(&b->left, volume * l, frames);
    box_audio_ramp_to(&b->right, volume * r, frames);
    box_audio_ramp_to(&b->side, width, frames);
    for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
        bool muted = b->mute && (b->pre_fader & (1u << j));
        level = b->send_level[j];
        if (add) {
            level = box_audio_clamp(level + add[BOX_AUDIO_MOD_BUS_SEND + j], 0,
                BOX_AUDIO_VOLUME_MAX);
        }

        box_audio_ramp_to(&b->send[j], muted ? 0.0f : level, frames);
    }
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
 * @return The voice, NULL when none plays with the handle and for handle 0, which orphaned
 *     voices have
 */
static box_audio_voice_t* box_audio_voice_find(
    box_audio_core_t *core,
    uint32_t handle)
{
    box_audio_voice_t *result = NULL;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS && !result; i ++) {
        box_audio_voice_t *v = &core->voices[i];
        if (handle && v->state == BOX_AUDIO_VOICE_PLAYING && v->handle == handle) {
            result = v;
        }
    }

    return result;
}

/** The level of a voice to pick a victim by: the gain it fades from for a fading voice, the
 * larger of its gain targets for a playing one.
 *
 * @param v The voice
 * @return The level
 */
static float box_audio_voice_victim_level(
    const box_audio_voice_t *v)
{
    return v->state == BOX_AUDIO_VOICE_STOPPING ?
        (v->left.value > v->right.value ? v->left.value : v->right.value) :
        box_audio_voice_level(v);
}

/** Whether a voice can give up what a play needs: any voice for a slot, a synth voice with
 * parts for parts, and of a playing voice only one of at most the priority of the play.
 *
 * @param v The voice
 * @param parts Whether the play needs synth parts
 * @param priority Priority of the play
 * @return Whether it can be the victim
 */
static bool box_audio_voice_victim_fits(
    const box_audio_voice_t *v,
    bool parts,
    uint32_t priority)
{
    return (v->state != BOX_AUDIO_VOICE_PLAYING || v->priority <= priority) && (!parts ||
        (v->source.kind == BOX_AUDIO_SOURCE_SYNTH && v->source.data.synth.layers > 0));
}

/** Pick the voice to take from for a new play: the quietest voice of a state, the first of
 * equals, of the playing voices only those of at most the priority of the play. Every steal,
 * of a slot, of the cap or of synth parts, picks here.
 *
 * @param core The core
 * @param state BOX_AUDIO_VOICE_PLAYING or BOX_AUDIO_VOICE_STOPPING
 * @param parts Whether only synth voices that hold parts qualify
 * @param priority Priority of the play
 * @return The voice, NULL when none qualifies
 */
static box_audio_voice_t* box_audio_voice_victim(
    box_audio_core_t *core,
    int32_t state,
    bool parts,
    uint32_t priority)
{
    box_audio_voice_t *quietest = NULL;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS; i ++) {
        box_audio_voice_t *v = &core->voices[i];
        if (v->state == state && box_audio_voice_victim_fits(v, parts, priority) && (!quietest ||
            box_audio_voice_victim_level(v) < box_audio_voice_victim_level(quietest)))
        {
            quietest = v;
        }
    }

    return quietest;
}

/** Report the steal of a playing voice.
 *
 * @param core The core
 * @param v The voice
 */
static void box_audio_voice_stolen(
    box_audio_core_t *core,
    const box_audio_voice_t *v)
{
    core->steals ++;
    box_audio_core_event(core, BOX_AUDIO_EVENT_VOICE_STOLEN, v->handle, core->steals);
}

/** Steal the playing voice with the lowest level of at most a priority, the first of equals,
 * with a short fade.
 *
 * @param core The core
 * @param priority Priority of the play
 */
static void box_audio_voice_steal(
    box_audio_core_t *core,
    uint32_t priority)
{
    box_audio_voice_t *quietest = box_audio_voice_victim(core, BOX_AUDIO_VOICE_PLAYING, false,
        priority);
    if (quietest) {
        box_audio_voice_stolen(core, quietest);
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
    box_audio_voice_t *result = NULL;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS && !result; i ++) {
        if (core->voices[i].state == BOX_AUDIO_VOICE_FREE) {
            result = &core->voices[i];
        }
    }

    if (!result) {
        result = box_audio_voice_victim(core, BOX_AUDIO_VOICE_STOPPING, false, 0);
        if (result) {
            box_audio_voice_end(core, result);
        }
    }

    return result;
}

/** Whether a new play finds a voice slot: a free one, a fading one to end, or one the cap
 * steal makes fade.
 *
 * @param core The core
 * @return Whether box_audio_voice_slot finds one after the steal of the cap
 */
static bool box_audio_voice_slot_fits(
    const box_audio_core_t *core)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS && !result; i ++) {
        result = core->voices[i].state != BOX_AUDIO_VOICE_PLAYING ||
            core->playing >= BOX_AUDIO_VOICE_MAX;
    }

    return result;
}

/** Whether the parts of the synth voices, free and held by voices that a play of a priority
 * can take from, hold the layers of a synth sound.
 *
 * @param core The core
 * @param sound Id of a ready synth sound
 * @param priority Priority of the play, BOX_AUDIO_PRIORITY_MAX for every voice
 * @return Whether box_audio_voice_reclaim frees enough
 */
static bool box_audio_voice_reclaim_fits(
    const box_audio_core_t *core,
    uint32_t sound,
    uint32_t priority)
{
    int32_t parts = core->synth_free_count, i;
    for (i = 0; i < BOX_AUDIO_VOICE_SLOTS; i ++) {
        const box_audio_voice_t *v = &core->voices[i];
        if (v->state != BOX_AUDIO_VOICE_FREE && v->source.kind == BOX_AUDIO_SOURCE_SYNTH &&
            (v->state != BOX_AUDIO_VOICE_PLAYING || v->priority <= priority))
        {
            parts += v->source.data.synth.layers;
        }
    }

    return box_audio_synth_parts(core, sound) <= parts;
}

/** End synth voices until the free parts hold the layers of a synth sound: the quietest fading
 * ones first, then the quietest playing ones of at most a priority, which report a steal.
 *
 * @param core The core
 * @param sound Id of a ready synth sound, which box_audio_voice_reclaim_fits found
 * @param priority Priority of the play
 */
static void box_audio_voice_reclaim(
    box_audio_core_t *core,
    uint32_t sound,
    uint32_t priority)
{
    int32_t need = box_audio_synth_parts(core, sound);
    while (core->synth_free_count < need) {
        box_audio_voice_t *v = box_audio_voice_victim(core, BOX_AUDIO_VOICE_STOPPING, true, 0);
        if (!v) {
            v = box_audio_voice_victim(core, BOX_AUDIO_VOICE_PLAYING, true, priority);
            if (!v) {
                break;
            }

            box_audio_voice_stolen(core, v);
        }

        box_audio_voice_end(core, v);
    }
}

/** Clamp the priority of a command.
 *
 * @param priority The priority of the command
 * @return The priority, at most BOX_AUDIO_PRIORITY_MAX
 */
static uint32_t box_audio_priority(
    uint32_t priority)
{
    return priority < BOX_AUDIO_PRIORITY_MAX ? priority : BOX_AUDIO_PRIORITY_MAX;
}

/** Refuse a new voice because every playing voice ranks higher: count it and report the end
 * of its handle, so the sender forgets it.
 *
 * @param core The core
 * @param handle Handle of the voice
 */
static void box_audio_voice_refuse(
    box_audio_core_t *core,
    uint32_t handle)
{
    core->refused ++;
    box_audio_core_event(core, BOX_AUDIO_EVENT_VOICE_END, handle, core->refused);
}

/** Whether a play of a priority finds a voice under the cap: one plays below it, or the
 * steal of the cap finds a voice of at most the priority.
 *
 * @param core The core
 * @param priority Priority of the play
 * @return Whether the play is not refused
 */
static bool box_audio_voice_priority_fits(
    box_audio_core_t *core,
    uint32_t priority)
{
    return core->playing < BOX_AUDIO_VOICE_MAX ||
        box_audio_voice_victim(core, BOX_AUDIO_VOICE_PLAYING, false, priority) != NULL;
}

/** Apply a play command. A synth voice starts at its gains, without the ramp: its envelope
 * starts from 0 itself, so an attack shorter than the ramp keeps its time. A sample voice with
 * BOX_AUDIO_PLAY_ATTACK starts at its gains too.
 *
 * A play that every playing voice outranks at the cap, or whose synth parts only voices of a
 * higher priority hold, is refused.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_play(
    box_audio_core_t *core,
    const box_audio_cmd_play_t *cmd)
{
    box_audio_voice_t *v = NULL;
    uint32_t priority = box_audio_priority(cmd->priority);
    bool synth;
    if (cmd->sound >= BOX_AUDIO_SOUND_MAX || cmd->bus >= BOX_AUDIO_BUS_MAX ||
        core->sounds[cmd->sound].state != BOX_AUDIO_SOUND_READY)
    {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_PLAY, 0);
        goto done;
    }

    synth = core->sounds[cmd->sound].synth != 0;
    if (!box_audio_voice_slot_fits(core) || (synth && (!box_audio_synth_strings_fit(core,
        cmd->sound) || !box_audio_voice_reclaim_fits(core, cmd->sound,
            BOX_AUDIO_PRIORITY_MAX))))
    {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_PLAY, 0);
        goto done;
    }

    if (!box_audio_voice_priority_fits(core, priority) ||
        (synth && !box_audio_voice_reclaim_fits(core, cmd->sound, priority)))
    {
        box_audio_voice_refuse(core, cmd->voice);
        goto done;
    }

    if (synth) {
        box_audio_voice_reclaim(core, cmd->sound, priority);
    }

    if (core->playing >= BOX_AUDIO_VOICE_MAX) {
        box_audio_voice_steal(core, priority);
    }

    v = box_audio_voice_slot(core);
    if (!v || (synth && !box_audio_synth_fits(core, cmd->sound))) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_PLAY, 0);
        goto done;
    }

    v->state = BOX_AUDIO_VOICE_PLAYING;
    v->handle = cmd->voice;
    v->bus = cmd->bus;
    v->volume = box_audio_clamp(cmd->volume, 0, BOX_AUDIO_VOLUME_MAX);
    v->pan = box_audio_clamp(cmd->pan, -1, 1);
    v->pitch = box_audio_clamp(cmd->pitch, BOX_AUDIO_PITCH_MIN, BOX_AUDIO_PITCH_MAX);
    v->priority = priority;
    box_audio_ramp_set(&v->left, 0);
    box_audio_ramp_set(&v->right, 0);
    box_audio_voice_express_reset(core, v, false);
    box_audio_voice_gains(core, v);
    if (core->sounds[cmd->sound].synth) {
        box_audio_ramp_set(&v->left, v->left.target);
        box_audio_ramp_set(&v->right, v->right.target);
        box_audio_source_synth(core, &v->source, cmd->sound, v->pitch,
            box_audio_clamp(cmd->velocity, 0, 1), cmd->beat, cmd->tempo,
            (cmd->flags & BOX_AUDIO_PLAY_LOOP) != 0,
            (cmd->flags & BOX_AUDIO_PLAY_GATE) != 0);
    } else {
        if (cmd->flags & BOX_AUDIO_PLAY_ATTACK) {
            box_audio_ramp_set(&v->left, v->left.target);
            box_audio_ramp_set(&v->right, v->right.target);
        }

        box_audio_source_sample(core, &v->source, cmd->sound, v->pitch,
            (cmd->flags & BOX_AUDIO_PLAY_LOOP) != 0);
    }

    core->playing ++;

done:
    return;
}

/** Apply a legato command: retarget the synth voice it follows and give it the new handle, or
 * take the handle from that voice and start a new one. A synth voice that lost its handle
 * stops looping, rings out its release and reports its end with handle 0; any other fades out.
 *
 * A command of a sound that is not ready leaves the voice as it is, with the new handle. A
 * retargeted voice takes the BOX_AUDIO_PLAY_GATE of the command: with it the voice sustains
 * until a release, without it the voice holds for the hold of its envelopes.
 *
 * A retargeted voice keeps the velocity of the note that started it, since its envelopes do
 * not restart.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_legato(
    box_audio_core_t *core,
    const box_audio_cmd_legato_t *cmd)
{
    box_audio_cmd_play_t play = {
        .voice = cmd->voice, .sound = cmd->sound, .bus = cmd->bus, .volume = cmd->volume,
        .pan = cmd->pan, .pitch = cmd->pitch, .flags = cmd->flags,
        .priority = cmd->priority, .velocity = cmd->velocity, .beat = cmd->beat,
        .tempo = cmd->tempo };
    box_audio_voice_t *v = box_audio_voice_find(core, cmd->from);
    float pitch = box_audio_clamp(cmd->pitch, BOX_AUDIO_PITCH_MIN, BOX_AUDIO_PITCH_MAX);
    bool ready = cmd->sound < BOX_AUDIO_SOUND_MAX && cmd->bus < BOX_AUDIO_BUS_MAX &&
        core->sounds[cmd->sound].state == BOX_AUDIO_SOUND_READY;
    bool synth = ready && v && v->source.kind == BOX_AUDIO_SOURCE_SYNTH &&
        core->sounds[cmd->sound].synth;
    if (!ready) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_LEGATO, 0);
        if (v) {
            v->handle = cmd->voice;
        }
    } else if (synth && box_audio_synth_legato(core, &v->source.data.synth, cmd->sound, pitch)) {
        v->handle = cmd->voice;
        v->bus = cmd->bus;
        v->volume = box_audio_clamp(cmd->volume, 0, BOX_AUDIO_VOLUME_MAX);
        v->pan = box_audio_clamp(cmd->pan, -1, 1);
        v->pitch = pitch;
        v->priority = box_audio_priority(cmd->priority);
        box_audio_voice_gains(core, v);
        box_audio_synth_gate(&v->source.data.synth, (cmd->flags & BOX_AUDIO_PLAY_GATE) != 0);
        box_audio_voice_express_reset(core, v, true);
    } else {
        if (v && v->source.kind == BOX_AUDIO_SOURCE_SYNTH) {
            v->source.data.synth.loop = false;
        } else if (v) {
            box_audio_voice_fade(core, v, core->ramp_frames);
        }

        if (v) {
            v->handle = 0;
        }

        box_audio_cmd_play(core, &play);
    }
}

/** Apply a release command: a synth voice releases its envelopes, a sample or stream voice
 * fades out over the fade of the command and ends. A voice that ended is not found.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_voice_release(
    box_audio_core_t *core,
    const box_audio_cmd_voice_release_t *cmd)
{
    box_audio_voice_t *v = box_audio_voice_find(core, cmd->voice);
    float fade;
    if (!v) {
        goto done;
    }

    if (v->source.kind == BOX_AUDIO_SOURCE_SYNTH) {
        box_audio_synth_release(&v->source.data.synth);
    } else {
        fade = box_audio_clamp(cmd->fade, BOX_AUDIO_RELEASE_FADE_MIN,
            BOX_AUDIO_RELEASE_FADE_MAX);
        box_audio_voice_fade(core, v, (int32_t)(fade * core->sample_rate + 0.5f));
    }

done:
    return;
}

/** Apply a stream command: open the stream and start a voice that reads it.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_stream(
    box_audio_core_t *core,
    const box_audio_cmd_stream_t *cmd)
{
    box_audio_voice_t *v = NULL;
    box_audio_stream_t *s;
    uint32_t priority = box_audio_priority(cmd->priority);
    int32_t i;
    if (cmd->stream >= BOX_AUDIO_STREAM_MAX || cmd->bus >= BOX_AUDIO_BUS_MAX ||
        (cmd->channels != 1 && cmd->channels != 2) || core->streams[cmd->stream].open)
    {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_STREAM, 0);
        goto done;
    }

    if (!box_audio_voice_priority_fits(core, priority)) {
        box_audio_voice_refuse(core, cmd->voice);
        goto done;
    }

    if (core->playing >= BOX_AUDIO_VOICE_MAX) {
        box_audio_voice_steal(core, priority);
    }

    v = box_audio_voice_slot(core);
    if (!v) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_STREAM, 0);
        goto done;
    }

    s = &core->streams[cmd->stream];
    for (i = 0; i < BOX_AUDIO_STREAM_CHUNKS; i ++) {
        s->chunks[i].data = NULL;
        s->chunks[i].frames = 0;
    }

    s->head = 0;
    s->count = 0;
    s->at = 0;
    s->avail = 0;
    s->channels = (int32_t)cmd->channels;
    s->rate = cmd->sample_rate > 0 && cmd->sample_rate < 1e7f ?
        (double)cmd->sample_rate / (double)core->sample_rate : 1.0;
    s->voice = cmd->voice;
    s->open = true;
    s->last = false;
    s->ended = false;
    s->consumed = 0;
    s->underruns = 0;
    s->starved = false;

    v->state = BOX_AUDIO_VOICE_PLAYING;
    v->handle = cmd->voice;
    v->bus = cmd->bus;
    v->volume = box_audio_clamp(cmd->volume, 0, BOX_AUDIO_VOLUME_MAX);
    v->pan = 0;
    v->pitch = 1;
    v->priority = priority;
    box_audio_ramp_set(&v->left, 0);
    box_audio_ramp_set(&v->right, 0);
    box_audio_voice_express_reset(core, v, false);
    box_audio_voice_gains(core, v);
    box_audio_source_stream(core, &v->source, cmd->stream);
    core->playing ++;

done:
    return;
}

/** Apply a ramp command of a voice.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_voice_ramp(
    box_audio_core_t *core,
    const box_audio_cmd_ramp_t *cmd)
{
    box_audio_voice_t *v = box_audio_voice_find(core, cmd->voice);
    if (!v) {
        goto done;
    }

    if (cmd->param == BOX_AUDIO_PARAM_BEND || cmd->param == BOX_AUDIO_PARAM_MOD ||
        cmd->param == BOX_AUDIO_PARAM_PRESSURE)
    {
        box_audio_voice_ramp(core, v, cmd->param, cmd->value, cmd->frames);
    } else {
        box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_VOICE_RAMP, 0);
    }

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
    } else if (cmd->param == BOX_AUDIO_PARAM_BEND || cmd->param == BOX_AUDIO_PARAM_MOD ||
        cmd->param == BOX_AUDIO_PARAM_PRESSURE)
    {
        box_audio_voice_ramp(core, v, cmd->param, value, 0);
    } else if (cmd->param == BOX_AUDIO_PARAM_BUS && value >= 0 && value < BOX_AUDIO_BUS_MAX) {
        v->bus = (uint32_t)value;
    } else if (cmd->param == BOX_AUDIO_PARAM_POSITION) {
        box_audio_source_seek(core, &v->source, value);
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
        b->send_level[0] = box_audio_clamp(value, 0, BOX_AUDIO_VOLUME_MAX);
    } else if (cmd->param >= BOX_AUDIO_PARAM_SEND_1 && cmd->param <= BOX_AUDIO_PARAM_SEND_3) {
        b->send_level[cmd->param - BOX_AUDIO_PARAM_SEND_1 + 1] =
            box_audio_clamp(value, 0, BOX_AUDIO_VOLUME_MAX);
    } else if (cmd->param == BOX_AUDIO_PARAM_WIDTH) {
        b->width = box_audio_clamp(value, 0, 2);
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
    int32_t i, j, count;
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
        routes[count - 1].output == BOX_AUDIO_BUS_NONE;
    for (j = 0; j < BOX_AUDIO_SENDS && valid; j ++) {
        valid = routes[count - 1].sends[j] == BOX_AUDIO_BUS_NONE;
    }

    for (i = 0; i < count - 1 && valid; i ++) {
        uint8_t out = routes[i].output;
        valid = out < BOX_AUDIO_BUS_MAX && position[out] > i;
        for (j = 0; j < BOX_AUDIO_SENDS && valid; j ++) {
            uint8_t send = routes[i].sends[j];
            valid = send == BOX_AUDIO_BUS_NONE ||
                (send < BOX_AUDIO_BUS_MAX && position[send] > i);
        }
    }

    if (!valid) {
        box_audio_core_event(core, BOX_AUDIO_EVENT_ORDER_REFUSED, 0, count);
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        core->bus_active[i] = position[i] >= 0;
        core->bus_position[i] = (uint8_t)(position[i] >= 0 ? position[i] : 0);
    }

    box_audio_effects_order(core);

    for (i = 0; i < count; i ++) {
        box_audio_bus_t *b = &core->buses[routes[i].bus];
        uint8_t pre = 0;
        for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
            if (routes[i].sends[j] != BOX_AUDIO_BUS_NONE && (routes[i].pre_fader & (1u << j))) {
                pre |= (uint8_t)(1u << j);
            }
        }

        if (b->pre_fader != pre) {
            b->pre_fader = pre;
            box_audio_bus_gains(core, b);
        }
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
    if (s->synth) {
        core->synths[s->synth - 1].used = false;
        s->synth = 0;
    }

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

/** Apply a command that takes effect on a frame, now.
 *
 * @param core The core
 * @param cmd The command
 */
static void box_audio_cmd_timed(
    box_audio_core_t *core,
    const box_audio_timed_cmd_t *cmd)
{
    box_audio_voice_t *v;
    if (cmd->header.type == BOX_AUDIO_CMD_PLAY) {
        box_audio_cmd_play(core, &cmd->play);
    } else if (cmd->header.type == BOX_AUDIO_CMD_STOP) {
        v = box_audio_voice_find(core, cmd->stop.voice);
        if (v) {
            box_audio_voice_fade(core, v, core->ramp_frames);
        }
    } else if (cmd->header.type == BOX_AUDIO_CMD_VOICE_SET) {
        box_audio_cmd_voice_set(core, &cmd->set);
    } else if (cmd->header.type == BOX_AUDIO_CMD_RELEASE) {
        box_audio_cmd_voice_release(core, &cmd->release);
    } else if (cmd->header.type == BOX_AUDIO_CMD_LEGATO) {
        box_audio_cmd_legato(core, &cmd->legato);
    } else if (cmd->header.type == BOX_AUDIO_CMD_EFFECT_PARAMS) {
        box_audio_effect_params_command(core, &cmd->effect);
    } else if (cmd->header.type == BOX_AUDIO_CMD_VOICE_RAMP) {
        box_audio_cmd_voice_ramp(core, &cmd->ramp);
    } else if (cmd->header.type == BOX_AUDIO_CMD_BEAT) {
        box_audio_beat_command(core, &cmd->beat);
    } else if (cmd->header.type == BOX_AUDIO_CMD_MACRO) {
        box_audio_macro_command(core, &cmd->macro);
    } else {
        box_audio_cmd_bus_set(core, &cmd->set);
    }
}

/** Rank of a command among the commands of its frame: the ramps after the others, so a ramp
 * at the frame of a play applies to the voice it starts, and the releases last.
 *
 * @param type The type of the command
 * @return The rank, lower first
 */
static int32_t box_audio_schedule_rank(
    uint16_t type)
{
    return type == BOX_AUDIO_CMD_RELEASE ? 2 : (type == BOX_AUDIO_CMD_VOICE_RAMP ? 1 : 0);
}

/** Whether a command of the schedule comes before another: an earlier frame, or the same
 * frame and a lower rank, or the same rank and an earlier arrival. The releases come last, so a
 * legato note on the frame where the note it follows ends glides on instead of restarting.
 *
 * @param a A command
 * @param b Another command
 * @return Whether a comes first
 */
static bool box_audio_schedule_before(
    const box_audio_scheduled_t *a,
    const box_audio_scheduled_t *b)
{
    int32_t ra = box_audio_schedule_rank(a->cmd.header.type);
    int32_t rb = box_audio_schedule_rank(b->cmd.header.type);

    return a->when < b->when || (a->when == b->when && (ra != rb ? ra < rb :
        a->order < b->order));
}

/** Move a command of the schedule down the heap to its place.
 *
 * @param core The core
 * @param at Index of the command
 */
static void box_audio_schedule_down(
    box_audio_core_t *core,
    int32_t at)
{
    box_audio_scheduled_t *heap = core->schedule, e = heap[at];
    int32_t count = core->schedule_count;
    for (;;) {
        int32_t child = at * 2 + 1;
        if (child >= count) {
            break;
        }

        if (child + 1 < count && box_audio_schedule_before(&heap[child + 1], &heap[child])) {
            child ++;
        }

        if (!box_audio_schedule_before(&heap[child], &e)) {
            break;
        }

        heap[at] = heap[child];
        at = child;
    }

    heap[at] = e;
}

/** Put a command into the schedule, which has room.
 *
 * @param core The core
 * @param when Frame it applies at
 * @param cmd The command
 */
static void box_audio_schedule_push(
    box_audio_core_t *core,
    uint64_t when,
    const box_audio_timed_cmd_t *cmd)
{
    box_audio_scheduled_t *heap = core->schedule, e;
    int32_t at = core->schedule_count ++;
    e.when = when;
    e.order = core->schedule_order ++;
    e.cmd = *cmd;
    while (at > 0) {
        int32_t parent = (at - 1) / 2;
        if (!box_audio_schedule_before(&e, &heap[parent])) {
            break;
        }

        heap[at] = heap[parent];
        at = parent;
    }

    heap[at] = e;
}

/** Apply the commands of the schedule whose frame the clock reached, in order.
 *
 * @param core The core
 */
static void box_audio_schedule_run(
    box_audio_core_t *core)
{
    while (core->schedule_count && core->schedule[0].when <= core->clock) {
        box_audio_timed_cmd_t cmd = core->schedule[0].cmd;
        core->schedule_count --;
        if (core->schedule_count) {
            core->schedule[0] = core->schedule[core->schedule_count];
            box_audio_schedule_down(core, 0);
        }

        box_audio_cmd_timed(core, &cmd);
    }
}

/** Drop the commands of the schedule for a voice handle: its plays, stops, sets, releases and
 * the legato notes that would give it the handle.
 *
 * @param core The core
 * @param voice The handle
 */
static void box_audio_schedule_cancel(
    box_audio_core_t *core,
    uint32_t voice)
{
    int32_t i, kept = 0;
    for (i = 0; i < core->schedule_count; i ++) {
        const box_audio_timed_cmd_t *cmd = &core->schedule[i].cmd;
        uint16_t type = cmd->header.type;
        bool drop = (type == BOX_AUDIO_CMD_PLAY && cmd->play.voice == voice) ||
            (type == BOX_AUDIO_CMD_STOP && cmd->stop.voice == voice) ||
            (type == BOX_AUDIO_CMD_VOICE_SET && cmd->set.target == voice) ||
            (type == BOX_AUDIO_CMD_RELEASE && cmd->release.voice == voice) ||
            (type == BOX_AUDIO_CMD_LEGATO && cmd->legato.voice == voice) ||
            (type == BOX_AUDIO_CMD_VOICE_RAMP && cmd->ramp.voice == voice);
        if (!drop) {
            if (kept != i) {
                core->schedule[kept] = core->schedule[i];
            }

            kept ++;
        }
    }

    if (kept != core->schedule_count) {
        core->schedule_count = kept;
        for (i = kept / 2 - 1; i >= 0; i --) {
            box_audio_schedule_down(core, i);
        }
    }
}

/** Drop the commands of the schedule that a cancel command names: the sets of a parameter of
 * a bus, or the parameters of an effect slot.
 *
 * @param core The core
 * @param cmd The cancel command
 */
static void box_audio_schedule_cancel_sets(
    box_audio_core_t *core,
    const box_audio_cmd_cancel_t *cmd)
{
    uint64_t from = ((uint64_t)cmd->param << 32) | cmd->target;
    int32_t i, kept = 0;
    for (i = 0; i < core->schedule_count; i ++) {
        const box_audio_timed_cmd_t *c = &core->schedule[i].cmd;
        uint16_t type = c->header.type;
        bool drop = (cmd->type == BOX_AUDIO_CMD_BEAT && type == BOX_AUDIO_CMD_BEAT &&
            core->schedule[i].when >= from) ||
            (cmd->type == BOX_AUDIO_CMD_BUS_SET && type == BOX_AUDIO_CMD_BUS_SET &&
            c->set.target == cmd->target && c->set.param == cmd->param) ||
            (cmd->type == BOX_AUDIO_CMD_EFFECT_PARAMS && type == BOX_AUDIO_CMD_EFFECT_PARAMS &&
            c->effect.slot == cmd->target) ||
            (cmd->type == BOX_AUDIO_CMD_VOICE_RAMP && type == BOX_AUDIO_CMD_VOICE_RAMP &&
            c->ramp.voice == cmd->target) ||
            (cmd->type == BOX_AUDIO_CMD_MACRO && type == BOX_AUDIO_CMD_MACRO &&
            c->macro.macro == cmd->target);
        if (!drop) {
            if (kept != i) {
                core->schedule[kept] = core->schedule[i];
            }

            kept ++;
        }
    }

    if (kept != core->schedule_count) {
        core->schedule_count = kept;
        for (i = kept / 2 - 1; i >= 0; i --) {
            box_audio_schedule_down(core, i);
        }
    }
}

/** Take a command that takes effect on a frame: into the schedule when its frame is to come,
 * else at once. A stop that applies at once drops the scheduled commands of its handle first.
 * A full schedule applies the command at once and reports it late.
 *
 * @param core The core
 * @param bytes The command, header included
 * @param size Bytes of its type
 */
static void box_audio_cmd_when(
    box_audio_core_t *core,
    const uint8_t *bytes,
    int32_t size)
{
    box_audio_timed_cmd_t cmd;
    uint64_t when;
    box_audio_copy(&cmd, bytes, size);
    when = ((uint64_t)cmd.play.when_hi << 32) | cmd.play.when_lo;
    if (when <= core->clock) {
        if (cmd.header.type == BOX_AUDIO_CMD_STOP && core->schedule_count) {
            box_audio_schedule_cancel(core, cmd.stop.voice);
        }

        box_audio_cmd_timed(core, &cmd);
    } else if (core->schedule_count < BOX_AUDIO_SCHEDULE_MAX) {
        box_audio_schedule_push(core, when, &cmd);
    } else {
        box_audio_core_event(core, BOX_AUDIO_EVENT_LATE, cmd.header.type, 0);
        box_audio_cmd_timed(core, &cmd);
    }
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
    box_audio_cmd_release_t release;
    box_audio_cmd_stream_t stream;
    box_audio_cmd_effect_t effect;
    box_audio_cmd_cancel_t cancel;
    box_audio_cmd_synth_t synth;
    box_audio_cmd_modulator_t modulator;
    box_audio_cmd_macro_map_t map;
    uint32_t layers;
    if (type == BOX_AUDIO_CMD_PLAY && size >= (int32_t)sizeof(box_audio_cmd_play_t)) {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_play_t));
    } else if (type == BOX_AUDIO_CMD_LEGATO && size >= (int32_t)sizeof(box_audio_cmd_legato_t)) {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_legato_t));
    } else if (type == BOX_AUDIO_CMD_RELEASE &&
        size >= (int32_t)sizeof(box_audio_cmd_voice_release_t))
    {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_voice_release_t));
    } else if (type == BOX_AUDIO_CMD_STOP && size >= (int32_t)sizeof(box_audio_cmd_stop_t)) {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_stop_t));
    } else if ((type == BOX_AUDIO_CMD_VOICE_SET || type == BOX_AUDIO_CMD_BUS_SET) &&
        size >= (int32_t)sizeof(box_audio_cmd_set_t))
    {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_set_t));
    } else if (type == BOX_AUDIO_CMD_EFFECT_PARAMS &&
        size >= (int32_t)sizeof(box_audio_cmd_effect_params_t))
    {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_effect_params_t));
    } else if (type == BOX_AUDIO_CMD_VOICE_RAMP &&
        size >= (int32_t)sizeof(box_audio_cmd_ramp_t))
    {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_ramp_t));
    } else if (type == BOX_AUDIO_CMD_BEAT && size >= (int32_t)sizeof(box_audio_cmd_beat_t)) {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_beat_t));
    } else if (type == BOX_AUDIO_CMD_MACRO && size >= (int32_t)sizeof(box_audio_cmd_macro_t)) {
        box_audio_cmd_when(core, bytes, (int32_t)sizeof(box_audio_cmd_macro_t));
    } else if (type == BOX_AUDIO_CMD_MODULATOR && size >= (int32_t)sizeof(modulator)) {
        box_audio_copy(&modulator, bytes, (int32_t)sizeof(modulator));
        box_audio_mod_command(core, &modulator);
    } else if (type == BOX_AUDIO_CMD_MACRO_MAP && size >= (int32_t)sizeof(map)) {
        box_audio_copy(&map, bytes, (int32_t)sizeof(map));
        box_audio_macro_map_command(core, &map);
    } else if (type == BOX_AUDIO_CMD_CANCEL && size >= (int32_t)sizeof(cancel)) {
        box_audio_copy(&cancel, bytes, (int32_t)sizeof(cancel));
        box_audio_schedule_cancel_sets(core, &cancel);
    } else if (type == BOX_AUDIO_CMD_ORDER && size >= (int32_t)sizeof(box_audio_cmd_order_t)) {
        box_audio_cmd_order(core, bytes, size);
    } else if (type == BOX_AUDIO_CMD_SOUND_RELEASE && size >= (int32_t)sizeof(release)) {
        box_audio_copy(&release, bytes, (int32_t)sizeof(release));
        box_audio_cmd_release(core, &release);
    } else if (type == BOX_AUDIO_CMD_STREAM && size >= (int32_t)sizeof(stream)) {
        box_audio_copy(&stream, bytes, (int32_t)sizeof(stream));
        box_audio_cmd_stream(core, &stream);
    } else if (type == BOX_AUDIO_CMD_EFFECT && size >= (int32_t)sizeof(effect)) {
        box_audio_copy(&effect, bytes, (int32_t)sizeof(effect));
        box_audio_effect_command(core, &effect);
    } else if (type == BOX_AUDIO_CMD_SYNTH && size >= BOX_AUDIO_CMD_SYNTH_SIZE(0)) {
        box_audio_copy(&synth, bytes, BOX_AUDIO_CMD_SYNTH_SIZE(0));
        layers = synth.patch.layers < BOX_AUDIO_SYNTH_LAYERS ? synth.patch.layers :
            BOX_AUDIO_SYNTH_LAYERS;
        if (size >= BOX_AUDIO_CMD_SYNTH_SIZE(layers)) {
            box_audio_copy(synth.patch.layer, bytes + BOX_AUDIO_CMD_SYNTH_SIZE(0),
                BOX_AUDIO_CMD_SYNTH_SIZE(layers) - BOX_AUDIO_CMD_SYNTH_SIZE(0));
            box_audio_synth_command(core, &synth);
        } else {
            box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, type, size);
        }
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
    int32_t i, k;
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
        b->width = 1;
        box_audio_ramp_set(&b->left, 1);
        box_audio_ramp_set(&b->right, 1);
        box_audio_ramp_set(&b->side, 1);
        for (k = 0; k < BOX_AUDIO_SENDS; k ++) {
            box_audio_ramp_set(&b->send[k], 0);
        }
    }

    core->order[0].bus = 0;
    core->order[0].output = BOX_AUDIO_BUS_NONE;
    for (k = 0; k < BOX_AUDIO_SENDS; k ++) {
        core->order[0].sends[k] = BOX_AUDIO_BUS_NONE;
    }

    core->order_count = 1;
    core->bus_active[0] = true;
    core->event_lost_at = -1;
    box_audio_effects_init(core);
    box_audio_synth_parts_init(core);
    box_audio_mods_init(core);

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

int32_t box_audio_core_refused(
    const box_audio_core_t *core)
{
    return core->refused;
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
    s->synth = 0;
    s->table = box_audio_table_frames(data, frames, channels);
    result = true;

done:
    return result;
}

bool box_audio_core_stream_chunk(
    box_audio_core_t *core,
    uint32_t stream,
    const float *data,
    int32_t frames,
    bool last)
{
    bool result = false;
    box_audio_stream_t *s;
    box_audio_chunk_t *c;
    if (!core || stream >= BOX_AUDIO_STREAM_MAX || !data || frames <= 0 ||
        frames > BOX_AUDIO_STREAM_CHUNK)
    {
        goto done;
    }

    s = &core->streams[stream];
    if (!s->open || s->last || s->count >= BOX_AUDIO_STREAM_CHUNKS) {
        goto done;
    }

    c = &s->chunks[(s->head + s->count) % BOX_AUDIO_STREAM_CHUNKS];
    c->data = data;
    c->frames = frames;
    s->count ++;
    s->avail += frames;
    s->last = last;
    result = true;

done:
    return result;
}

void box_audio_core_stream_status(
    const box_audio_core_t *core,
    uint32_t stream,
    box_audio_stream_status_t *status)
{
    const box_audio_stream_t *s;
    *status = (box_audio_stream_status_t){0};
    if (!core || stream >= BOX_AUDIO_STREAM_MAX) {
        goto done;
    }

    s = &core->streams[stream];
    status->voice = s->voice;
    status->open = s->open;
    status->ended = s->ended;
    status->consumed = s->consumed;
    status->at = s->at;
    status->underruns = s->underruns;
    status->starved = s->starved;

done:
    return;
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

uint64_t box_audio_core_clock(
    const box_audio_core_t *core)
{
    return core ? core->clock : 0;
}

int32_t box_audio_core_scheduled(
    const box_audio_core_t *core)
{
    return core ? core->schedule_count : 0;
}

int32_t box_audio_core_meters(
    box_audio_core_t *core,
    box_audio_meter_t *meters,
    int32_t max)
{
    int32_t i, result = 0;
    if (!core || !meters || max <= 0) {
        goto done;
    }

    result = max < BOX_AUDIO_BUS_MAX ? max : BOX_AUDIO_BUS_MAX;
    for (i = 0; i < result; i ++) {
        meters[i] = core->meters[i];
        core->meters[i] = (box_audio_meter_t){0};
    }

done:
    return result;
}

int32_t box_audio_core_effect_meters(
    box_audio_core_t *core,
    box_audio_effect_meter_t *meters,
    int32_t max)
{
    int32_t i, result = 0;
    if (!core || !meters || max <= 0) {
        goto done;
    }

    result = max < BOX_AUDIO_EFFECT_MAX ? max : BOX_AUDIO_EFFECT_MAX;
    for (i = 0; i < result; i ++) {
        meters[i] = core->effect_meters[i];
        core->effect_meters[i] = (box_audio_effect_meter_t){0};
    }

done:
    return result;
}

/** Render frames of the source of a voice into its bus from a frame of the block.
 *
 * @param core The core
 * @param v The voice
 * @param b The bus
 * @param active Whether the bus is active, else the frames only advance the gains
 * @param at First frame of the block
 * @param frames Frames to render
 * @return Frames the source rendered
 */
static int32_t box_audio_voice_mix(
    box_audio_core_t *core,
    box_audio_voice_t *v,
    box_audio_bus_t *b,
    bool active,
    int32_t at,
    int32_t frames)
{
    bool stereo;
    int32_t done = box_audio_source_render(core, &v->source, frames, &stereo), i;
    const float *sl = core->src_l, *sr = stereo ? core->src_r : core->src_l;
    for (i = 0; i < frames; i ++) {
        float gl = box_audio_ramp_next(&v->left);
        float gr = box_audio_ramp_next(&v->right);
        if (active) {
            b->l[at + i] += sl[i] * gl;
            b->r[at + i] += sr[i] * gr;
        }
    }

    return done;
}

/** Render a block of a voice into its bus, and end it when its source ends or its fade does.
 * While a ramp of its expression runs, the source renders in pieces of
 * BOX_AUDIO_EXPRESS_FRAMES and takes the values of the ramps after each.
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
    bool active = v->bus < BOX_AUDIO_BUS_MAX && core->bus_active[v->bus];
    box_audio_bus_t *b = &core->buses[active ? v->bus : 0];
    int32_t done, at, n, k;
    if (!v->expressive) {
        done = box_audio_voice_mix(core, v, b, active, 0, frames);
    } else {
        for (at = 0, done = 0; at < frames; at += n) {
            n = frames - at < BOX_AUDIO_EXPRESS_FRAMES ? frames - at : BOX_AUDIO_EXPRESS_FRAMES;
            k = box_audio_voice_mix(core, v, b, active, at, n);
            done += k;
            if (v->expressive) {
                box_audio_voice_express_next(core, v, n);
            }

            if (k < n) {
                break;
            }
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

/** Apply the width of a bus to a block: a gain of its side signal, nothing at a width of 1.
 *
 * @param b The bus
 * @param frames Frames of the block
 */
static void box_audio_bus_width(
    box_audio_bus_t *b,
    int32_t frames)
{
    int32_t i;
    if (b->side.left == 0 && b->side.value == 1.0f) {
        goto done;
    }

    for (i = 0; i < frames; i ++) {
        float w = box_audio_ramp_next(&b->side);
        float mid = 0.5f * (b->l[i] + b->r[i]), side = 0.5f * (b->l[i] - b->r[i]) * w;
        b->l[i] = mid + side;
        b->r[i] = mid - side;
    }

done:
    return;
}

/** Add the sends of a bus of one kind, pre-fader or post-fader, to their buses. A send
 * without a bus only advances its ramp.
 *
 * @param core The core
 * @param route The route of the bus
 * @param b The bus, with the samples the sends tap
 * @param pre Whether to add the pre-fader sends, else the post-fader ones
 * @param frames Frames of the block
 */
static void box_audio_bus_sends(
    box_audio_core_t *core,
    const box_audio_route_t *route,
    box_audio_bus_t *b,
    bool pre,
    int32_t frames)
{
    int32_t i, j;
    for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
        box_audio_ramp_t *ramp = &b->send[j];
        box_audio_bus_t *send;
        if (((b->pre_fader & (1u << j)) != 0) != pre) {
            continue;
        }

        if (route->sends[j] == BOX_AUDIO_BUS_NONE) {
            box_audio_ramp_skip(ramp, frames);
            continue;
        }

        send = &core->buses[route->sends[j]];
        for (i = 0; i < frames; i ++) {
            float s = box_audio_ramp_next(ramp);
            send->l[i] += b->l[i] * s;
            send->r[i] += b->r[i] * s;
        }
    }
}

/** Run the buses of the order over a block: inserts, width, pre-fader sends, gains, meter,
 * output and post-fader sends, the master last.
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
        box_audio_meter_t *m = &core->meters[route->bus];
        float peak_l = m->peak_left, peak_r = m->peak_right, square_l = 0, square_r = 0;
        if ((core->mods.keys >> route->bus) & 1) {
            box_audio_mods_follow(core, route->bus, b->l, b->r, frames);
        }

        if (core->bus_effects[route->bus] || (!route->bus && core->effect_tails)) {
            box_audio_effects_render(core, route->bus, b->l, b->r, frames);
        }

        box_audio_bus_width(b, frames);
        if (b->pre_fader) {
            box_audio_bus_sends(core, route, b, true, frames);
        }

        for (i = 0; i < frames; i ++) {
            float l = box_audio_clean(b->l[i] * box_audio_ramp_next(&b->left));
            float r = box_audio_clean(b->r[i] * box_audio_ramp_next(&b->right));
            float al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            b->l[i] = l;
            b->r[i] = r;
            peak_l = al > peak_l ? al : peak_l;
            peak_r = ar > peak_r ? ar : peak_r;
            square_l += l * l;
            square_r += r * r;
            if (out) {
                out->l[i] += l;
                out->r[i] += r;
            }
        }

        m->peak_left = peak_l;
        m->peak_right = peak_r;
        m->square_left += (double)square_l;
        m->square_right += (double)square_r;
        m->frames += (uint32_t)frames;

        box_audio_bus_sends(core, route, b, false, frames);
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
        if (core->schedule_count) {
            box_audio_schedule_run(core);
            if (core->schedule_count && core->schedule[0].when - core->clock < (uint64_t)n) {
                n = (int32_t)(core->schedule[0].when - core->clock);
            }
        }

        if (core->mods.on || core->mods.maps || core->mods.buses || core->mods.effects) {
            n = box_audio_mods_block(core, n);
        }

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
        core->clock += (uint64_t)n;
        at += n;
    }

    if (core->schedule_count) {
        box_audio_schedule_run(core);
    }

done:
    return;
}
