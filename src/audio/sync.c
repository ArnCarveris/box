/**
 * @file audio/sync.c
 * @brief The system that turns the changes of the audio components into one command batch per
 * frame: bus parameters and the processing order, sounds (AudioPcm and the decoded samples of
 * sound files), sources, plays, stops and releases.
 *
 * The system remembers what it sent to the output and writes a command only for what differs,
 * so a frame without changes sends nothing. A new output (a new serial) gets everything again.
 */

#include "module.h"
#include <math.h>

/** Make room in the batch for one more command.
 *
 * @param state The state
 * @return Whether the batch has room
 */
static bool box_audio_reserve(
    box_audio_module_t *state)
{
    box_audio_writer_t *w = &state->writer;
    bool result = true;
    if (w->capacity - w->size < BOX_AUDIO_COMMAND_RESERVE) {
        int32_t capacity = w->capacity ? w->capacity * 2 : 4096;
        uint8_t *data = ecs_os_realloc(w->data, capacity);
        if (data) {
            w->data = data;
            w->capacity = capacity;
        } else {
            result = false;
        }
    }

    state->stats.commands += result;

    return result;
}

/** Write a command that sets a parameter of a bus.
 *
 * @param state The state
 * @param bus Index of the bus
 * @param param The parameter
 * @param value The value
 */
static void box_audio_bus_set(
    box_audio_module_t *state,
    uint32_t bus,
    box_audio_param_t param,
    float value)
{
    if (box_audio_reserve(state)) {
        box_audio_write_bus_set(&state->writer, bus, param, value);
    }
}

/** Write a command that sets a parameter of a voice.
 *
 * @param state The state
 * @param voice Handle of the voice
 * @param param The parameter
 * @param value The value
 */
static void box_audio_voice_set(
    box_audio_module_t *state,
    uint32_t voice,
    box_audio_param_t param,
    float value)
{
    if (box_audio_reserve(state)) {
        box_audio_write_voice_set(&state->writer, voice, param, value);
    }
}

/** Write a stop command.
 *
 * @param state The state
 * @param voice Handle of the voice
 */
static void box_audio_stop(
    box_audio_module_t *state,
    uint32_t voice)
{
    if (box_audio_reserve(state)) {
        box_audio_write_stop(&state->writer, voice);
    }
}

/** Pick the handle of a new voice.
 *
 * @param state The state
 * @return The handle, never 0
 */
static uint32_t box_audio_new_voice(
    box_audio_module_t *state)
{
    uint32_t result = state->next_voice ++;
    if (!state->next_voice) {
        state->next_voice = 1;
    }

    return result;
}

/** Forget what was sent to an output that is gone: the next frame sends everything again.
 *
 * @param state The state
 * @param serial Serial of the new output, 0 for none
 */
static void box_audio_reset(
    box_audio_module_t *state,
    uint32_t serial)
{
    state->serial = serial;
    ecs_os_memset_n(state->sound_used, 0, uint8_t, BOX_AUDIO_SOUND_MAX);
    ecs_map_clear(&state->voices);
    ecs_vec_clear(&state->oneshots);
    ecs_vec_clear(&state->stops);
    ecs_vec_clear(&state->releases);
    ecs_vec_clear(&state->fades);
    state->settings_sent = false;
    state->order_dirty = true;
    state->voices_stolen = 0;
    state->spatial_dirty = true;
    if (state->capture.path && state->capture.serial) {
        ecs_err("box.audio: the output changed, the capture of '%s' is dropped",
            state->capture.path);
        ecs_os_free(state->capture.path);
        state->capture.path = NULL;
    }
}

/** Get the bus index of a bus entity.
 *
 * @param world The world
 * @param state The state
 * @param bus The entity
 * @return The index, 0 when the entity is not a live bus with an index
 */
static uint8_t box_audio_bus_of(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t bus)
{
    const AudioBusState *s;
    uint8_t result = 0;
    if (!bus || !ecs_is_alive(world, bus) || !ecs_has(world, bus, AudioBus)) {
        goto done;
    }

    s = ecs_get(world, bus, AudioBusState);
    if (s && s->index && state->bus_entity[s->index] == bus) {
        result = s->index;
    }

done:
    return result;
}

/** Take the events of the output: ended voices, steals and freed sounds.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 */
static void box_audio_events(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    box_audio_event_t events[64];
    int32_t count, i, k;
    do {
        count = t->read_events(t, events, 64);
        for (i = 0; i < count; i ++) {
            const box_audio_event_t *e = &events[i];
            if (e->type == BOX_AUDIO_EVENT_SOUND_FREE && e->id < BOX_AUDIO_SOUND_MAX) {
                state->sound_used[e->id] = 0;
            } else if (e->type == BOX_AUDIO_EVENT_VOICE_STOLEN) {
                state->voices_stolen = e->value;
            } else if (e->type == BOX_AUDIO_EVENT_VOICE_END) {
                ecs_map_val_t *v = ecs_map_get(&state->voices, e->id);
                ecs_entity_t src = v ? (ecs_entity_t)v[0] : 0;
                box_audio_oneshot_t *shots = ecs_vec_first(&state->oneshots);
                for (k = 0; k < ecs_vec_count(&state->oneshots); k ++) {
                    if (shots[k].handle == e->id) {
                        ecs_vec_remove_t(&state->oneshots, box_audio_oneshot_t, k);
                        break;
                    }
                }

                if (v) {
                    ecs_map_remove(&state->voices, e->id);
                    state->spatial_dirty = true;
                }

                AudioVoice *voice = src && ecs_is_alive(world, src) ?
                    ecs_get_mut(world, src, AudioVoice) : NULL;
                AudioSource *source = voice ? ecs_get_mut(world, src, AudioSource) : NULL;
                if (voice && source && voice->handle == e->id) {
                    voice->handle = 0;
                    if (!voice->loop) {
                        source->playing = false;
                        ecs_modified(world, src, AudioSource);
                    }
                }
            }
        }
    } while (count == 64);
}

/** Give a bus an index in the core: the lowest one that is free and was not freed this frame.
 *
 * @param world The world
 * @param state The state
 * @param e The bus entity
 * @return The index, 0 when every index is taken
 */
