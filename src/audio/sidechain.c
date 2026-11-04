/**
 * @file audio/sidechain.c
 * @brief The sidechains of the compressors: the key bus of each insert slot, the edges they add
 * to the order compile of sync.c so a key runs before the bus it ducks, and their cycles.
 *
 * A sidechain that would form a cycle logs one error and the compressor keys on its own input.
 */

#include "module.h"

/** Get the entity of a bus index: the master for 0.
 *
 * @param state The state
 * @param bus The bus index
 * @return The entity, 0 for a free index
 */
static ecs_entity_t box_audio_bus_entity(
    const box_audio_module_t *state,
    int32_t bus)
{
    return bus ? state->bus_entity[bus] : state->master;
}

/** Get the sidechain of the Compressor in an insert slot of a bus index.
 *
 * @param world The world
 * @param state The state
 * @param bus The bus index
 * @param index The insert slot
 * @return The sidechain entity, 0 when the slot holds no Compressor or it has none
 */
static ecs_entity_t box_audio_sidechain_of(
    ecs_world_t *world,
    const box_audio_module_t *state,
    int32_t bus,
    int32_t index)
{
    ecs_entity_t e = box_audio_bus_entity(state, bus), effect;
    const AudioBus *b = e && ecs_is_alive(world, e) ? ecs_get(world, e, AudioBus) : NULL;
    const Compressor *c;
    ecs_entity_t result = 0;
    effect = b ? b->effects[index] : 0;
    if (!effect || !ecs_is_alive(world, effect)) {
        goto done;
    }

    c = ecs_get(world, effect, Compressor);
    if (c && c->sidechain &&
        box_audio_module_effect_kind(world, effect, NULL) == BOX_AUDIO_EFFECT_COMPRESSOR)
    {
        result = c->sidechain;
    }

done:
    return result;
}

void box_audio_sidechains_check(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    int32_t i, j;
    if (state->order_dirty ||
        (!state->sidechain_count && !ecs_count_id(world, ecs_id(Compressor))))
    {
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_BUS_MAX && !state->order_dirty; i ++) {
        for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
            if (box_audio_sidechain_of(world, state, i, j) != state->sidechains[i][j]) {
                state->order_dirty = true;
                break;
            }
        }
    }

done:
    return;
}

/** Check whether a bus reaches another through outputs, sends and sidechains.
 *
 * @param out Output of each bus index
 * @param sends BOX_AUDIO_SENDS sends of each bus index in a row, BOX_AUDIO_BUS_NONE for none
 * @param keyed Buses each bus index is the sidechain of, a bit each
 * @param from The bus to start from
 * @param to The bus to reach
 * @return Whether to is reached
 */
static bool box_audio_reaches(
    const uint8_t *out,
    const uint8_t *sends,
    const uint64_t *keyed,
    uint8_t from,
    uint8_t to)
{
    uint8_t stack[BOX_AUDIO_BUS_MAX];
    uint64_t seen = (uint64_t)1 << from;
    int32_t top = 0, i;
    bool result = false;
    stack[top ++] = from;
    while (top && !result) {
        uint8_t bus = stack[-- top];
        uint64_t next = keyed[bus];
        if (out[bus] && out[bus] != BOX_AUDIO_BUS_NONE) {
            next |= (uint64_t)1 << out[bus];
        }

        for (i = 0; i < BOX_AUDIO_SENDS; i ++) {
            uint8_t n = sends[bus * BOX_AUDIO_SENDS + i];
            if (n && n != BOX_AUDIO_BUS_NONE) {
                next |= (uint64_t)1 << n;
            }
        }

        result = (next >> to) & 1;
        next &= ~seen;
        seen |= next;
        for (i = 0; next && i < BOX_AUDIO_BUS_MAX; i ++) {
            if (next & ((uint64_t)1 << i)) {
                stack[top ++] = (uint8_t)i;
            }
        }
    }

    return result;
}

/** Log once that the sidechain of a compressor would form a cycle, by the names of the
 * compressor, its bus and the key.
 *
 * @param world The world
 * @param state The state
 * @param bus The bus index of the compressor
 * @param index Its insert slot
 * @param key The sidechain entity
 */
static void box_audio_sidechain_error(
    ecs_world_t *world,
    box_audio_module_t *state,
    int32_t bus,
    int32_t index,
    ecs_entity_t key)
{
    ecs_entity_t e = box_audio_bus_entity(state, bus);
    const AudioBus *b = ecs_get(world, e, AudioBus);
    char *effect, *channel, *name;
    if (state->sidechain_refused[bus][index] == key) {
        goto done;
    }

    effect = ecs_get_path(world, b->effects[index]);
    channel = ecs_get_path(world, e);
    name = ecs_get_path(world, key);
    ecs_err("box.audio: the compressor '%s' on '%s' has the sidechain '%s', which runs after "
        "'%s', a cycle: '%s' keys on its own input", effect, channel, name, channel, effect);
    ecs_os_free(effect);
    ecs_os_free(channel);
    ecs_os_free(name);
    state->sidechain_refused[bus][index] = key;

done:
    return;
}

void box_audio_sidechains_compile(
    ecs_world_t *world,
    box_audio_module_t *state,
    uint64_t *keyed)
{
    int32_t i, j;
    state->sidechain_count = 0;
    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        keyed[i] = 0;
    }

    for (i = 0; i < BOX_AUDIO_BUS_MAX; i ++) {
        for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
            ecs_entity_t key = box_audio_bus_entity(state, i) ?
                box_audio_sidechain_of(world, state, i, j) : 0;
            uint8_t k = key ? box_audio_bus_of(world, state, key) : 0;
            state->sidechains[i][j] = key;
            state->keys[i][j] = BOX_AUDIO_BUS_NONE;
            state->sidechain_count += key != 0;
            if (key && key == state->master) {
                box_audio_sidechain_error(world, state, i, j, key);
                continue;
            }

            if (!k || k == i) {
                state->sidechain_refused[i][j] = 0;
                continue;
            }

            if (box_audio_reaches(state->route_out, state->route_sends[0], keyed, (uint8_t)i,
                k))
            {
                box_audio_sidechain_error(world, state, i, j, key);
                continue;
            }

            state->sidechain_refused[i][j] = 0;
            state->keys[i][j] = k;
            keyed[k] |= (uint64_t)1 << i;
        }
    }
}
