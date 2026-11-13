/**
 * @file audio/modulator_sync.c
 * @brief The AudioModulator entities: their targets resolved as the ones of automation clips,
 * their slots of the modulators of the core, the commands that start, change and release them,
 * and the beat clock of the core they need.
 *
 * The module sends a modulator once, and again only when what it sends changes. The core runs
 * the modulator from there on, on a bus, an effect slot or a macro before its maps shape it.
 *
 * The index of the parameter of an effect is found by writing two marks into a copy of its
 * component and looking for them in the params its builder makes, so no table is needed.
 */

#include "module.h"
#include <string.h>

/** Two values written into a member to find its parameter, which no builder makes up */
#define BOX_AUDIO_MOD_MARK_A 0.1234567f
#define BOX_AUDIO_MOD_MARK_B 0.8765433f

int32_t box_audio_effect_member_param(
    const ecs_world_t *world,
    ecs_entity_t component,
    const void *ptr,
    ecs_size_t size,
    const char *member)
{
    uint64_t copy[BOX_AUDIO_AUTOMATION_COMPONENT / 8];
    float a[BOX_AUDIO_EFFECT_PARAMS], b[BOX_AUDIO_EFFECT_PARAMS];
    ecs_meta_cursor_t cur;
    char *error;
    int32_t i, result = -1;
    ecs_os_memcpy(copy, ptr, size);
    error = box_audio_member_cursor(world, component, copy, member, &cur);
    if (error) {
        ecs_os_free(error);
        goto done;
    }

    ecs_meta_set_float(&cur, (double)BOX_AUDIO_MOD_MARK_A);
    box_audio_module_effect_params(world, component, copy, a);
    ecs_os_memcpy(copy, ptr, size);
    error = box_audio_member_cursor(world, component, copy, member, &cur);
    if (error) {
        ecs_os_free(error);
        goto done;
    }

    ecs_meta_set_float(&cur, (double)BOX_AUDIO_MOD_MARK_B);
    box_audio_module_effect_params(world, component, copy, b);
    for (i = 0; i < BOX_AUDIO_EFFECT_PARAMS && result < 0; i ++) {
        if (a[i] == BOX_AUDIO_MOD_MARK_A && b[i] == BOX_AUDIO_MOD_MARK_B) {
            result = i;
        }
    }

done:
    return result;
}

bool box_audio_member_frequency(
    ecs_entity_t component,
    const char *member)
{
    const char *dot = strrchr(member, '.'), *name = dot ? dot + 1 : member;

    return !strcmp(name, "cutoff") || !strcmp(name, "center") ||
        !strcmp(name, "frequency") || (component == ecs_id(Delay) && !strcmp(name, "damping"));
}

/** Get the bus index of a bus entity or the master.
 *
 * @param world The world
 * @param state The state
 * @param bus The entity
 * @return The index, -1 when the entity is no bus with an index
 */