static uint8_t box_audio_bus_alloc(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e)
{
    uint8_t result = 0;
    int32_t i;
    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        if (!state->bus_entity[i] && !(state->bus_freed & (1u << i))) {
            state->bus_entity[i] = e;
            result = (uint8_t)i;
            break;
        }
    }

    if (!result && !state->bus_overflow) {
        char *path = ecs_get_path(world, e);
        ecs_err("box.audio: more than %d buses, '%s' is silent", BOX_AUDIO_BUS_MAX - 1, path);
        ecs_os_free(path);
        state->bus_overflow = true;
    }

    return result;
}

/** Send the parameters of a bus that changed, all of them to a new output or a new index.
 *
 * @param state The state
 * @param bus The bus
 * @param s What was sent for it
 * @param disabled Whether the bus is Disabled
 */
static void box_audio_bus_params(
    box_audio_module_t *state,
    const AudioBus *bus,
    AudioBusState *s,
    bool disabled)
{
    bool all = s->serial != state->serial, mute = bus->mute || disabled;
    if (!state->serial) {
        goto done;
    }

    if (all || s->volume != bus->volume) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_VOLUME, bus->volume);
    }

    if (all || s->pan != bus->pan) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_PAN, bus->pan);
    }

    if (all || s->mute != mute) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_MUTE, mute ? 1.0f : 0.0f);
    }

    if (all || s->send_level != bus->send_level) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_SEND, bus->send_level);
    }

    s->serial = state->serial;
    s->volume = bus->volume;
    s->pan = bus->pan;
    s->mute = mute;
    s->send_level = bus->send_level;
done:
    return;
}

/** Give buses their indices, send their parameters and notice route changes.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_buses(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->buses);
    int32_t i;
    while (ecs_query_next(&it)) {
        const AudioBus *bus = ecs_field(&it, AudioBus, 0);
        AudioBusState *s = ecs_field(&it, AudioBusState, 1);
        bool disabled = ecs_table_has_id(world, it.table, EcsDisabled);
        for (i = 0; i < it.count; i ++) {
            if (!s[i].index || state->bus_entity[s[i].index] != it.entities[i]) {
                s[i].index = box_audio_bus_alloc(world, state, it.entities[i]);
                s[i].serial = 0;
                state->order_dirty = true;
                if (!s[i].index) {
                    continue;
                }
            }

            if (s[i].output != bus[i].output || s[i].send != bus[i].send) {
                s[i].output = bus[i].output;
                s[i].send = bus[i].send;
                state->order_dirty = true;
            }

            box_audio_bus_params(state, &bus[i], &s[i], disabled);
        }
    }

    if (state->bus_freed) {
        state->order_dirty = true;
    }
}

/** Log the cycle that a walk of the routes found, the buses on it by name.
 *
 * @param world The world
 * @param state The state
 * @param stack The buses of the walk, the cycle at its end
 * @param depth Buses on the stack
 * @param start Position on the stack where the cycle starts
 */
static void box_audio_cycle_error(
    ecs_world_t *world,
    const box_audio_module_t *state,
    const uint8_t *stack,
    int32_t depth,
    int32_t start)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i;
    for (i = start; i <= depth; i ++) {
        char *path = ecs_get_path(world, state->bus_entity[stack[i < depth ? i : start]]);
        ecs_strbuf_append(&buf, "%s%s", i > start ? " -> " : "", path);
        ecs_os_free(path);
    }

    char *text = ecs_strbuf_get(&buf);
    ecs_err("box.audio: the outputs and sends of the buses %s form a cycle, the last valid "
        "order stays", text);
    ecs_os_free(text);
}

/** Walk the routes from a bus depth first, compute its depth (the longest path to the master)
 * and find a cycle.
 *
 * @param out Output of each bus index
 * @param send Send of each bus index, BOX_AUDIO_BUS_NONE for none
 * @param color 0 unseen, 1 on the walk, 2 done
 * @param depth Receives the depth of each finished bus
 * @param stack The walk
 * @param top Buses on the walk
 * @param bus The bus
 * @param cycle Receives the position of the walk where a cycle starts, -1 for none
 * @return Whether a cycle was found
 */
static bool box_audio_walk(
    const uint8_t *out,
    const uint8_t *send,
    uint8_t *color,
    int32_t *depth,
    uint8_t *stack,
    int32_t *top,
    uint8_t bus,
    int32_t *cycle)
{
    uint8_t next[2] = { out[bus], send[bus] };
    int32_t i, k, d = 0;
    bool result = false;
    color[bus] = 1;
    stack[(*top) ++] = bus;
    for (i = 0; i < 2 && !result; i ++) {
        uint8_t n = next[i];
        if (n == 0 || n == BOX_AUDIO_BUS_NONE) {
            d = d > 1 ? d : 1;
            continue;
        }

        if (color[n] == 1) {
            for (k = 0; k < *top; k ++) {
                if (stack[k] == n) {
                    *cycle = k;
                }
            }

            result = true;
        } else {
            if (!color[n]) {
                result = box_audio_walk(out, send, color, depth, stack, top, n, cycle);
            }

            d = depth[n] + 1 > d ? depth[n] + 1 : d;
        }
    }

    if (!result) {
        depth[bus] = d;
        color[bus] = 2;
        (*top) --;
    }

    return result;
}

