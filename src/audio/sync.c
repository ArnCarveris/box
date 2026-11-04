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

_Static_assert(sizeof(box_audio_cmd_synth_t) <= BOX_AUDIO_COMMAND_RESERVE,
    "the reserve of a command batch holds a synth patch");

bool box_audio_reserve(
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
        box_audio_write_bus_set(&state->writer, bus, param, value, 0);
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
        box_audio_write_voice_set(&state->writer, voice, param, value, 0);
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
        box_audio_write_stop(&state->writer, voice, 0);
    }
}

uint32_t box_audio_new_voice(
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
    state->voices_refused = 0;
    box_audio_music_reset(state);
    state->spatial_dirty = true;
    ecs_os_memset_n(state->meters, 0, box_audio_meter_window_t, BOX_AUDIO_BUS_MAX);
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
uint8_t box_audio_bus_of(
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
                if (e->value > 0) {
                    state->voices_refused = e->value;
                }

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
        if (!state->bus_entity[i] && !(state->bus_freed & ((uint64_t)1 << i))) {
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

/** Get the BUS_SET parameter of the level of a send.
 *
 * @param j Index of the send
 * @return BOX_AUDIO_PARAM_SEND or BOX_AUDIO_PARAM_SEND_1..3
 */
static box_audio_param_t box_audio_send_param(
    int32_t j)
{
    return j ? (box_audio_param_t)(BOX_AUDIO_PARAM_SEND_1 + j - 1) : BOX_AUDIO_PARAM_SEND;
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
    bool all = s->serial != state->serial, mute = bus->mute || disabled ||
        !(state->audible & ((uint64_t)1 << s->index));
    const AudioSend *sends = ecs_vec_first(&bus->sends);
    int32_t j, count = ecs_vec_count(&bus->sends);
    if (!state->serial) {
        goto done;
    }

    if (all || s->volume != bus->volume) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_VOLUME, bus->volume);
    }

    if (all || s->pan != bus->pan) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_PAN, bus->pan);
    }

    if (all || s->width != bus->width) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_WIDTH, bus->width);
    }

    if (all || s->mute != mute) {
        box_audio_bus_set(state, s->index, BOX_AUDIO_PARAM_MUTE, mute ? 1.0f : 0.0f);
    }

    for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
        float level = j < count ? sends[j].level : 0.0f;
        if (all || s->send_level[j] != level) {
            box_audio_bus_set(state, s->index, box_audio_send_param(j), level);
        }

        s->send_level[j] = level;
    }

    s->serial = state->serial;
    s->volume = bus->volume;
    s->pan = bus->pan;
    s->width = bus->width;
    s->mute = mute;
done:
    return;
}

/** Note the sends of a bus: the buses of the first BOX_AUDIO_SENDS and which are pre-fader,
 * with one error when it has more.
 *
 * @param world The world
 * @param e The bus entity
 * @param bus The bus
 * @param s What was sent for it
 * @return Whether its sends changed
 */
static bool box_audio_bus_sends(
    ecs_world_t *world,
    ecs_entity_t e,
    const AudioBus *bus,
    AudioBusState *s)
{
    const AudioSend *sends = ecs_vec_first(&bus->sends);
    int32_t j, count = ecs_vec_count(&bus->sends);
    uint8_t pre = 0;
    bool changed = false;
    if (count > BOX_AUDIO_SENDS && !s->sends_refused) {
        char *path = ecs_get_path(world, e);
        ecs_err("box.audio: '%s' has %d sends, more than %d, the first %d play", path, count,
            BOX_AUDIO_SENDS, BOX_AUDIO_SENDS);
        ecs_os_free(path);
    }

    s->sends_refused = count > BOX_AUDIO_SENDS;
    for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
        ecs_entity_t to = j < count ? sends[j].bus : 0;
        if (j < count && sends[j].pre_fader) {
            pre |= (uint8_t)(1u << j);
        }

        changed |= s->sends[j] != to;
        s->sends[j] = to;
    }

    changed |= s->pre_fader != pre;
    s->pre_fader = pre;

    return changed;
}

