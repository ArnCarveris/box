/**
 * @file audio/bus_sync.c
 * @brief The buses of the sync system: their indices in the core, their parameters and sends,
 * the solo and the processing order the core runs them in.
 */

#include "module.h"

void box_audio_bus_set(
    box_audio_module_t *state,
    uint32_t bus,
    box_audio_param_t param,
    float value)
{
    if (box_audio_reserve(state)) {
        box_audio_write_bus_set(&state->writer, bus, param, value, 0);
    }
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
 * A bus of a song that fades out before it unloads is sent muted.
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
        !(state->audible & ((uint64_t)1 << s->index)) ||
        (state->song.silenced & ((uint64_t)1 << s->index));
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

void box_audio_buses(
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

void box_audio_solo(
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

void box_audio_buses_send(
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
 * @param keyed Buses each bus index is the sidechain of, a bit each, NULL for none
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
    const uint64_t *keyed,
    uint8_t *color,
    int32_t *depth,
    uint8_t *stack,
    int32_t *top,
    uint8_t bus,
    int32_t *cycle)
{
    uint8_t next[1 + BOX_AUDIO_SENDS + BOX_AUDIO_BUS_MAX] = { out[bus] };
    int32_t i, k, d = 0, count = 1 + BOX_AUDIO_SENDS;
    uint64_t mask = keyed ? keyed[bus] : 0;
    bool result = false;
    for (i = 0; i < BOX_AUDIO_SENDS; i ++) {
        next[1 + i] = sends[bus * BOX_AUDIO_SENDS + i];
    }

    for (i = 0; mask && i < BOX_AUDIO_BUS_MAX; i ++) {
        if (mask & ((uint64_t)1 << i)) {
            next[count ++] = (uint8_t)i;
        }
    }

    color[bus] = 1;
    stack[(*top) ++] = bus;
    for (i = 0; i < count && !result; i ++) {
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
                result = box_audio_walk(out, sends, keyed, color, depth, stack, top, n,
                    cycle);
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

void box_audio_compile(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    uint8_t *out = state->route_out, (*sends)[BOX_AUDIO_SENDS] = state->route_sends;
    uint8_t color[BOX_AUDIO_BUS_MAX] = {0}, pre[BOX_AUDIO_BUS_MAX] = {0};
    uint8_t stack[BOX_AUDIO_BUS_MAX], order[BOX_AUDIO_BUS_MAX];
    int32_t depth[BOX_AUDIO_BUS_MAX] = {0}, count = 0, top = 0, cycle = -1, i, j, k;
    uint64_t keyed[BOX_AUDIO_BUS_MAX];
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

    box_audio_sidechains_compile(world, state, keyed);
    for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
        if (state->bus_entity[i] && !color[i] &&
            box_audio_walk(out, state->route_sends[0], NULL, color, depth, stack, &top,
                (uint8_t)i, &cycle))
        {
            box_audio_cycle_error(world, state, stack, top, cycle);
            state->stats.cycles ++;
            goto done;
        }
    }

    if (state->sidechain_count) {
        for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
            color[i] = 0;
            depth[i] = 0;
        }

        for (i = 1; i < BOX_AUDIO_BUS_MAX; i ++) {
            top = 0;
            if (state->bus_entity[i] && !color[i]) {
                box_audio_walk(out, state->route_sends[0], keyed, color, depth, stack, &top,
                    (uint8_t)i, &cycle);
            }
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