/** Compile the buses into a processing order: every bus before its output and its send, the
 * deepest first, the master last. A cycle logs an error and keeps the last valid order.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_compile(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    uint8_t out[BOX_AUDIO_BUS_MAX], send[BOX_AUDIO_BUS_MAX], color[BOX_AUDIO_BUS_MAX] = {0};
    uint8_t stack[BOX_AUDIO_BUS_MAX], order[BOX_AUDIO_BUS_MAX];
    int32_t depth[BOX_AUDIO_BUS_MAX] = {0}, count = 0, top = 0, cycle = -1, i, k;
    box_audio_route_t routes[BOX_AUDIO_BUS_MAX];
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        ecs_entity_t e = state->bus_entity[i];
        const AudioBus *bus = i && e ? ecs_get(world, e, AudioBus) : NULL;
        out[i] = bus ? box_audio_bus_of(world, state, bus->output) : 0;
        send[i] = bus ? box_audio_bus_of(world, state, bus->send) : 0;
        send[i] = send[i] ? send[i] : BOX_AUDIO_BUS_NONE;
    }

    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        if (state->bus_entity[i] && !color[i] &&
            box_audio_walk(out, send, color, depth, stack, &top, (uint8_t)i, &cycle))
        {
            box_audio_cycle_error(world, state, stack, top, cycle);
            state->stats.cycles ++;
            goto done;
        }
    }

    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        if (state->bus_entity[i]) {
            for (k = count; k > 0 && depth[order[k - 1]] < depth[i]; k --) {
                order[k] = order[k - 1];
            }

            order[k] = (uint8_t)i;
            count ++;
        }
    }

    for (i = 0; i < count; i ++) {
        routes[i] = (box_audio_route_t){ order[i], out[order[i]], send[order[i]], 0 };
        state->order[i] = order[i];
    }

    state->spatial_dirty = true;
    routes[count] = (box_audio_route_t){ 0, BOX_AUDIO_BUS_NONE, BOX_AUDIO_BUS_NONE, 0 };
    state->order[count] = 0;
    state->order_count = count + 1;
    if (state->serial && box_audio_reserve(state)) {
        box_audio_write_order(&state->writer, routes, count + 1);
        state->stats.orders ++;
    }

done:
    state->order_dirty = false;
}

/** Write a reverb command and remember what it sent.
 *
 * @param state The state
 * @param slot Slot of the reverb
 * @param bus Bus index, BOX_AUDIO_BUS_NONE to take the reverb off
 * @param params The parameters
 */
static void box_audio_reverb_send(
    box_audio_module_t *state,
    uint32_t slot,
    uint32_t bus,
    const box_audio_reverb_params_t *params)
{
    box_audio_reverb_sent_t *sent = &state->reverb_sent[slot];
    if (box_audio_reserve(state)) {
        box_audio_write_reverb(&state->writer, slot, bus, params);
        state->stats.reverbs ++;
    }

    sent->serial = state->serial;
    sent->bus = bus;
    sent->params = *params;
}

/** Free the reverb slots whose entity was deleted or lost its Reverb or its bus; their tails
 * fade out.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_reverbs_free(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    uint32_t i;
    for (i = 0; i < BOX_AUDIO_REVERB_MAX; i ++) {
        ecs_entity_t e = state->reverb_entity[i];
        const AudioReverbState *rs = e && ecs_is_alive(world, e) ?
            ecs_get(world, e, AudioReverbState) : NULL;
        box_audio_reverb_sent_t *sent = &state->reverb_sent[i];
        if (!e || (rs && rs->slot == i + 1 && ecs_has(world, e, Reverb) &&
            ecs_has(world, e, AudioBus)))
        {
            continue;
        }

        if (state->serial && sent->serial == state->serial && sent->bus != BOX_AUDIO_BUS_NONE) {
            box_audio_reverb_send(state, i, BOX_AUDIO_BUS_NONE, &sent->params);
        }

        state->reverb_entity[i] = 0;
    }
}

/** Give a reverb a free slot, or log once that every slot is taken.
 *
 * @param world The world
 * @param state The state
 * @param e The entity
 * @param rs What was done for it
 * @return The slot, -1 when it stays dry
 */
static int32_t box_audio_reverb_alloc(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioReverbState *rs)
{
    int32_t i, result = -1;
    for (i = 0; i < BOX_AUDIO_REVERB_MAX && result < 0; i ++) {
        if (!state->reverb_entity[i]) {
            state->reverb_entity[i] = e;
            state->reverb_sent[i].serial = 0;
            rs->slot = (uint8_t)(i + 1);
            rs->refused = false;
            result = i;
        }
    }

    if (result < 0 && !rs->refused) {
        char *path = ecs_get_path(world, e);
        ecs_err("box.audio: more than %d reverb, '%s' stays dry", BOX_AUDIO_REVERB_MAX, path);
        ecs_os_free(path);
        rs->refused = true;
        rs->slot = 0;
    }

    return result;
}

/** Give reverbs their slots and delay memory and send their parameters when they changed.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 */
static void box_audio_reverbs(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    ecs_iter_t it = ecs_query_iter(world, state->reverbs);
    int32_t i, slot;
    box_audio_reverbs_free(world, state);
    while (ecs_query_next(&it)) {
        const Reverb *reverb = ecs_field(&it, Reverb, 0);
        AudioReverbState *rs = ecs_field(&it, AudioReverbState, 1);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            uint8_t index = box_audio_bus_of(world, state, e);
            uint32_t bus = index ? index : BOX_AUDIO_BUS_NONE;
            box_audio_reverb_params_t params = {
                .decay = reverb[i].decay, .damping = reverb[i].damping,
                .predelay = reverb[i].predelay, .size = reverb[i].size, .mix = reverb[i].mix };
            box_audio_reverb_sent_t *sent;
            slot = rs[i].slot && state->reverb_entity[rs[i].slot - 1] == e ? rs[i].slot - 1 :
                box_audio_reverb_alloc(world, state, e, &rs[i]);
            if (slot < 0) {
                continue;
            }

            sent = &state->reverb_sent[slot];
            if (sent->memory != state->serial && t->reverb(t, (uint32_t)slot)) {
                sent->memory = state->serial;
            }

            if (sent->serial != state->serial || sent->bus != bus ||
                ecs_os_memcmp_t(&sent->params, &params, box_audio_reverb_params_t))
            {
                box_audio_reverb_send(state, (uint32_t)slot, bus, &params);
            }
        }
    }
}

/** Pick a free sound id of the core, or log once that every id is taken.
 *
 * @param state The state
 * @return The id, 0 when every id is taken
 */
static uint32_t box_audio_sound_id(
    box_audio_module_t *state)
{
    uint32_t id;
    for (id = 1; id < BOX_AUDIO_SOUND_MAX && state->sound_used[id]; id ++) { }
    if (id == BOX_AUDIO_SOUND_MAX) {
        if (!state->sound_overflow) {
            ecs_err("box.audio: more than %d sounds", BOX_AUDIO_SOUND_MAX - 1);
            state->sound_overflow = true;
        }

        id = 0;
    }

    return id;
}