/** Give buses their indices, notice route changes and note the solos and the solo safe buses.
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
    state->solo = 0;
    state->solo_safe = 0;
    while (ecs_query_next(&it)) {
        const AudioBus *bus = ecs_field(&it, AudioBus, 0);
        AudioBusState *s = ecs_field(&it, AudioBusState, 1);
        bool disabled = ecs_table_has_id(world, it.table, EcsDisabled);
        for (i = 0; i < it.count; i ++) {
            uint64_t bit;
            if (it.entities[i] == state->master) {
                continue;
            }

            if (!s[i].index || state->bus_entity[s[i].index] != it.entities[i]) {
                s[i].index = box_audio_bus_alloc(world, state, it.entities[i]);
                s[i].serial = 0;
                state->order_dirty = true;
                if (!s[i].index) {
                    continue;
                }
            }

            if (box_audio_bus_sends(world, it.entities[i], &bus[i], &s[i]) ||
                s[i].output != bus[i].output)
            {
                s[i].output = bus[i].output;
                state->order_dirty = true;
            }

            bit = (uint64_t)1 << s[i].index;
            if (bus[i].solo && !disabled) {
                state->solo |= bit;
            }

            if (bus[i].kind == AudioChannelReturn || bus[i].kind == AudioChannelSfx) {
                state->solo_safe |= bit;
            }
        }
    }

    if (state->bus_freed) {
        state->order_dirty = true;
    }
}

/** Compute the buses the solo keeps: without a solo all of them, else the soloed buses, the
 * buses that feed them, the returns and SFX buses, and every bus downstream of these. Walks the
 * routes of the last compile until nothing changes, so a cycle ends too.
 *
 * @param state The state
 */
static void box_audio_solo(
    box_audio_module_t *state)
{
    uint64_t targets[BOX_AUDIO_BUS_MAX] = {0}, audible = state->solo, prev;
    int32_t i, j;
    if (!state->solo) {
        state->audible = ~(uint64_t)0;
        goto done;
    }

    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        if (!state->bus_entity[i]) {
            continue;
        }

        targets[i] = (uint64_t)1 << state->route_out[i];
        for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
            uint8_t n = state->route_sends[i][j];
            if (n && n != BOX_AUDIO_BUS_NONE) {
                targets[i] |= (uint64_t)1 << n;
            }
        }
    }

    do {
        prev = audible;
        for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
            if (targets[i] & audible & ~(uint64_t)1) {
                audible |= (uint64_t)1 << i;
            }
        }
    } while (audible != prev);

    audible |= state->solo_safe;
    do {
        prev = audible;
        for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
            if (audible & ((uint64_t)1 << i)) {
                audible |= targets[i];
            }
        }
    } while (audible != prev);

    state->audible = audible | 1;
done:
    return;
}