static int32_t box_audio_modulator_bus(
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

/** Build the command of a modulator, its slot aside.
 *
 * @param world The world
 * @param state The state
 * @param mod The modulator
 * @param cmd Receives the command
 * @param error Receives the reason it cannot run, to free with ecs_os_free, else NULL
 * @return Whether the command can be sent: false with an error, or while its target is not in
 *         the output
 */
static bool box_audio_modulator_build(
    ecs_world_t *world,
    box_audio_module_t *state,
    const AudioModulator *mod,
    box_audio_cmd_modulator_t *cmd,
    char **error)
{
    const AudioSettings *settings;
    const AudioEffectState *es;
    const EcsComponent *c;
    box_audio_target_t spec;
    const void *ptr;
    int32_t index = -1, key = -1, param, i;
    bool result = false;
    char *path;
    ecs_os_zeromem(cmd);
    *error = box_audio_target_resolve(world, state, mod->target, mod->component, mod->member,
        &spec);
    if (*error) {
        goto done;
    }

    cmd->depth = mod->depth;
    if (spec.kind == BOX_AUDIO_TARGET_MACRO) {
        if (mod->scale == AudioModOctaves) {
            *error = flecs_asprintf("ModOctaves applies to a frequency, not to the value "
                "of a macro");
            goto done;
        }

        if (mod->source == AudioModFollower && !mod->key) {
            *error = flecs_asprintf("a follower of a macro needs a key bus");
            goto done;
        }

        index = box_audio_macro_slot(world, mod->target);
        if (index < 0) {
            goto done;
        }

        cmd->target = BOX_AUDIO_MOD_TARGET_MACRO;
        cmd->param = 0;
        cmd->scale = BOX_AUDIO_MOD_LINEAR;
    } else if (spec.kind == BOX_AUDIO_TARGET_BUS) {
        if (mod->scale == AudioModOctaves) {
            *error = flecs_asprintf("ModOctaves applies to a frequency, not to '%s' of "
                "Bus", mod->member);
            goto done;
        }

        index = box_audio_modulator_bus(world, state, mod->target);
        if (index < 0 || ecs_has_id(world, mod->target, EcsDisabled)) {
            goto done;
        }

        cmd->target = BOX_AUDIO_MOD_TARGET_BUS;
        cmd->param = spec.param;
        cmd->scale = BOX_AUDIO_MOD_LINEAR;
        if (mod->target == state->master && spec.param == BOX_AUDIO_PARAM_VOLUME) {
            settings = ecs_singleton_get(world, AudioSettings);
            cmd->depth *= settings ? settings->master_volume : 1.0f;
        }

        key = index;
    } else {
        ptr = ecs_get_id(world, mod->target, mod->component);
        c = ecs_get(world, mod->component, EcsComponent);
        param = ptr && c ? box_audio_effect_member_param(world, mod->component, ptr, c->size,
            mod->member) : -1;
        if (param < 0) {
            *error = flecs_asprintf("member '%s' does not reach the output as the component "
                "is, such as the time of a Delay with a sync", mod->member);
            goto done;
        }

        if (mod->scale == AudioModOctaves &&
            !box_audio_member_frequency(mod->component, mod->member))
        {
            *error = flecs_asprintf("ModOctaves applies to a frequency, not to '%s'",
                mod->member);
            goto done;
        }

        index = box_audio_module_effect_slot(world, mod->target);
        es = ecs_get(world, mod->target, AudioEffectState);
        if (index < 0 || !es || ecs_has_id(world, mod->target, EcsDisabled) ||
            box_audio_module_effect_component(world, mod->target) != mod->component)
        {
            goto done;
        }

        cmd->target = BOX_AUDIO_MOD_TARGET_EFFECT;
        cmd->param = (uint32_t)param;
        cmd->kind = box_audio_module_effect_params(world, mod->component, NULL, NULL);
        cmd->scale = mod->scale == AudioModOctaves ? BOX_AUDIO_MOD_OCTAVES :
            BOX_AUDIO_MOD_LINEAR;
        key = es->bus;
    }

    if (mod->source == AudioModFollower && mod->key) {
        key = box_audio_modulator_bus(world, state, mod->key);
        if (key < 0 && !(ecs_is_alive(world, mod->key) && ecs_has(world, mod->key, AudioBus))) {
            path = ecs_is_alive(world, mod->key) ? ecs_get_path(world, mod->key) : NULL;
            *error = flecs_asprintf("the key '%s' is no bus", path ? path : "(deleted)");
            ecs_os_free(path);
            goto done;
        }

        if (key < 0) {
            goto done;
        }
    }

    cmd->index = (uint32_t)index;
    cmd->source = mod->source == AudioModSteps ? BOX_AUDIO_MOD_STEPS :
        (mod->source == AudioModFollower ? BOX_AUDIO_MOD_FOLLOWER : BOX_AUDIO_MOD_LFO);
    cmd->wave = (uint32_t)mod->wave <= (uint32_t)AudioModRandom ? (uint32_t)mod->wave :
        BOX_AUDIO_MOD_SINE;
    cmd->unipolar = mod->unipolar ? 1u : 0u;
    cmd->key = mod->source == AudioModFollower ? (uint32_t)key : BOX_AUDIO_BUS_NONE;
    cmd->step_count = mod->step_count < 1 ? 1u : (mod->step_count > BOX_AUDIO_MOD_STEP_MAX ?
        BOX_AUDIO_MOD_STEP_MAX : (uint32_t)mod->step_count);
    cmd->rate = mod->rate;
    cmd->sync = mod->sync;
    cmd->phase = mod->phase;
    cmd->smooth = mod->smooth;
    cmd->attack = mod->attack;
    cmd->release = mod->release;
    for (i = 0; i < BOX_AUDIO_MOD_STEP_MAX; i ++) {
        cmd->steps[i] = mod->steps[i];
    }

    result = true;
done:
    return result;
}

/** Log the error of a modulator once, until it changes.
 *
 * @param world The world
 * @param state The state
 * @param e The modulator
 * @param ms Its state
 * @param error The reason
 */
static void box_audio_modulator_error(
    ecs_world_t *world,
    box_audio_module_t *state,
    ecs_entity_t e,
    AudioModulatorState *ms,
    const char *error)
{
    char *path;
    if (!ms->logged) {
        path = ecs_get_path(world, e);
        ecs_err("box.audio: modulator '%s' does nothing: %s", path, error);
        ecs_os_free(path);
        ms->logged = true;
        state->modulators.stats.errors ++;
    }
}

/** Find a slot for a modulator: its own, else the first free one.
 *
 * @param mods The modulators
 * @param e The modulator
 * @param ms Its state
 * @return The slot, -1 when every slot is taken
 */
static int32_t box_audio_modulator_slot(
    box_audio_modulators_t *mods,
    ecs_entity_t e,
    AudioModulatorState *ms)
{
    int32_t i, result = -1;
    if (ms->slot && mods->slots[ms->slot - 1].entity == e) {
        result = ms->slot - 1;
        goto done;
    }

    for (i = 0; i < BOX_AUDIO_MOD_MAX && result < 0; i ++) {
        if (!mods->slots[i].entity) {
            result = i;
        }
    }

    if (result >= 0) {
        mods->slots[result].entity = e;
        ms->slot = (uint8_t)(result + 1);
    }

done:
    return result;
}

/** Write a modulator command, when it differs from what the slot got.
 *
 * @param state The state
 * @param slot The slot
 * @param cmd The command, its slot set here
 */
static void box_audio_modulator_send(
    box_audio_module_t *state,
    uint32_t slot,
    box_audio_cmd_modulator_t *cmd)
{
    box_audio_mod_sent_t *sent = &state->modulators.slots[slot];
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
        box_audio_write_modulator(&state->writer, cmd);
        if (cmd->target == BOX_AUDIO_MOD_TARGET_NONE) {
            state->modulators.stats.releases ++;
        } else {
            state->modulators.stats.commands ++;
        }
    }

    sent->serial = state->serial;
    sent->cmd = *cmd;
done:
    return;
}