/** Give a sound an id of the core and hand its frames to the output, which copies them.
 *
 * @param state The state
 * @param t The transport
 * @param data Frames, interleaved when stereo
 * @param frames Number of frames
 * @param channels 1 or 2
 * @param sample_rate Frames per second, 0 for the rate of the output
 * @return The id, 0 when the output did not take the sound
 */
static uint32_t box_audio_sound_send(
    box_audio_module_t *state,
    box_audio_transport_t *t,
    const float *data,
    int32_t frames,
    int32_t channels,
    float sample_rate)
{
    uint32_t id = box_audio_sound_id(state), result = 0;
    if (id && t->sound(t, id, data, frames, channels, sample_rate)) {
        state->sound_used[id] = 1;
        result = id;
    }

    return result;
}

/** Compile the synth sounds that changed and send their patches: a new one gets an id of the
 * core, a changed one replaces the patch of its id, which the next play takes.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_synths(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->synths);
    int32_t i;
    while (ecs_query_next(&it)) {
        AudioSynth *synth = ecs_field(&it, AudioSynth, 1);
        AudioSoundId *s = ecs_field(&it, AudioSoundId, 2);
        for (i = 0; i < it.count; i ++) {
            if (!synth[i].compiled &&
                box_audio_synth_compile(world, state, it.entities[i], &synth[i]))
            {
                synth[i].pending = true;
            }

            if (s[i].id && s[i].serial == state->serial && !synth[i].pending) {
                continue;
            }

            if (!s[i].id || s[i].serial != state->serial) {
                s[i].id = box_audio_sound_id(state);
                s[i].serial = state->serial;
                if (!s[i].id) {
                    continue;
                }

                state->sound_used[s[i].id] = 1;
            }

            if (box_audio_reserve(state)) {
                box_audio_write_synth(&state->writer, s[i].id, &synth[i].patch);
            }

            synth[i].pending = false;
        }
    }
}

/** Send the sounds the output does not have yet: AudioPcm, decoded files and synth patches.
 *
 * @param world The world
 * @param state The state
 * @param t The transport
 */
static void box_audio_sounds(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    ecs_iter_t it = ecs_query_iter(world, state->sounds);
    box_audio_sample_t **samples = ecs_vec_first(&state->samples);
    int32_t i;
    while (ecs_query_next(&it)) {
        const AudioPcm *pcm = ecs_field(&it, AudioPcm, 0);
        AudioSoundId *s = ecs_field(&it, AudioSoundId, 1);
        for (i = 0; i < it.count; i ++) {
            int32_t channels = pcm[i].channels ? pcm[i].channels : 1;
            int32_t frames = ecs_vec_count(&pcm[i].samples) / (channels > 0 ? channels : 1);
            if ((s[i].id && s[i].serial == state->serial) || frames <= 0 ||
                (channels != 1 && channels != 2))
            {
                continue;
            }

            s[i].id = box_audio_sound_send(state, t, ecs_vec_first_t(&pcm[i].samples, float),
                frames, channels, pcm[i].sample_rate);
            s[i].serial = state->serial;
            state->spatial_dirty |= s[i].id != 0;
        }
    }

    for (i = 0; i < ecs_vec_count(&state->samples); i ++) {
        box_audio_sample_t *sample = samples[i];
        if (sample->state != SoundReady || (sample->id && sample->serial == state->serial)) {
            continue;
        }

        sample->id = box_audio_sound_send(state, t, sample->data, sample->frames,
            sample->channels, sample->sample_rate);
        sample->serial = state->serial;
        state->spatial_dirty |= sample->id != 0;
    }

    box_audio_synths(world, state);
}

/** Get the sound id of a sound entity in the current output.
 *
 * @param world The world
 * @param state The state
 * @param sound The sound entity
 * @return The id, 0 when the output does not have the sound
 */
static uint32_t box_audio_sound_of(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t sound)
{
    const box_audio_sample_t *sample = box_audio_sample_of(world, sound);
    const AudioSoundId *s = !sample && sound && ecs_is_alive(world, sound) ?
        ecs_get(world, sound, AudioSoundId) : NULL;
    uint32_t result = 0;
    if (sample) {
        result = sample->serial == state->serial ? sample->id : 0;
    } else if (s) {
        result = s->serial == state->serial ? s->id : 0;
    }

    return result;
}

/** Check whether the SoundFile of a sound still decodes.
 *
 * @param world The world
 * @param sound The sound entity
 * @return Whether its sample is loading
 */
static bool box_audio_sound_loading(
    ecs_world_t *world,
    ecs_entity_t sound)
{
    const box_audio_sample_t *sample = box_audio_sample_of(world, sound);

    return sample && sample->state == SoundLoading;
}

/** Get the bus a sound plays on by default: the bus of its AudioOutput.
 *
 * @param world The world
 * @param state The state
 * @param sound The sound entity
 * @return The bus index, 0 for the master
 */
static uint8_t box_audio_sound_bus(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t sound)
{
    const AudioOutput *out = ecs_get(world, sound, AudioOutput);

    return out ? box_audio_bus_of(world, state, out->bus) : 0;
}

/** The pitch ratio the core plays at.
 *
 * @param pitch The pitch of a source
 * @return The pitch, clamped as the core clamps it
 */
static double box_audio_pitch_of(
    float pitch)
{
    return pitch > BOX_AUDIO_PITCH_MIN ?
        (pitch < BOX_AUDIO_PITCH_MAX ? pitch : BOX_AUDIO_PITCH_MAX) : BOX_AUDIO_PITCH_MIN;
}

/** Get the position of a parked voice in frames of its sound: where it would play now had it
 * kept its voice.
 *
 * @param world The world
 * @param state The state
 * @param sound The sound entity
 * @param v What was sent for the source
 * @return The position, wrapped into the sound
 */
static float box_audio_resume_position(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t sound,
    const AudioVoice *v)
{
    const box_audio_sample_t *sample = box_audio_sample_of(world, sound);
    const AudioPcm *pcm = sample ? NULL : ecs_get(world, sound, AudioPcm);
    int32_t channels = pcm && pcm->channels ? pcm->channels : 1;
    double frames = sample ? (double)sample->frames :
        pcm ? (double)(ecs_vec_count(&pcm->samples) / channels) : 0;
    double rate = sample && sample->sample_rate > 0 ? sample->sample_rate :
        pcm && pcm->sample_rate > 0 ? pcm->sample_rate : state->rate;
    double result = (state->time - v->origin) * box_audio_pitch_of(v->pitch) * rate;
    if (frames > 0 && result > 0) {
        result = fmod(result, frames);
    } else {
        result = 0;
    }

    return (float)result;
}