/** Send the parameters of the buses that changed, the solo in their mute.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_buses_send(
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
            if (it.entities[i] != state->master && s[i].index &&
                state->bus_entity[s[i].index] == it.entities[i])
            {
                box_audio_bus_params(state, &bus[i], &s[i], disabled);
            }
        }
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
 * @param sends BOX_AUDIO_SENDS sends of each bus index in a row, BOX_AUDIO_BUS_NONE for none
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
    const uint8_t *sends,
    uint8_t *color,
    int32_t *depth,
    uint8_t *stack,
    int32_t *top,
    uint8_t bus,
    int32_t *cycle)
{
    uint8_t next[1 + BOX_AUDIO_SENDS] = { out[bus] };
    int32_t i, k, d = 0;
    bool result = false;
    for (i = 0; i < BOX_AUDIO_SENDS; i ++) {
        next[1 + i] = sends[bus * BOX_AUDIO_SENDS + i];
    }

    color[bus] = 1;
    stack[(*top) ++] = bus;
    for (i = 0; i < 1 + BOX_AUDIO_SENDS && !result; i ++) {
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
                result = box_audio_walk(out, sends, color, depth, stack, top, n, cycle);
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

/** Compile the buses into a processing order: every bus before its output and its sends, the
 * deepest first, the master last. A cycle logs an error and keeps the last valid order.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_compile(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    uint8_t *out = state->route_out, (*sends)[BOX_AUDIO_SENDS] = state->route_sends;
    uint8_t color[BOX_AUDIO_BUS_MAX] = {0}, pre[BOX_AUDIO_BUS_MAX] = {0};
    uint8_t stack[BOX_AUDIO_BUS_MAX], order[BOX_AUDIO_BUS_MAX];
    int32_t depth[BOX_AUDIO_BUS_MAX] = {0}, count = 0, top = 0, cycle = -1, i, j, k;
    box_audio_route_t routes[BOX_AUDIO_BUS_MAX];
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        ecs_entity_t e = state->bus_entity[i];
        const AudioBus *bus = i && e ? ecs_get(world, e, AudioBus) : NULL;
        const AudioBusState *bs = bus ? ecs_get(world, e, AudioBusState) : NULL;
        out[i] = bus ? box_audio_bus_of(world, state, bus->output) : 0;
        for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
            uint8_t n = bs ? box_audio_bus_of(world, state, bs->sends[j]) : 0;
            sends[i][j] = n ? n : BOX_AUDIO_BUS_NONE;
            if (n && (bs->pre_fader & (1u << j))) {
                pre[i] |= (uint8_t)(1u << j);
            }
        }
    }

    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        if (state->bus_entity[i] && !color[i] &&
            box_audio_walk(out, state->route_sends[0], color, depth, stack, &top, (uint8_t)i,
                &cycle))
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
        routes[i] = (box_audio_route_t){ .bus = order[i], .output = out[order[i]],
            .pre_fader = pre[order[i]] };
        for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
            routes[i].sends[j] = sends[order[i]][j];
        }

        state->order[i] = order[i];
    }

    state->spatial_dirty = true;
    routes[count] = (box_audio_route_t){ .bus = 0, .output = BOX_AUDIO_BUS_NONE };
    for (j = 0; j < BOX_AUDIO_SENDS; j ++) {
        routes[count].sends[j] = BOX_AUDIO_BUS_NONE;
    }

    state->order[count] = 0;
    state->order_count = count + 1;
    if (state->serial && box_audio_reserve(state)) {
        box_audio_write_order(&state->writer, routes, count + 1);
        state->stats.orders ++;
    }

done:
    state->order_dirty = false;
}

uint32_t box_audio_sound_id(
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
uint32_t box_audio_sound_of(
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
bool box_audio_sound_loading(
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
uint8_t box_audio_sound_bus(
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

/** Check whether a sound is a legato synth sound.
 *
 * @param world The world
 * @param sound The sound entity
 * @param channel Receives its SynthGlide.channel, may be NULL
 * @return Whether it has SynthSound and a SynthGlide with legato
 */
bool box_audio_legato_of(
    ecs_world_t *world,
    ecs_entity_t sound,
    int32_t *channel)
{
    const SynthGlide *glide = sound && ecs_is_alive(world, sound) &&
        ecs_has(world, sound, SynthSound) ? ecs_get(world, sound, SynthGlide) : NULL;
    bool result = glide && glide->legato;
    if (channel) {
        *channel = result ? glide->channel : 0;
    }

    return result;
}

