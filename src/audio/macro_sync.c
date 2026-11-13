/**
 * @file audio/macro_sync.c
 * @brief The AudioMacro entities and their AudioMacroMap children: the macro and map slots of
 * the core, the MACRO commands of the values of the components and the MACRO_MAP commands of
 * the maps, whose targets resolve as the ones of automation clips and modulators.
 *
 * The core shapes the value of a macro into the parameters of its maps, so an automation lane
 * of the value (MACRO commands at their frames) and its modulators move them sample accurately,
 * and the module never writes the components of the targets.
 *
 * The module sends the value of a macro at once when its component changes, and a map when
 * what it sends changes.
 */

#include "module.h"
#include <string.h>

int32_t box_audio_macro_slot(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const AudioMacroState *ms = e && ecs_is_alive(world, e) ?
        ecs_get(world, e, AudioMacroState) : NULL;

    return ms && ms->slot && ms->status == BOX_AUDIO_MACRO_STATUS_RUNS ? ms->slot - 1 : -1;
}

/** Get the value of a macro the core gets, 0..1.
 *
 * @param value The value of the component
 * @return The clamped value, 0 for NaN
 */
static float box_audio_macro_value(
    float value)
{
    return value >= 0 ? (value <= 1 ? value : 1.0f) : 0.0f;
}

/** Find a slot for a macro: its own, else the first free one.
 *
 * @param macros The macros
 * @param e The macro
 * @param ms Its state
 * @return The slot, -1 when every slot is taken
 */
static int32_t box_audio_macro_find_slot(
    box_audio_macros_t *macros,
    ecs_entity_t e,
    AudioMacroState *ms)
{
    int32_t i, result = -1;
    if (ms->slot && macros->slots[ms->slot - 1].entity == e) {
        result = ms->slot - 1;
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_MACRO_MAX && result < 0; i ++) {
        if (!macros->slots[i].entity) {
            result = i;
        }
    }

    if (result >= 0) {
        macros->slots[result].entity = e;
        macros->slots[result].serial = 0;
        ms->slot = (uint8_t)(result + 1);
    }

done:
    return result;
}

/** Give every macro a slot, send the values that changed and free the slots of the macros
 * that are gone or disabled.
 *
 * @param world The world
 * @param state The state
 */
static void box_audio_macros_values(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_macros_t *macros = &state->macros;
    ecs_iter_t it = ecs_query_iter(world, macros->query);
    int32_t i, slot;
    char *path;
    while (ecs_query_next(&it)) {
        const AudioMacro *m = ecs_field(&it, AudioMacro, 0);
        AudioMacroState *ms = ecs_field(&it, AudioMacroState, 1);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            box_audio_macro_sent_t *sent;
            float value;
            ms[i].frame = macros->frame;
            if (ms[i].maps <= BOX_AUDIO_MACRO_MAPS) {
                ms[i].maps_logged = false;
            }

            ms[i].maps = 0;
            if (ecs_has_id(world, e, EcsDisabled)) {
                ms[i].status = BOX_AUDIO_MACRO_STATUS_DISABLED;
                continue;
            }

            slot = box_audio_macro_find_slot(macros, e, &ms[i]);
            if (slot < 0) {
                ms[i].status = BOX_AUDIO_MACRO_STATUS_FULL;
                if (!ms[i].logged) {
                    path = ecs_get_path(world, e);
                    ecs_err("box.audio: more than %d macros, '%s' does nothing",
                        BOX_AUDIO_MACROS, path);
                    ecs_os_free(path);
                    ms[i].logged = true;
                    macros->stats.errors ++;
                }

                continue;
            }

            ms[i].status = BOX_AUDIO_MACRO_STATUS_RUNS;
            ms[i].logged = false;
            sent = &macros->slots[slot];
            value = box_audio_macro_value(m[i].value);
            if (state->serial && (sent->serial != state->serial || sent->value != value) &&
                box_audio_reserve(state))
            {
                box_audio_write_macro(&state->writer, (uint32_t)slot, value, 0);
                macros->stats.values ++;
                sent->serial = state->serial;
                sent->value = value;
            }
        }
    }

    for (i = 0; i < BOX_AUDIO_MACRO_MAX; i ++) {
        box_audio_macro_sent_t *sent = &macros->slots[i];
        ecs_entity_t e = sent->entity;
        AudioMacroState *ms = e && ecs_is_alive(world, e) ?
            ecs_get_mut(world, e, AudioMacroState) : NULL;
        if (!e || (ms && ms->slot == i + 1 && ms->frame == macros->frame &&
            ms->status == BOX_AUDIO_MACRO_STATUS_RUNS && ecs_has(world, e, AudioMacro)))
        {
            continue;
        }

        if (ms && ms->slot == i + 1) {
            ms->slot = 0;
        }

        sent->entity = 0;
        sent->serial = 0;
    }
}