/** Start, stop and update the voice of a source.
 *
 * A new pitch moves the origin so the source keeps its place in time. A looping spatial source
 * out of range gives its voice back while the time of its sound runs on, and starts at that
 * place when it comes back; one that does not loop does not start out of range.
 *
 * @param world The world
 * @param state The state
 * @param e The source entity
 * @param src The source
 * @param out Its AudioOutput, NULL for none
 * @param v What was sent for it
 * @param muted Whether the source is Disabled
 * @param waiting Whether the page waits for a gesture
 * @param place Gain and pan from the listener of a spatial source, NULL for others
 */
static void box_audio_source(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioSource *src,
    const AudioOutput *out,
    AudioVoice *v,
    bool muted,
    bool waiting,
    const box_audio_place_t *place)
{
    uint32_t sound = src->playing ? box_audio_sound_of(world, state, src->sound) : 0;
    float volume = muted ? 0.0f : src->volume * (place ? place->gain : 1.0f);
    float pan = place ? place->pan : 0.0f;
    bool away = place && !(place->gain > 0), resume;
    uint8_t bus;
    if (v->serial != state->serial) {
        *v = (AudioVoice){ .serial = state->serial };
    }

    if ((v->handle || v->parked) && (!sound || v->sound != sound || v->loop != src->loop)) {
        if (v->handle) {
            box_audio_stop(state, v->handle);
            ecs_map_remove(&state->voices, v->handle);
            v->handle = 0;
        }

        v->parked = false;
    }

    if (!sound && src->playing && !src->loop && box_audio_sound_loading(world, src->sound)) {
        src->playing = false;
        state->stats.not_ready ++;
    }

    if (!sound) {
        goto done;
    }

    if (v->pitch != src->pitch && (v->handle || v->parked)) {
        double played = (state->time - v->origin) * box_audio_pitch_of(v->pitch);
        v->origin = state->time - played / box_audio_pitch_of(src->pitch);
        if (v->handle) {
            box_audio_voice_set(state, v->handle, BOX_AUDIO_PARAM_PITCH, src->pitch);
        }

        v->pitch = src->pitch;
    }

    if (away && src->loop) {
        if (v->handle) {
            box_audio_stop(state, v->handle);
            ecs_map_remove(&state->voices, v->handle);
            v->handle = 0;
            v->parked = true;
        } else if (!v->parked) {
            v->parked = true;
            v->origin = state->time;
            v->sound = sound;
            v->loop = true;
            v->pitch = src->pitch;
        }

        goto done;
    }

    if (away && !v->handle) {
        src->playing = false;
        state->stats.out_of_range ++;
        goto done;
    }

    bus = out ? box_audio_bus_of(world, state, out->bus) :
        box_audio_sound_bus(world, state, src->sound);
    if (!v->handle) {
        if (waiting && !src->loop) {
            src->playing = false;
            state->stats.dropped ++;
            goto done;
        }

        resume = v->parked;
        v->handle = box_audio_new_voice(state);
        v->sound = sound;
        v->loop = src->loop;
        v->volume = volume;
        v->pan = pan;
        v->pitch = src->pitch;
        v->bus = bus;
        v->parked = false;
        if (!resume) {
            v->origin = state->time;
        }

        ecs_map_insert(&state->voices, v->handle, e);
        if (box_audio_reserve(state)) {
            box_audio_write_play(&state->writer, v->handle, sound, bus, volume, pan,
                src->pitch, src->loop ? BOX_AUDIO_PLAY_LOOP : 0);
        }

        if (resume) {
            box_audio_voice_set(state, v->handle, BOX_AUDIO_PARAM_POSITION,
                box_audio_resume_position(world, state, src->sound, v));
        }

        goto done;
    }

    if (v->volume != volume) {
        box_audio_voice_set(state, v->handle, BOX_AUDIO_PARAM_VOLUME, volume);
        v->volume = volume;
    }

    if (v->pan != pan) {
        box_audio_voice_set(state, v->handle, BOX_AUDIO_PARAM_PAN, pan);
        v->pan = pan;
    }

    if (v->pitch != src->pitch) {
        box_audio_voice_set(state, v->handle, BOX_AUDIO_PARAM_PITCH, src->pitch);
        v->pitch = src->pitch;
    }

    if (v->bus != bus) {
        box_audio_voice_set(state, v->handle, BOX_AUDIO_PARAM_BUS, (float)bus);
        v->bus = bus;
    }

done:
    return;
}

/** Update the voices of every source.
 *
 * @param world The world
 * @param state The state
 * @param waiting Whether the page waits for a gesture
 */
static void box_audio_sources(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting)
{
    ecs_iter_t it = ecs_query_iter(world, state->sources);
    int32_t i;
    while (ecs_query_next(&it)) {
        AudioSource *src = ecs_field(&it, AudioSource, 0);
        AudioVoice *v = ecs_field(&it, AudioVoice, 1);
        const AudioOutput *out = ecs_field(&it, AudioOutput, 2);
        bool muted = ecs_table_has_id(world, it.table, EcsDisabled);
        for (i = 0; i < it.count; i ++) {
            box_audio_source(world, state, it.entities[i], &src[i], out ? &out[i] : NULL,
                &v[i], muted, waiting, NULL);
        }
    }
}

/** Compute where a spatial source is heard from the listener.
 *
 * @param state The state
 * @param spatial Its distances
 * @param wt Its world transform, NULL for none
 * @return The gain and the pan, unity and centered without a listener or a transform
 */
static box_audio_place_t box_audio_place(
    box_audio_module_t *state,
    const AudioSpatial *spatial,
    const WorldTransform *wt)
{
    box_audio_place_t result = { .gain = 1.0f };
    state->stats.spatial_updates ++;
    if (wt && state->listener.entity) {
        result.gain = box_audio_spatial_gain(spatial, (vec4*)state->listener.m, wt->m[3],
            &result.pan);
    }

    return result;
}