/** Start, stop and update the voice of a source.
 *
 * A new pitch moves the origin so the source keeps its place in time. A looping spatial source
 * out of range gives its voice back while the time of its sound runs on, and starts at that
 * place when it comes back; one that does not loop does not start out of range.
 *
 * A new legato synth sound follows the voice that plays with a legato command, which moves the
 * voice to a new handle, so an end of the old voice that is still on its way does not end it.
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
    uint32_t handle;
    uint8_t bus;
    if (v->serial != state->serial) {
        *v = (AudioVoice){ .serial = state->serial };
    }

    if (v->handle && sound && v->sound != sound && v->loop == src->loop && !away &&
        box_audio_legato_of(world, src->sound, NULL))
    {
        bus = out ? box_audio_bus_of(world, state, out->bus) :
            box_audio_sound_bus(world, state, src->sound);
        handle = box_audio_new_voice(state);
        if (box_audio_reserve(state)) {
            box_audio_write_legato(&state->writer, handle, v->handle, sound, bus, volume, pan,
                src->pitch, src->loop ? BOX_AUDIO_PLAY_LOOP : 0);
        }

        ecs_map_remove(&state->voices, v->handle);
        ecs_map_insert(&state->voices, handle, e);
        v->handle = handle;
        v->sound = sound;
        v->volume = volume;
        v->pan = pan;
        v->pitch = src->pitch;
        v->bus = bus;
        v->origin = state->time;
        goto done;
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
                src->pitch, src->loop ? BOX_AUDIO_PLAY_LOOP : 0, 0);
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

void box_audio_play_send(
    ecs_world_t *world,
    box_audio_module_t *state,
    const box_audio_play_t *play,
    bool waiting,
    uint64_t when)
{
    box_audio_oneshot_t *shots, *shot, next;
    uint32_t sound = box_audio_sound_of(world, state, play->sound), held;
    const AudioSpatial *spatial = NULL;
    float gain = 1.0f, pan = 0;
    int32_t k, channel;
    bool legato;
    if (!sound) {
        state->stats.not_ready += box_audio_sound_loading(world, play->sound);
        goto done;
    }

    if (waiting) {
        state->stats.dropped ++;
        goto done;
    }

    if (play->spatial) {
        spatial = ecs_get(world, play->sound, AudioSpatial);
        state->stats.spatial_updates ++;
        if (state->listener.entity) {
            gain = box_audio_spatial_gain(spatial, (vec4*)state->listener.m,
                play->position, &pan);
        }

        if (!(gain > 0)) {
            state->stats.out_of_range ++;
            goto done;
        }
    }

    legato = box_audio_legato_of(world, play->sound, &channel);
    shots = ecs_vec_first(&state->oneshots);
    for (k = ecs_vec_count(&state->oneshots) - 1; legato && !when && k >= 0; k --) {
        if (shots[k].legato && shots[k].channel == channel) {
            break;
        }
    }

    next = (box_audio_oneshot_t){
        .legato = legato,
        .channel = channel,
        .handle = box_audio_new_voice(state),
        .sound = sound,
        .bus = box_audio_sound_bus(world, state, play->sound),
        .spatial = play->spatial,
        .position = { play->position[0], play->position[1], play->position[2] },
        .distances = spatial ? *spatial :
            (AudioSpatial){ .min_distance = 1.0f, .max_distance = 50.0f, .rolloff = 1.0f },
        .volume = play->volume,
        .sent_volume = play->volume * gain,
        .sent_pan = pan
    };

    if (legato && !when && k >= 0) {
        held = shots[k].handle;
        shots[k] = next;
        if (box_audio_reserve(state)) {
            box_audio_write_legato(&state->writer, next.handle, held, sound, next.bus,
                next.sent_volume, pan, 1.0f, 0);
        }

        goto done;
    }

    if (ecs_vec_count(&state->oneshots) >= BOX_AUDIO_ONESHOT_MAX) {
        ecs_vec_remove_ordered_t(&state->oneshots, box_audio_oneshot_t, 0);
    }

    shot = ecs_vec_append_t(NULL, &state->oneshots, box_audio_oneshot_t);
    *shot = next;
    if (box_audio_reserve(state)) {
        box_audio_write_play(&state->writer, shot->handle, sound, shot->bus,
            shot->sent_volume, pan, 1.0f, 0, when);
    }

done:
    return;
}

/** Send the plays of box_audio_play and box_audio_play_at, the stops of deleted sources and
 * the releases of sounds, and reroute the one shots of deleted buses to the master. The plays
 * of box_audio_play_quantized wait for box_audio_timeline_quantized.
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
        if (shots[i].bus && (state->bus_freed & ((uint64_t)1 << shots[i].bus))) {
            box_audio_voice_set(state, shots[i].handle, BOX_AUDIO_PARAM_BUS, 0.0f);
            shots[i].bus = 0;
        }
    }

    for (i = 0; i < ecs_vec_count(&state->plays); i ++) {
        if (plays[i].quantize == AudioQuantizeNone) {
            box_audio_play_send(world, state, &plays[i], waiting, 0);
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

    ecs_vec_clear(&state->stops);
    ecs_vec_clear(&state->releases);
    ecs_vec_clear(&state->fades);
}

/** Send the master volume and mute of AudioSettings, and the volume, mute, pan and width of
 * the AudioBus of the master, when they changed. The two volumes multiply and either mute
 * mutes; the output, sends, solo and kind of the master are ignored.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_settings(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    const AudioSettings *s = ecs_singleton_get(world, AudioSettings);
    const AudioBus *mb = state->master && ecs_is_alive(world, state->master) &&
        !ecs_has_id(world, state->master, EcsDisabled) ?
            ecs_get(world, state->master, AudioBus) : NULL;
    AudioSettings settings = s ? *s : (AudioSettings){ .master_volume = 1.0f };
    AudioBus bus = { .volume = mb ? mb->volume : 1.0f, .pan = mb ? mb->pan : 0.0f,
        .width = mb ? mb->width : 1.0f, .mute = mb ? mb->mute : false };
    const AudioBus *sent = &state->master_bus;
    bool all = !state->settings_sent;
    if (all || settings.master_volume != state->settings.master_volume ||
        bus.volume != sent->volume)
    {
        box_audio_bus_set(state, 0, BOX_AUDIO_PARAM_VOLUME, settings.master_volume * bus.volume);
    }

    if (all || settings.mute != state->settings.mute || bus.mute != sent->mute) {
        box_audio_bus_set(state, 0, BOX_AUDIO_PARAM_MUTE,
            settings.mute || bus.mute ? 1.0f : 0.0f);
    }

    if (all || bus.pan != sent->pan) {
        box_audio_bus_set(state, 0, BOX_AUDIO_PARAM_PAN, bus.pan);
    }

    if (all || bus.width != sent->width) {
        box_audio_bus_set(state, 0, BOX_AUDIO_PARAM_WIDTH, bus.width);
    }

    state->master_bus = bus;
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
        status.voices_refused = state->voices_refused;
        status.load = stats.load;
        status.clock = (int64_t)t->clock(t);
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

/** Keep the master out of the scripts that set its AudioBus: a script owns the entities it
 * declares and deletes them when it reloads, which would delete the master.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_master_unown(
    ecs_world_t *world,
    const box_audio_module_t *state)
{
    if (ecs_has_pair(world, state->master, ecs_id(EcsScript), EcsWildcard)) {
        ecs_remove_pair(world, state->master, ecs_id(EcsScript),
            ecs_get_target(world, state->master, ecs_id(EcsScript), 0));
    }
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

    box_audio_master_unown(world, state);
    box_audio_buses(world, state);
    if (state->order_dirty) {
        box_audio_compile(world, state);
    }

    box_audio_solo(state);
    box_audio_buses_send(world, state);

    if (t) {
        box_audio_settings(world, state);
        box_audio_effects_sync(world, state, t);
        box_audio_sounds(world, state, t);
        state->rate = t->sample_rate;
        box_audio_sources(world, state, waiting);
        box_audio_music_update(world, state, t, waiting);
        box_audio_spatial_sources(world, state, waiting);
        if (listener_moved) {
            box_audio_spatial_oneshots(state);
        }

        box_audio_queued(world, state, waiting);
        box_audio_notes_send(world, state, waiting);
        box_audio_timeline_update(world, state, t, waiting);
        box_audio_timeline_quantized(world, state, waiting);
        state->spatial_dirty = false;
    } else {
        box_audio_music_update(world, state, NULL, false);
        ecs_vec_clear(&state->moved);
        ecs_vec_clear(&state->plays);
        ecs_vec_clear(&state->notes);
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
    box_audio_music_send(world, state, t);
    box_audio_meters_update(world, state, t);
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
            state->bus_freed |= (uint64_t)1 << s->index;
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

    state->meter_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "AudioMeters" }),
        .terms = {
            { .id = ecs_id(AudioMeter), .inout = EcsOut, .src.id = EcsSelf },
            { .id = ecs_id(AudioBusState), .inout = EcsIn, .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(AudioEffectState), .inout = EcsIn, .src.id = EcsSelf,
                .oper = EcsOptional }
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