/** Get the bus index of a bus entity or the master.
 *
 * @param world The world
 * @param state The state
 * @param bus The entity
 * @return The index, -1 when the entity is no bus with an index
 */
static int32_t box_audio_macro_bus(
    ecs_world_t *world,
    const box_audio_module_t *state,
    ecs_entity_t bus)
{
    int32_t result = -1;
    if (bus == state->master) {
        result = 0;
    } else if (bus) {
        result = box_audio_bus_of(world, state, bus);
        result = result ? result : -1;
    }

    return result;
}

/** Build the command of a map, its slot aside.
 *
 * @param world The world
 * @param state The state
 * @param map The map
 * @param macro Slot of its macro
 * @param cmd Receives the command
 * @param error Receives the reason it cannot run, to free with ecs_os_free, else NULL
 * @return Whether the command can be sent: false with an error, or while its target is not in
 *         the output
 */
static bool box_audio_macro_map_build(
    ecs_world_t *world,
    box_audio_module_t *state,
    const AudioMacroMap *map,
    int32_t macro,
    box_audio_cmd_macro_map_t *cmd,
    char **error)
{
    const AudioSettings *settings;
    const AudioEffectState *es;
    const EcsComponent *c;
    box_audio_target_t spec;
    const void *ptr;
    int32_t index = -1, param;
    float min = map->min, max = map->max, gain;
    bool result = false;
    ecs_os_zeromem(cmd);
    *error = box_audio_target_resolve(world, state, map->target, map->component, map->member,
        &spec);
    if (*error) {
        goto done;
    }

    if (!(min == min) || !(max == max)) {
        *error = flecs_asprintf("min and max must be numbers");
        goto done;
    }

    if (spec.kind == BOX_AUDIO_TARGET_MACRO) {
        *error = flecs_asprintf("a map cannot move the value of a macro");
        goto done;
    }

    if (spec.kind == BOX_AUDIO_TARGET_BUS) {
        if (map->scale == AudioModOctaves) {
            *error = flecs_asprintf("ModOctaves applies to a frequency, not to '%s' of "
                "Bus", map->member);
            goto done;
        }

        index = box_audio_macro_bus(world, state, map->target);
        if (index < 0 || ecs_has_id(world, map->target, EcsDisabled)) {
            goto done;
        }

        cmd->target = BOX_AUDIO_MOD_TARGET_BUS;
        cmd->param = spec.param;
        cmd->scale = BOX_AUDIO_MOD_LINEAR;
        if (map->target == state->master && spec.param == BOX_AUDIO_PARAM_VOLUME) {
            settings = ecs_singleton_get(world, AudioSettings);
            gain = settings ? settings->master_volume : 1.0f;
            min *= gain;
            max *= gain;
        }
    } else {
        ptr = ecs_get_id(world, map->target, map->component);
        c = ecs_get(world, map->component, EcsComponent);
        param = ptr && c ? box_audio_effect_member_param(world, map->component, ptr, c->size,
            map->member) : -1;
        if (param < 0) {
            *error = flecs_asprintf("member '%s' does not reach the output as the component "
                "is, such as the time of a Delay with a sync", map->member);
            goto done;
        }

        if (map->scale == AudioModOctaves &&
            !box_audio_member_frequency(map->component, map->member))
        {
            *error = flecs_asprintf("ModOctaves applies to a frequency, not to '%s'",
                map->member);
            goto done;
        }

        if (map->scale == AudioModOctaves && !(min > 0 && max > 0)) {
            *error = flecs_asprintf("ModOctaves needs a min and a max above 0, not %g "
                "and %g", (double)min, (double)max);
            goto done;
        }

        index = box_audio_module_effect_slot(world, map->target);
        es = ecs_get(world, map->target, AudioEffectState);
        if (index < 0 || !es || ecs_has_id(world, map->target, EcsDisabled) ||
            box_audio_module_effect_component(world, map->target) != map->component)
        {
            goto done;
        }

        cmd->target = BOX_AUDIO_MOD_TARGET_EFFECT;
        cmd->param = (uint32_t)param;
        cmd->kind = box_audio_module_effect_params(world, map->component, NULL, NULL);
        cmd->scale = map->scale == AudioModOctaves ? BOX_AUDIO_MOD_OCTAVES :
            BOX_AUDIO_MOD_LINEAR;
    }

    cmd->index = (uint32_t)index;
    cmd->macro = (uint32_t)macro;
    cmd->min = min;
    cmd->max = max;
    cmd->curve = map->curve == map->curve ? (map->curve < -1 ? -1.0f :
        (map->curve > 1 ? 1.0f : map->curve)) : 0.0f;
    result = true;
done:
    return result;
}