void box_audio_modulators_sync(
    ecs_world_t *world,
    box_audio_module_t *state,
    box_audio_transport_t *t)
{
    box_audio_modulators_t *mods = &state->modulators;
    box_audio_cmd_modulator_t cmd, none;
    uint32_t keys[BOX_AUDIO_MOD_MAX][3];
    int32_t key_count = 0, i, j, slot, same;
    ecs_iter_t it;
    bool wanted = false;
    char *error, *path;
    (void)t;
    mods->frame ++;
    mods->frame = mods->frame ? mods->frame : 1;
    it = ecs_query_iter(world, mods->query);
    while (ecs_query_next(&it)) {
        const AudioModulator *m = ecs_field(&it, AudioModulator, 0);
        AudioModulatorState *ms = ecs_field(&it, AudioModulatorState, 1);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            ms[i].frame = mods->frame;
            if (ecs_has_id(world, e, EcsDisabled)) {
                ms[i].status = BOX_AUDIO_MOD_STATUS_DISABLED;
                continue;
            }

            if (!box_audio_modulator_build(world, state, &m[i], &cmd, &error)) {
                ms[i].status = error ? BOX_AUDIO_MOD_STATUS_ERROR : BOX_AUDIO_MOD_STATUS_WAITS;
                if (error) {
                    box_audio_modulator_error(world, state, e, &ms[i], error);
                    ecs_os_free(error);
                }

                continue;
            }

            for (j = 0, same = 0; j < key_count; j ++) {
                same += keys[j][0] == cmd.target && keys[j][1] == cmd.index &&
                    keys[j][2] == cmd.param;
            }

            if (same >= BOX_AUDIO_MOD_PER_TARGET) {
                ms[i].status = BOX_AUDIO_MOD_STATUS_CROWDED;
                error = flecs_asprintf("more than %d modulators move '%s' of its target",
                    BOX_AUDIO_MOD_PER_TARGET, m[i].member);
                box_audio_modulator_error(world, state, e, &ms[i], error);
                ecs_os_free(error);
                continue;
            }

            slot = box_audio_modulator_slot(mods, e, &ms[i]);
            if (slot < 0) {
                ms[i].status = BOX_AUDIO_MOD_STATUS_FULL;
                if (!ms[i].logged) {
                    path = ecs_get_path(world, e);
                    ecs_err("box.audio: more than %d modulators, '%s' does nothing",
                        BOX_AUDIO_MOD_MAX, path);
                    ecs_os_free(path);
                    ms[i].logged = true;
                    mods->stats.errors ++;
                }

                continue;
            }

            keys[key_count][0] = cmd.target;
            keys[key_count][1] = cmd.index;
            keys[key_count][2] = cmd.param;
            key_count ++;
            ms[i].status = BOX_AUDIO_MOD_STATUS_RUNS;
            wanted = true;
            box_audio_modulator_send(state, (uint32_t)slot, &cmd);
        }
    }

    ecs_os_zeromem(&none);
    none.target = BOX_AUDIO_MOD_TARGET_NONE;
    for (i = 0; i < BOX_AUDIO_MOD_MAX; i ++) {
        box_audio_mod_sent_t *sent = &mods->slots[i];
        ecs_entity_t e = sent->entity;
        AudioModulatorState *ms = e && ecs_is_alive(world, e) ?
            ecs_get_mut(world, e, AudioModulatorState) : NULL;
        if (!e || (ms && ms->slot == i + 1 && ms->frame == mods->frame &&
            ms->status == BOX_AUDIO_MOD_STATUS_RUNS && ecs_has(world, e, AudioModulator)))
        {
            continue;
        }

        if (sent->serial == state->serial && state->serial &&
            sent->cmd.target != BOX_AUDIO_MOD_TARGET_NONE)
        {
            cmd = none;
            box_audio_modulator_send(state, (uint32_t)i, &cmd);
        }

        if (ms && ms->slot == i + 1) {
            ms->slot = 0;
        }

        sent->entity = 0;
    }

    state->timeline.beat_wanted = wanted || state->effect_beat;
}