/** Update the voices of the spatial sources whose table changed, every one when the listener
 * moved or something they depend on changed, and the ones whose static transform moved.
 *
 * @param world The world
 * @param state The state
 * @param waiting Whether the page waits for a gesture
 */
static void box_audio_spatial_sources(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting)
{
    ecs_iter_t it = ecs_query_iter(world, state->spatial);
    ecs_entity_t *moved = ecs_vec_first(&state->moved);
    int32_t i;
    while (ecs_query_next(&it)) {
        if (!state->spatial_dirty && !ecs_iter_changed(&it)) {
            ecs_iter_skip(&it);
            continue;
        }

        AudioSource *src = ecs_field(&it, AudioSource, 0);
        AudioVoice *v = ecs_field(&it, AudioVoice, 1);
        const AudioSpatial *spatial = ecs_field(&it, AudioSpatial, 2);
        const WorldTransform *wt = ecs_field(&it, WorldTransform, 3);
        const AudioOutput *out = ecs_field(&it, AudioOutput, 4);
        bool muted = ecs_table_has_id(world, it.table, EcsDisabled);
        for (i = 0; i < it.count; i ++) {
            box_audio_place_t place = box_audio_place(state, &spatial[i], wt ? &wt[i] : NULL);
            box_audio_source(world, state, it.entities[i], &src[i], out ? &out[i] : NULL,
                &v[i], muted, waiting, &place);
        }
    }

    for (i = 0; !state->spatial_dirty && i < ecs_vec_count(&state->moved); i ++) {
        ecs_entity_t e = moved[i];
        if (!ecs_is_alive(world, e) || ecs_has_id(world, e, EcsPrefab)) {
            continue;
        }

        AudioSource *src = ecs_get_mut(world, e, AudioSource);
        AudioVoice *v = ecs_get_mut(world, e, AudioVoice);
        const AudioSpatial *spatial = ecs_get(world, e, AudioSpatial);
        if (!src || !v || !spatial) {
            continue;
        }

        bool playing = src->playing;
        box_audio_place_t place = box_audio_place(state, spatial,
            ecs_get(world, e, WorldTransform));
        box_audio_source(world, state, e, src, ecs_get(world, e, AudioOutput), v,
            ecs_has_id(world, e, EcsDisabled), waiting, &place);
        if (src->playing != playing) {
            ecs_modified(world, e, AudioSource);
        }
    }

    ecs_vec_clear(&state->moved);
}

/** Follow the listener with the volume and pan of the one shots of box_audio_play_at.
 *
 * @param state The state
 */
static void box_audio_spatial_oneshots(
    box_audio_module_t *state)
{
    box_audio_oneshot_t *shots = ecs_vec_first(&state->oneshots);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->oneshots); i ++) {
        box_audio_oneshot_t *shot = &shots[i];
        float pan = 0, gain = 1.0f;
        if (!shot->spatial) {
            continue;
        }

        state->stats.spatial_updates ++;
        if (state->listener.entity) {
            gain = box_audio_spatial_gain(&shot->distances, (vec4*)state->listener.m,
                shot->position, &pan);
        }

        if (shot->sent_volume != shot->volume * gain) {
            shot->sent_volume = shot->volume * gain;
            box_audio_voice_set(state, shot->handle, BOX_AUDIO_PARAM_VOLUME, shot->sent_volume);
        }

        if (shot->sent_pan != pan) {
            shot->sent_pan = pan;
            box_audio_voice_set(state, shot->handle, BOX_AUDIO_PARAM_PAN, pan);
        }
    }
}

/** Send the plays of box_audio_play, the stops of deleted sources and the releases of
 * sounds, and reroute the one shots of deleted buses to the master.
 *
 * @param world The world
 * @param state The state
 * @param waiting Whether the page waits for a gesture
 */