/** Log the error of a map once, until it changes.
 *
 * @param world The world
 * @param state The state
 * @param e The map
 * @param ms Its state
 * @param error The reason
 */
static void box_audio_macro_map_error(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioMacroMapState *ms,
    const char *error)
{
    char *path;
    if (!ms->logged) {
        path = ecs_get_path(world, e);
        ecs_err("box.audio: macro map '%s' does nothing: %s", path, error);
        ecs_os_free(path);
        ms->logged = true;
        state->macros.stats.errors ++;
    }
}

/** Find a slot for a map: its own, else the first free one.
 *
 * @param macros The macros
 * @param e The map
 * @param ms Its state
 * @return The slot, -1 when every slot is taken
 */
static int32_t box_audio_macro_map_slot(
    box_audio_macros_t *macros,
    ecs_entity_t e,
    AudioMacroMapState *ms)
{
    int32_t i, result = -1;
    if (ms->slot && macros->maps[ms->slot - 1].entity == e) {
        result = ms->slot - 1;
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_MACRO_MAP_MAX && result < 0; i ++) {
        if (!macros->maps[i].entity) {
            result = i;
        }
    }

    if (result >= 0) {
        macros->maps[result].entity = e;
        ms->slot = (uint16_t)(result + 1);
    }

done:
    return result;
}

/** Write a map command, when it differs from what the slot got.
 *
 * @param state The state
 * @param slot The slot
 * @param cmd The command, its slot set here
 */
static void box_audio_macro_map_send(
    box_audio_module_t *state,
    uint32_t slot,
    box_audio_cmd_macro_map_t *cmd)
{
    box_audio_map_sent_t *sent = &state->macros.maps[slot];
    cmd->slot = slot;
    cmd->header = (box_audio_cmd_header_t){0};
    if (sent->serial == state->serial && !memcmp(&sent->cmd, cmd, sizeof(*cmd))) {
        goto done;
    }

    if (sent->serial != state->serial && cmd->target == BOX_AUDIO_MOD_TARGET_NONE) {
        sent->serial = state->serial;
        sent->cmd = *cmd;
        goto done;
    }

    if (box_audio_reserve(state)) {
        box_audio_write_macro_map(&state->writer, cmd);
        if (cmd->target == BOX_AUDIO_MOD_TARGET_NONE) {
            state->macros.stats.releases ++;
        } else {
            state->macros.stats.maps ++;
        }
    }

    sent->serial = state->serial;
    sent->cmd = *cmd;
done:
    return;
}

/** Collect the maps that can run this frame: each with a macro that has a slot, a target in
 * the output and room among the maps of its macro.
 *
 * @param world The world
 * @param state The state
 * @return Number of candidates
 */