const char* box_audio_modulator_status(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const AudioModulatorState *ms = e && ecs_is_alive(world, e) ?
        ecs_get(world, e, AudioModulatorState) : NULL;
    const char *result = "not looked at yet";
    switch (ms ? ms->status : BOX_AUDIO_MOD_STATUS_NONE) {
    case BOX_AUDIO_MOD_STATUS_RUNS:
        result = "runs";
        break;
    case BOX_AUDIO_MOD_STATUS_WAITS:
        result = "waits: its target is not in the output";
        break;
    case BOX_AUDIO_MOD_STATUS_ERROR:
        result = "does nothing: see the error it logged";
        break;
    case BOX_AUDIO_MOD_STATUS_FULL:
        result = "does nothing: every modulator of the core is taken";
        break;
    case BOX_AUDIO_MOD_STATUS_CROWDED:
        result = "does nothing: too many modulators move its member";
        break;
    case BOX_AUDIO_MOD_STATUS_DISABLED:
        result = "disabled";
        break;
    default:
        break;
    }

    return result;
}

/** Forget that the error of a modulator was logged when it changes, so a new error logs.
 *
 * @param it The iterator of the observer
 */
static void BoxAudioModulatorSet(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        AudioModulatorState *ms = ecs_get_mut(it->world, it->entities[i],
            AudioModulatorState);
        if (ms) {
            ms->logged = false;
        }
    }
}

void box_audio_modulators_init(
    ecs_world_t *world,
    box_audio_module_t *state)
{
    state->modulators.query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Modulators" }),
        .terms = {
            { .id = ecs_id(AudioModulator), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioModulatorState), .src.id = EcsSelf }
        },
        .flags = EcsQueryMatchDisabled,
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ModulatorSet" }),
        .query.flags = EcsQueryMatchDisabled,
        .query.terms = {{ .id = ecs_id(AudioModulator), .src.id = EcsSelf }},
        .events = { EcsOnSet },
        .callback = BoxAudioModulatorSet,
        .ctx = state
    });
}

void box_audio_modulators_fini(
    box_audio_module_t *state)
{
    ecs_os_zeromem(&state->modulators.slots);
}