static void box_audio_queued(
    ecs_world_t *world,
    box_audio_module_t *state,
    bool waiting)
{
    box_audio_play_t *plays = ecs_vec_first(&state->plays);
    box_audio_oneshot_t *shots = ecs_vec_first(&state->oneshots);
    uint32_t *stops = ecs_vec_first(&state->stops);
    uint32_t *releases = ecs_vec_first(&state->releases);
    uint32_t *fades = ecs_vec_first(&state->fades);
    int32_t i, k;
    for (i = 0; state->bus_freed && i < ecs_vec_count(&state->oneshots); i ++) {
        if (shots[i].bus && (state->bus_freed & (1u << shots[i].bus))) {
            box_audio_voice_set(state, shots[i].handle, BOX_AUDIO_PARAM_BUS, 0.0f);
            shots[i].bus = 0;
        }
    }

    for (i = 0; i < ecs_vec_count(&state->plays); i ++) {
        uint32_t sound = box_audio_sound_of(world, state, plays[i].sound);
        const AudioSpatial *spatial = NULL;
        float gain = 1.0f, pan = 0;
        if (!sound) {
            state->stats.not_ready += box_audio_sound_loading(world, plays[i].sound);
            continue;
        }

        if (waiting) {
            state->stats.dropped ++;
            continue;
        }

        if (plays[i].spatial) {
            spatial = ecs_get(world, plays[i].sound, AudioSpatial);
            state->stats.spatial_updates ++;
            if (state->listener.entity) {
                gain = box_audio_spatial_gain(spatial, (vec4*)state->listener.m,
                    plays[i].position, &pan);
            }

            if (!(gain > 0)) {
                state->stats.out_of_range ++;
                continue;
            }
        }

        if (ecs_vec_count(&state->oneshots) >= BOX_AUDIO_ONESHOT_MAX) {
            ecs_vec_remove_ordered_t(&state->oneshots, box_audio_oneshot_t, 0);
        }

        box_audio_oneshot_t *shot = ecs_vec_append_t(NULL, &state->oneshots,
            box_audio_oneshot_t);
        *shot = (box_audio_oneshot_t){
            .handle = box_audio_new_voice(state),
            .sound = sound,
            .bus = box_audio_sound_bus(world, state, plays[i].sound),
            .spatial = plays[i].spatial,
            .position = { plays[i].position[0], plays[i].position[1], plays[i].position[2] },
            .distances = spatial ? *spatial :
                (AudioSpatial){ .min_distance = 1.0f, .max_distance = 50.0f, .rolloff = 1.0f },
            .volume = plays[i].volume,
            .sent_volume = plays[i].volume * gain,
            .sent_pan = pan
        };

        if (box_audio_reserve(state)) {
            box_audio_write_play(&state->writer, shot->handle, sound, shot->bus,
                shot->sent_volume, pan, 1.0f, 0);
        }
    }

    for (i = 0; i < ecs_vec_count(&state->stops); i ++) {
        box_audio_stop(state, stops[i]);
    }

    for (k = 0; k < ecs_vec_count(&state->fades); k ++) {
        shots = ecs_vec_first(&state->oneshots);
        for (i = ecs_vec_count(&state->oneshots) - 1; i >= 0; i --) {
            if (shots[i].sound == fades[k]) {
                box_audio_stop(state, shots[i].handle);
                ecs_vec_remove_ordered_t(&state->oneshots, box_audio_oneshot_t, i);
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&state->releases); i ++) {
        if (box_audio_reserve(state)) {
            box_audio_write_release(&state->writer, releases[i]);
        }
    }

    ecs_vec_clear(&state->plays);
    ecs_vec_clear(&state->stops);
    ecs_vec_clear(&state->releases);
    ecs_vec_clear(&state->fades);
}

/** Send the master volume and mute of AudioSettings when they changed.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_settings(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    const AudioSettings *s = ecs_singleton_get(world, AudioSettings);
    AudioSettings settings = s ? *s : (AudioSettings){ .master_volume = 1.0f };
    if (!state->settings_sent || settings.master_volume != state->settings.master_volume) {
        box_audio_bus_set(state, 0, BOX_AUDIO_PARAM_VOLUME, settings.master_volume);
    }

    if (!state->settings_sent || settings.mute != state->settings.mute) {
        box_audio_bus_set(state, 0, BOX_AUDIO_PARAM_MUTE, settings.mute ? 1.0f : 0.0f);
    }

    state->settings = settings;
    state->settings_sent = true;
}

/** Map the state of a transport to the state of AudioStatus.
 *
 * @param state The state of the transport
 * @return The AudioState
 */
static AudioState box_audio_state_of(
    box_audio_state_t state)
{
    AudioState result = AudioOff;
    switch (state) {
    case BOX_AUDIO_STATE_OFF: result = AudioOff; break;
    case BOX_AUDIO_STATE_NO_DEVICE: result = AudioNoDevice; break;
    case BOX_AUDIO_STATE_STARTING: result = AudioStarting; break;
    case BOX_AUDIO_STATE_RUNNING: result = AudioRunning; break;
    case BOX_AUDIO_STATE_LOST: result = AudioNoDevice; break;
    case BOX_AUDIO_STATE_FAILED: result = AudioFailed; break;
    case BOX_AUDIO_STATE_WAITING: result = AudioWaitingForGesture; break;
    case BOX_AUDIO_STATE_SUSPENDED: result = AudioSuspended; break;
    }

    return result;
}

/** Write AudioStatus when it changed.
 *
 * @param world The world
 * @param state The state
 * @param t The transport, NULL for none
 */
static void box_audio_status(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    box_audio_transport_stats_t stats = {0};
    AudioStatus status = {0};
    const AudioStatus *prev = ecs_singleton_get(world, AudioStatus);
    if (t) {
        t->stats(t, &stats);
        status.state = box_audio_state_of(t->state(t));
        status.sample_rate = (int32_t)t->sample_rate;
        status.voices = stats.voices;
        status.voices_stolen = state->voices_stolen;
        status.load = stats.load;
    }

    status.buses = t ? state->order_count : 0;
    if (!prev || ecs_os_memcmp_t(prev, &status, AudioStatus)) {
        ecs_singleton_set_ptr(world, AudioStatus, &status);
    }
}

/** Start the capture that waits and write the file of a capture that is complete.
 *
 * @param state The state
 * @param t The transport, NULL for none
 */
static void box_audio_capture_update(
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    box_audio_capture_job_t *job = &state->capture;
    int32_t frames = 0;
    float *data;
    if (!job->path) {
        goto done;
    }

    if (!t) {
        ecs_err("box.audio: the world has no audio output, the capture of '%s' is dropped",
            job->path);
        goto drop;
    }

    if (!job->serial) {
        if (t->sample_rate <= 0) {
            goto done;
        }

        frames = (int32_t)ceilf(job->seconds * t->sample_rate);
        if (!t->capture(t, frames)) {
            ecs_err("box.audio: the output cannot record the capture of '%s'", job->path);
            goto drop;
        }

        job->serial = state->serial;
        goto done;
    }

    data = t->capture_take(t, &frames);
    if (data) {
        box_audio_wav_write(job->path, data, frames, (int32_t)t->sample_rate);
        ecs_os_free(data);
        goto drop;
    }

    goto done;
drop:
    ecs_os_free(job->path);
    job->path = NULL;
    job->serial = 0;
done:
    return;
}

/** The sync system: once per frame, at the end of the frame, one batch of what changed.
 *
 * @param it The iterator of the system
 */
static void BoxAudioSync(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_audio_module_t *state = it->ctx;
    box_audio_transport_t *t = box_audio_output_transport(world);
    uint32_t serial = t ? box_audio_output_serial(world) : 0;
    box_audio_listener_t listener;
    bool waiting, listener_moved = false;
    if (serial != state->serial) {
        box_audio_reset(state, serial);
    }

    if (t) {
        box_audio_events(world, state, t);
    }

    waiting = t && t->state(t) == BOX_AUDIO_STATE_WAITING;
    if (waiting != state->waiting) {
        state->waiting = waiting;
        state->spatial_dirty = true;
    }

    box_audio_listener_find(world, state, &listener);
    if (listener.entity != state->listener.entity ||
        ecs_os_memcmp(listener.m, state->listener.m, ECS_SIZEOF(mat4)))
    {
        state->listener = listener;
        state->spatial_dirty = true;
        listener_moved = true;
    }

    box_audio_buses(world, state);
    if (state->order_dirty) {
        box_audio_compile(world, state);
    }

    if (t) {
        box_audio_settings(world, state);
        box_audio_reverbs(world, state, t);
        box_audio_sounds(world, state, t);
        state->rate = t->sample_rate;
        box_audio_sources(world, state, waiting);
        box_audio_spatial_sources(world, state, waiting);
        if (listener_moved) {
            box_audio_spatial_oneshots(state);
        }

        box_audio_queued(world, state, waiting);
        state->spatial_dirty = false;
    } else {
        ecs_vec_clear(&state->moved);
        ecs_vec_clear(&state->plays);
        ecs_vec_clear(&state->stops);
        ecs_vec_clear(&state->releases);
        ecs_vec_clear(&state->fades);
    }

    if (t && state->writer.size) {
        t->commands(t, state->writer.data, state->writer.size);
        state->stats.batches ++;
        state->stats.bytes += state->writer.size;
    }

    state->writer.size = 0;
    state->bus_freed = 0;
    box_audio_status(world, state, t);
    box_audio_capture_update(state, t);
    state->time += (double)it->delta_time;
}

/** Free the bus index of a deleted bus; its users go to the master.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioBusRemove(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        const AudioBusState *s = ecs_get(it->world, it->entities[i], AudioBusState);
        if (s && s->index && state->bus_entity[s->index] == it->entities[i]) {
            state->bus_entity[s->index] = 0;
            state->bus_freed |= 1u << s->index;
            state->order_dirty = true;
        }
    }

done:
    return;
}

/** Stop the voice of a deleted source, with the fade of a stop.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioSourceRemove(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        const AudioVoice *v = ecs_get(it->world, it->entities[i], AudioVoice);
        if (v && v->handle && v->serial == state->serial && state->serial) {
            ecs_vec_append_t(NULL, &state->stops, uint32_t)[0] = v->handle;
            ecs_map_remove(&state->voices, v->handle);
        }
    }

done:
    return;
}

/** Release the sound of changed or removed samples, and of removed synth sounds. Voices that
 * play changed samples finish first, one shots of removed ones fade out as sources of a
 * deleted sound do.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioPcmChange(
    ecs_iter_t *it)
{
    box_audio_module_t *state = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        AudioSoundId *s = ecs_get_mut(it->world, it->entities[i], AudioSoundId);
        if (s && s->id && s->serial == state->serial && state->serial) {
            ecs_vec_append_t(NULL, &state->releases, uint32_t)[0] = s->id;
            if (it->event == EcsOnRemove) {
                ecs_vec_append_t(NULL, &state->fades, uint32_t)[0] = s->id;
            }
        }

        if (s) {
            s->id = 0;
        }
    }

    state->spatial_dirty = true;

done:
    return;
}

/** Remember the spatial sources whose static transform moved, a box_transform_moved_t.
 *
 * @param world The world
 * @param entities The entities
 * @param count Number of entities
 * @param ctx The state
 */
static void box_audio_on_moved(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx)
{
    box_audio_module_t *state = ctx;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (ecs_has(world, entities[i], AudioSpatial) &&
            ecs_has(world, entities[i], AudioSource))
        {
            ecs_vec_append_t(NULL, &state->moved, ecs_entity_t)[0] = entities[i];
        }
    }
}

