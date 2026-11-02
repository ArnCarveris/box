/**
 * @file audio/sync.c
 * @brief The system that turns the changes of the audio components into one command batch per
 * frame: bus parameters and the processing order, sounds, sources, plays, stops and releases.
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
    state->settings_sent = false;
    state->order_dirty = true;
    state->voices_stolen = 0;
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

/** Send the samples of sounds the output does not have yet.
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
    uint32_t id;
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

            s[i].id = 0;
            for (id = 1; id < BOX_AUDIO_SOUND_MAX && state->sound_used[id]; id ++) { }
            if (id == BOX_AUDIO_SOUND_MAX) {
                if (!state->sound_overflow) {
                    ecs_err("box.audio: more than %d sounds", BOX_AUDIO_SOUND_MAX - 1);
                    state->sound_overflow = true;
                }

                continue;
            }

            if (t->sound(t, id, ecs_vec_first_t(&pcm[i].samples, float), frames, channels,
                pcm[i].sample_rate))
            {
                state->sound_used[id] = 1;
                s[i].id = id;
                s[i].serial = state->serial;
            }
        }
    }
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
    const AudioSoundId *s = sound && ecs_is_alive(world, sound) ?
        ecs_get(world, sound, AudioSoundId) : NULL;

    return s && s->serial == state->serial ? s->id : 0;
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

/** Start, stop and update the voice of a source.
 *
 * @param world The world
 * @param state The state
 * @param e The source entity
 * @param src The source
 * @param out Its AudioOutput, NULL for none
 * @param v What was sent for it
 * @param muted Whether the source is Disabled
 * @param waiting Whether the page waits for a gesture
 */
static void box_audio_source(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioSource *src,
    const AudioOutput *out,
    AudioVoice *v,
    bool muted,
    bool waiting)
{
    uint32_t sound = src->playing ? box_audio_sound_of(world, state, src->sound) : 0;
    float volume = muted ? 0.0f : src->volume;
    uint8_t bus;
    if (v->serial != state->serial) {
        *v = (AudioVoice){ .serial = state->serial };
    }

    if (v->handle && (!sound || v->sound != sound || v->loop != src->loop)) {
        box_audio_stop(state, v->handle);
        ecs_map_remove(&state->voices, v->handle);
        v->handle = 0;
    }

    if (!sound) {
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

        v->handle = box_audio_new_voice(state);
        v->sound = sound;
        v->loop = src->loop;
        v->volume = volume;
        v->pitch = src->pitch;
        v->bus = bus;
        ecs_map_insert(&state->voices, v->handle, e);
        if (box_audio_reserve(state)) {
            box_audio_write_play(&state->writer, v->handle, sound, bus, volume, 0.0f,
                src->pitch, src->loop ? BOX_AUDIO_PLAY_LOOP : 0);
        }

        goto done;
    }

    if (v->volume != volume) {
        box_audio_voice_set(state, v->handle, BOX_AUDIO_PARAM_VOLUME, volume);
        v->volume = volume;
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
                &v[i], muted, waiting);
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
    int32_t i;
    for (i = 0; state->bus_freed && i < ecs_vec_count(&state->oneshots); i ++) {
        if (shots[i].bus && (state->bus_freed & (1u << shots[i].bus))) {
            box_audio_voice_set(state, shots[i].handle, BOX_AUDIO_PARAM_BUS, 0.0f);
            shots[i].bus = 0;
        }
    }

    for (i = 0; i < ecs_vec_count(&state->plays); i ++) {
        uint32_t sound = box_audio_sound_of(world, state, plays[i].sound);
        if (!sound) {
            continue;
        }

        if (waiting) {
            state->stats.dropped ++;
            continue;
        }

        if (ecs_vec_count(&state->oneshots) >= BOX_AUDIO_ONESHOT_MAX) {
            ecs_vec_remove_ordered_t(&state->oneshots, box_audio_oneshot_t, 0);
        }

        box_audio_oneshot_t *shot = ecs_vec_append_t(NULL, &state->oneshots,
            box_audio_oneshot_t);
        shot->handle = box_audio_new_voice(state);
        shot->bus = box_audio_sound_bus(world, state, plays[i].sound);
        if (box_audio_reserve(state)) {
            box_audio_write_play(&state->writer, shot->handle, sound, shot->bus,
                plays[i].volume, 0.0f, 1.0f, 0);
        }
    }

    for (i = 0; i < ecs_vec_count(&state->stops); i ++) {
        box_audio_stop(state, stops[i]);
    }

    for (i = 0; i < ecs_vec_count(&state->releases); i ++) {
        if (box_audio_reserve(state)) {
            box_audio_write_release(&state->writer, releases[i]);
        }
    }

    ecs_vec_clear(&state->plays);
    ecs_vec_clear(&state->stops);
    ecs_vec_clear(&state->releases);
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
    bool waiting;
    if (serial != state->serial) {
        box_audio_reset(state, serial);
    }

    if (t) {
        box_audio_events(world, state, t);
    }

    waiting = t && t->state(t) == BOX_AUDIO_STATE_WAITING;
    box_audio_buses(world, state);
    if (state->order_dirty) {
        box_audio_compile(world, state);
    }

    if (t) {
        box_audio_settings(world, state);
        box_audio_sounds(world, state, t);
        box_audio_sources(world, state, waiting);
        box_audio_queued(world, state, waiting);
    } else {
        ecs_vec_clear(&state->plays);
        ecs_vec_clear(&state->stops);
        ecs_vec_clear(&state->releases);
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

/** Release the sound of changed or removed samples; voices that play it finish first.
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
        }

        if (s) {
            s->id = 0;
        }
    }

done:
    return;
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
            { .id = ecs_id(AudioOutput), .inout = EcsIn, .oper = EcsOptional }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    state->sounds = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioSounds" }),
        .terms = {
            { .id = ecs_id(AudioPcm), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioSoundId), .src.id = EcsSelf }
        },
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
}