static int32_t box_audio_macro_maps_collect(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    box_audio_macros_t *macros = &state->macros;
    ecs_iter_t it = ecs_query_iter(world, macros->map_query);
    box_audio_cmd_macro_map_t cmd;
    int32_t i, macro, count = 0;
    char *error, *path, *map_path;
    while (ecs_query_next(&it)) {
        const AudioMacroMap *m = ecs_field(&it, AudioMacroMap, 0);
        AudioMacroMapState *ms = ecs_field(&it, AudioMacroMapState, 1);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i], parent;
            AudioMacroState *owner;
            box_audio_map_candidate_t *cand;
            ms[i].frame = macros->frame;
            if (ecs_has_id(world, e, EcsDisabled)) {
                ms[i].status = BOX_AUDIO_MACRO_STATUS_DISABLED;
                continue;
            }

            parent = ecs_get_parent(world, e);
            owner = parent && ecs_has(world, parent, AudioMacro) ?
                ecs_get_mut(world, parent, AudioMacroState) : NULL;
            if (!owner) {
                ms[i].status = BOX_AUDIO_MACRO_STATUS_ERROR;
                box_audio_macro_map_error(world, state, e, &ms[i],
                    "it is not a child of an entity with Macro");
                continue;
            }

            macro = box_audio_macro_slot(world, parent);
            if (macro < 0) {
                ms[i].status = BOX_AUDIO_MACRO_STATUS_WAITS;
                continue;
            }

            if (!box_audio_macro_map_build(world, state, &m[i], macro, &cmd, &error)) {
                ms[i].status = error ? BOX_AUDIO_MACRO_STATUS_ERROR :
                    BOX_AUDIO_MACRO_STATUS_WAITS;
                if (error) {
                    box_audio_macro_map_error(world, state, e, &ms[i], error);
                    ecs_os_free(error);
                }

                continue;
            }

            owner->maps ++;
            if (owner->maps > BOX_AUDIO_MACRO_MAPS || count >= BOX_AUDIO_MACRO_MAP_MAX) {
                ms[i].status = BOX_AUDIO_MACRO_STATUS_FULL;
                if (!owner->maps_logged) {
                    path = ecs_get_path(world, parent);
                    map_path = ecs_get_path(world, e);
                    ecs_err("box.audio: macro '%s' has more than %d maps, '%s' does nothing",
                        path, BOX_AUDIO_MACRO_MAPS, map_path);
                    ecs_os_free(path);
                    ecs_os_free(map_path);
                    owner->maps_logged = true;
                    macros->stats.errors ++;
                }

                continue;
            }

            ms[i].status = BOX_AUDIO_MACRO_STATUS_RUNS;
            cand = &macros->candidates[count ++];
            cand->entity = e;
            cand->macro = parent;
            cand->cmd = cmd;
            cand->taken = false;
        }
    }

    return count;
}

/** Sort out the maps of one member: the newest map, the one with the higher entity id, moves
 * it, and each other logs one warning that names both.
 *
 * @param world The world
 * @param state The state
 * @param count Number of candidates
 */
static void box_audio_macro_maps_claim(
    ecs_world_t *world,
    box_audio_module_t *state,
    int32_t count)
{
    box_audio_map_candidate_t *cands = state->macros.candidates;
    int32_t i, j, winner;
    for (i = 0; i < count; i ++) {
        const box_audio_cmd_macro_map_t *a = &cands[i].cmd;
        AudioMacroMapState *ms;
        winner = -1;
        for (j = 0; j < count; j ++) {
            const box_audio_cmd_macro_map_t *b = &cands[j].cmd;
            if (j != i && a->target == b->target && a->index == b->index &&
                a->param == b->param && a->kind == b->kind &&
                cands[j].entity > cands[i].entity &&
                (winner < 0 || cands[j].entity > cands[winner].entity))
            {
                winner = j;
            }
        }

        if (winner < 0) {
            continue;
        }

        cands[i].taken = true;
        ms = ecs_get_mut(world, cands[i].entity, AudioMacroMapState);
        if (ms) {
            ms->status = BOX_AUDIO_MACRO_STATUS_TAKEN;
            if (!ms->warned) {
                const AudioMacroMap *m = ecs_get(world, cands[i].entity, AudioMacroMap);
                char *loser = ecs_get_path(world, cands[i].entity);
                char *newer = ecs_get_path(world, cands[winner].entity);
                ecs_warn("box.audio: macro maps '%s' and '%s' move the same member '%s', the "
                    "newer '%s' wins", loser, newer, m && m->member ? m->member : "", newer);
                ecs_os_free(loser);
                ecs_os_free(newer);
                ms->warned = true;
                state->macros.stats.warnings ++;
            }
        }
    }
}