void box_audio_sync_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    state->buses = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioBuses" }),
        .terms = {
            { .id = ecs_id(AudioBus), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioBusState), .src.id = EcsSelf }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    state->sources = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioSources" }),
        .terms = {
            { .id = ecs_id(AudioSource), .src.id = EcsSelf },
            { .id = ecs_id(AudioVoice), .src.id = EcsSelf },
            { .id = ecs_id(AudioOutput), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(AudioSpatial), .src.id = EcsSelf, .oper = EcsNot }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    state->spatial = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioSpatialSources" }),
        .terms = {
            { .id = ecs_id(AudioSource), .src.id = EcsSelf },
            { .id = ecs_id(AudioVoice), .src.id = EcsSelf },
            { .id = ecs_id(AudioSpatial), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(AudioOutput), .inout = EcsIn, .oper = EcsOptional }
        },
        .flags = EcsQueryMatchDisabled | EcsQueryDetectChanges,
        .cache_kind = EcsQueryCacheAuto
    });

    state->listeners = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioListeners" }),
        .terms = {
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .src.id = EcsSelf },
            { .id = AudioListener, .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    state->views = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioViews" }),
        .terms = {{ .id = ecs_id(View), .inout = EcsIn, .src.id = EcsSelf }},
        .cache_kind = EcsQueryCacheAuto
    });

    box_transform_on_moved(world, box_audio_on_moved, state, NULL);

    state->sounds = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioSounds" }),
        .terms = {
            { .id = ecs_id(AudioPcm), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioSoundId), .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    state->synths = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioSynths" }),
        .terms = {
            { .id = ecs_id(SynthSound), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioSynth), .src.id = EcsSelf },
            { .id = ecs_id(AudioSoundId), .src.id = EcsSelf }
        },
        .flags = EcsQueryMatchPrefab,
        .cache_kind = EcsQueryCacheAuto
    });

    state->reverbs = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioReverbs" }),
        .terms = {
            { .id = ecs_id(Reverb), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioReverbState), .src.id = EcsSelf },
            { .id = ecs_id(AudioBus), .inout = EcsInOutNone, .src.id = EcsSelf }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AudioSync" }),
        .phase = EcsPreStore,
        .run = BoxAudioSync,
        .ctx = state,
        .immediate = true
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioBusRemove" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(AudioBus), .src.id = EcsSelf }},
        .events = { EcsOnRemove },
        .callback = BoxAudioBusRemove,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioSourceRemove" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(AudioSource), .src.id = EcsSelf }},
        .events = { EcsOnRemove },
        .callback = BoxAudioSourceRemove,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioPcmChange" }),
        .query.terms = {{ .id = ecs_id(AudioPcm), .src.id = EcsSelf }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BoxAudioPcmChange,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AudioSynthRemove" }),
        .query.flags = EcsQueryMatchPrefab,
        .query.terms = {{ .id = ecs_id(SynthSound), .src.id = EcsSelf }},
        .events = { EcsOnRemove },
        .callback = BoxAudioPcmChange,
        .ctx = state
    });
}