void box_audio_macros_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    box_audio_macros_t *macros = &state->macros;
    box_audio_map_candidate_t *cands = macros->candidates;
    box_audio_cmd_macro_map_t cmd, none;
    int32_t count, i, slot;
    (void)t;
    macros->frame ++;
    macros->frame = macros->frame ? macros->frame : 1;
    box_audio_macros_values(world, state);
    count = box_audio_macro_maps_collect(world, state);
    box_audio_macro_maps_claim(world, state, count);
    for (i = 0; i < count; i ++) {
        AudioMacroMapState *ms;
        if (cands[i].taken) {
            continue;
        }

        ms = ecs_get_mut(world, cands[i].entity, AudioMacroMapState);
        slot = ms ? box_audio_macro_map_slot(macros, cands[i].entity, ms) : -1;
        if (slot < 0) {
            if (ms) {
                ms->status = BOX_AUDIO_MACRO_STATUS_FULL;
            }

            continue;
        }

        ms->warned = false;
        cmd = cands[i].cmd;
        box_audio_macro_map_send(state, (uint32_t)slot, &cmd);
    }

    ecs_os_zeromem(&none);
    none.target = BOX_AUDIO_MOD_TARGET_NONE;
    for (i = 0; i < BOX_AUDIO_MACRO_MAP_MAX; i ++) {
        box_audio_map_sent_t *sent = &macros->maps[i];
        ecs_entity_t e = sent->entity;
        AudioMacroMapState *ms = e && ecs_is_alive(world, e) ?
            ecs_get_mut(world, e, AudioMacroMapState) : NULL;
        if (!e || (ms && ms->slot == i + 1 && ms->frame == macros->frame &&
            ms->status == BOX_AUDIO_MACRO_STATUS_RUNS && ecs_has(world, e, AudioMacroMap)))
        {
            continue;
        }

        if (sent->serial == state->serial && state->serial &&
            sent->cmd.target != BOX_AUDIO_MOD_TARGET_NONE)
        {
            cmd = none;
            box_audio_macro_map_send(state, (uint32_t)i, &cmd);
        }

        if (ms && ms->slot == i + 1) {
            ms->slot = 0;
        }

        sent->entity = 0;
    }
}

const char* box_audio_macro_status(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const AudioMacroState *macro = e && ecs_is_alive(world, e) ?
        ecs_get(world, e, AudioMacroState) : NULL;
    const AudioMacroMapState *map = e && ecs_is_alive(world, e) ?
        ecs_get(world, e, AudioMacroMapState) : NULL;
    const char *result = "not looked at yet";
    uint8_t status = macro ? macro->status : (map ? map->status : BOX_AUDIO_MACRO_STATUS_NONE);
    switch (status) {
    case BOX_AUDIO_MACRO_STATUS_RUNS:
        result = "runs";
        break;
    case BOX_AUDIO_MACRO_STATUS_WAITS:
        result = "waits: its target is not in the output, or its macro does not run";
        break;
    case BOX_AUDIO_MACRO_STATUS_ERROR:
        result = "does nothing: see the error it logged";
        break;
    case BOX_AUDIO_MACRO_STATUS_FULL:
        result = macro ? "does nothing: every macro of the core is taken" :
            "does nothing: its macro has too many maps";
        break;
    case BOX_AUDIO_MACRO_STATUS_TAKEN:
        result = "does nothing: a newer map moves its member";
        break;
    case BOX_AUDIO_MACRO_STATUS_DISABLED:
        result = "disabled";
        break;
    default:
        break;
    }

    return result;
}

/** Forget that the error or the warning of a map was logged when it changes, so a new one
 * logs.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioMacroMapSet(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        AudioMacroMapState *ms = ecs_get_mut(it->world, it->entities[i],
            AudioMacroMapState);
        if (ms) {
            ms->logged = false;
            ms->warned = false;
        }
    }
}

void box_audio_macros_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    state->macros.query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Macros" }),
        .terms = {
            { .id = ecs_id(AudioMacro), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioMacroState), .src.id = EcsSelf }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    state->macros.map_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "MacroMaps" }),
        .terms = {
            { .id = ecs_id(AudioMacroMap), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioMacroMapState), .src.id = EcsSelf }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "MacroMapSet" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(AudioMacroMap), .src.id = EcsSelf }},
        .events = { EcsOnSet },
        .callback = BoxAudioMacroMapSet,
        .ctx = state
    });
}

void box_audio_macros_fini(
    box_audio_module_t *state)
{
    ecs_os_zeromem(&state->macros.slots);
    ecs_os_zeromem(&state->macros.maps);
}
