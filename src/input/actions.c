/**
 * @file input/actions.c
 * @brief The OnLoad system that resolves the bindings of the maps into actions.
 */

#include "input_internal.h"

/** The four modifier families, in the bit order of the key masks */
static const uint64_t box_input_families[4] = {
    BOX_KEY_MOD_SHIFT, BOX_KEY_MOD_CTRL, BOX_KEY_MOD_ALT, BOX_KEY_MOD_SUPER
};

/** The eight modifier keys, in the bit order of the key masks */
static const int32_t box_input_mod_keys[8] = {
    BoxKeyShiftLeft, BoxKeyShiftRight, BoxKeyCtrlLeft, BoxKeyCtrlRight,
    BoxKeyAltLeft, BoxKeyAltRight, BoxKeySuperLeft, BoxKeySuperRight
};

/** Modifier bits a key code stands for.
 *
 * A sided modifier key gives its own bit and one of the four family codes gives
 * both bits of its family, which is what makes a pure modifier binding.
 *
 * @param code The key code
 * @return The modifier bits, or 0 when the code is not a modifier
 */
static uint64_t box_input_mod_bit(
    int32_t code)
{
    uint64_t result = 0;
    switch (code) {
    case BoxKeyShiftLeft: result = BOX_KEY_MOD_SHIFT_LEFT; break;
    case BoxKeyShiftRight: result = BOX_KEY_MOD_SHIFT_RIGHT; break;
    case BoxKeyCtrlLeft: result = BOX_KEY_MOD_CTRL_LEFT; break;
    case BoxKeyCtrlRight: result = BOX_KEY_MOD_CTRL_RIGHT; break;
    case BoxKeyAltLeft: result = BOX_KEY_MOD_ALT_LEFT; break;
    case BoxKeyAltRight: result = BOX_KEY_MOD_ALT_RIGHT; break;
    case BoxKeySuperLeft: result = BOX_KEY_MOD_SUPER_LEFT; break;
    case BoxKeySuperRight: result = BOX_KEY_MOD_SUPER_RIGHT; break;
    case BoxKeyShift: result = BOX_KEY_MOD_SHIFT; break;
    case BoxKeyCtrl: result = BOX_KEY_MOD_CTRL; break;
    case BoxKeyAlt: result = BOX_KEY_MOD_ALT; break;
    case BoxKeySuper: result = BOX_KEY_MOD_SUPER; break;
    default: break;
    }

    return result;
}

uint64_t box_input_key_mask(
    int32_t code)
{
    uint64_t result = box_input_mod_bit(code);
    if (!result) {
        result = (uint64_t)code;
    }

    return result;
}

/** Widen the modifier bits of a mask to the families they belong to.
 *
 * @param mods Modifier bits
 * @return The families the bits belong to
 */
static uint64_t box_input_families_of(
    uint64_t mods)
{
    uint64_t result = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        if (mods & box_input_families[i]) {
            result |= box_input_families[i];
        }
    }

    return result;
}

/** Collect the modifier bits of the modifier keys that are down.
 *
 * @param in The Input singleton
 * @return The modifier bits of the frame
 */
static uint64_t box_input_held_mods(
    const Input *in)
{
    uint64_t result = 0;
    int32_t i;
    for (i = 0; i < 8; i ++) {
        if (in->keys[box_input_mod_keys[i]].state) {
            result |= box_input_mod_bit(box_input_mod_keys[i]);
        }
    }

    return result;
}

/** Test whether the modifiers a mask requires are held.
 *
 * A family requirement matches either side, a sided requirement only its own side
 * and a mask without modifiers matches whatever is held.
 *
 * @param required Modifier bits of the mask
 * @param held Modifier bits of the frame
 * @return Whether the requirement is met
 */
static bool box_input_mods_match(
    uint64_t required,
    uint64_t held)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        uint64_t req = required & box_input_families[i];
        uint64_t got = held & box_input_families[i];
        if (!req) {
            continue;
        }

        if (req == box_input_families[i] ? !got : (got & req) != req) {
            result = false;
        }
    }

    return result;
}

/** Count the modifier families a mask requires, which is how specific it is.
 *
 * @param required Modifier bits of the mask
 * @return Number of families the mask requires
 */
static int32_t box_input_mod_count(
    uint64_t required)
{
    int32_t result = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        if (required & box_input_families[i]) {
            result ++;
        }
    }

    return result;
}

/** Key code a key constant of the keys scope stands for.
 *
 * @param world The world
 * @param state The module state
 * @param constant The entity to test
 * @return The key code, or -1 when the entity is not a key constant
 */
static int32_t box_input_key_code(
    ecs_world_t *world,
    const InputState *state,
    ecs_entity_t constant)
{
    int32_t result = -1;
    if (constant && ecs_get_parent(world, constant) == state->keys) {
        const ecs_i32_t *value = ecs_get_pair_second(
            world, constant, EcsConstant, ecs_i32_t);
        if (value) {
            result = value[0];
        }
    }

    return result;
}

/** Add the key constant pairs of a map entity to the bindings of the frame.
 *
 * @param world The world
 * @param state The module state
 * @param map The map entity
 */
static void box_input_map_pairs(
    ecs_world_t *world,
    InputState *state,
    ecs_entity_t map)
{
    const ecs_type_t *type = ecs_get_type(world, map);
    int32_t i;
    for (i = 0; type && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (!ECS_IS_PAIR(id)) {
            continue;
        }

        int32_t code = box_input_key_code(
            world, state, ecs_pair_first(world, id));
        if (code < 0) {
            continue;
        }

        box_binding_t *out = ecs_vec_append_t(
            NULL, &state->bound, box_binding_t);
        out->mask = (uint64_t)code;
        out->action = ecs_pair_second(world, id);
        out->entity = map;
    }
}

/** Test whether the map a binding belongs to resolves its bindings.
 *
 * @param world The world
 * @param binding The binding entity
 * @return Whether the parent map is enabled, true when there is no map
 */
static bool box_input_map_enabled(
    ecs_world_t *world,
    ecs_entity_t binding)
{
    bool result = true;
    ecs_entity_t parent = ecs_get_parent(world, binding);
    const InputMap *map = parent ? ecs_get(world, parent, InputMap) : NULL;
    if (map) {
        result = map->enabled;
    }

    return result;
}

/** Collect the bindings of the frame from the components and from the map pairs.
 *
 * @param world The world
 * @param state The module state
 */
static void box_input_collect(
    ecs_world_t *world,
    InputState *state)
{
    ecs_vec_clear(&state->bound);
    ecs_iter_t it = ecs_query_iter(world, state->bindings);
    while (ecs_query_next(&it)) {
        const Binding *binding = ecs_field(&it, Binding, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!binding[i].action || !box_input_map_enabled(world, it.entities[i])) {
                continue;
            }

            box_binding_t *out = ecs_vec_append_t(
                NULL, &state->bound, box_binding_t);
            out->mask = binding[i].mask;
            out->action = binding[i].action;
            out->entity = it.entities[i];
        }
    }

    it = ecs_query_iter(world, state->maps);
    while (ecs_query_next(&it)) {
        const InputMap *map = ecs_field(&it, InputMap, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (map[i].enabled) {
                box_input_map_pairs(world, state, it.entities[i]);
            }
        }
    }
}

/** Split a key mask into the key that must be down and the modifiers it needs.
 *
 * A mask whose key is a modifier key, or that only holds modifier bits, is a pure
 * modifier binding and has no key of its own.
 *
 * @param mask The key mask
 * @param mods_out Receives the modifier bits the mask requires
 * @return The key code that must be down, or 0 for a pure modifier binding
 */
static int32_t box_input_split(
    uint64_t mask,
    uint64_t *mods_out)
{
    int32_t key = (int32_t)(mask & BOX_KEY_MASK);
    uint64_t mods = mask & BOX_KEY_MOD_MASK;
    uint64_t bit = box_input_mod_bit(key);
    if (bit) {
        mods |= bit;
        key = 0;
    }

    mods_out[0] = mods;

    return key;
}

/** Resolve the key bindings of the frame, keeping the most specific ones per key.
 *
 * @param state The module state
 * @param in The Input singleton
 * @param held Modifier bits of the frame
 * @return The modifier families the resolved bindings claim
 */
static uint64_t box_input_resolve_keys(
    InputState *state,
    const Input *in,
    uint64_t held)
{
    int32_t best[BOX_KEY_COUNT];
    int32_t i, count = ecs_vec_count(&state->bound);
    box_binding_t *bound = ecs_vec_first_t(&state->bound, box_binding_t);
    for (i = 0; i < BOX_KEY_COUNT; i ++) {
        best[i] = -1;
    }

    for (i = 0; i < count; i ++) {
        uint64_t mods = 0;
        int32_t key = box_input_split(bound[i].mask, &mods);
        if (key <= 0 || key >= BOX_KEY_COUNT || !in->keys[key].state) {
            continue;
        }

        if (box_input_mods_match(mods, held)) {
            int32_t spec = box_input_mod_count(mods);
            best[key] = spec > best[key] ? spec : best[key];
        }
    }

    uint64_t claimed = 0;
    for (i = 0; i < count; i ++) {
        uint64_t mods = 0;
        int32_t key = box_input_split(bound[i].mask, &mods);
        if (key <= 0 || key >= BOX_KEY_COUNT || !in->keys[key].state) {
            continue;
        }

        if (box_input_mod_count(mods) != best[key] ||
            !box_input_mods_match(mods, held))
        {
            continue;
        }

        box_active_t *out = ecs_vec_append_t(NULL, &state->active, box_active_t);
        out->action = bound[i].action;
        out->source = bound[i].entity;
        out->mask = bound[i].mask;
        claimed |= box_input_families_of(mods);
    }

    return claimed;
}

/** Resolve the pure modifier bindings that no other binding of the frame claims.
 *
 * @param state The module state
 * @param held Modifier bits of the frame
 * @param claimed Modifier families the key bindings of the frame claim
 */
static void box_input_resolve_mods(
    InputState *state,
    uint64_t held,
    uint64_t claimed)
{
    int32_t i, count = ecs_vec_count(&state->bound);
    box_binding_t *bound = ecs_vec_first_t(&state->bound, box_binding_t);
    for (i = 0; i < count; i ++) {
        uint64_t mods = 0;
        int32_t key = box_input_split(bound[i].mask, &mods);
        if (key || !mods) {
            continue;
        }

        if (!box_input_mods_match(mods, held) ||
            (box_input_families_of(mods) & claimed))
        {
            continue;
        }

        box_active_t *out = ecs_vec_append_t(NULL, &state->active, box_active_t);
        out->action = bound[i].action;
        out->source = bound[i].entity;
        out->mask = bound[i].mask;
    }
}

/** Find the first binding of the frame that fires an action.
 *
 * @param state The module state
 * @param action The action entity
 * @return The binding that fired it, or NULL when none did
 */
static const box_active_t* box_input_active(
    const InputState *state,
    ecs_entity_t action)
{
    const box_active_t *result = NULL;
    int32_t i, count = ecs_vec_count(&state->active);
    const box_active_t *active = ecs_vec_first_t(&state->active, box_active_t);
    for (i = 0; i < count && !result; i ++) {
        if (active[i].action == action) {
            result = &active[i];
        }
    }

    return result;
}

/** Append one event to the list of the frame.
 *
 * @param events The list
 * @param action The action the event is about
 * @param phase What the action did
 * @param source Binding or emitter the event comes from
 * @param mask Key mask that fired it, 0 when it was emitted
 */
static void box_input_event(
    ActionEvents *events,
    ecs_entity_t action,
    ActionPhase phase,
    ecs_entity_t source,
    uint64_t mask)
{
    ActionEvent *out = ecs_vec_append_t(NULL, &events->events, ActionEvent);
    out->action = action;
    out->phase = phase;
    out->source = source;
    out->mask = mask;
}

/** Write the state of every action of the world and append its events.
 *
 * @param world The world
 * @param state The module state
 * @param events The event list of the frame
 */
static void box_input_write_actions(
    ecs_world_t *world,
    InputState *state,
    ActionEvents *events)
{
    ecs_iter_t it = ecs_query_iter(world, state->actions);
    while (ecs_query_next(&it)) {
        InputAction *action = ecs_field(&it, InputAction, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const box_active_t *active = box_input_active(state, it.entities[i]);
            bool held = active != NULL, was = action[i].held;
            ecs_entity_t source = active ? active->source : 0;
            uint64_t mask = active ? active->mask : 0;
            action[i].pressed = held && !was;
            action[i].released = !held && was;
            action[i].held = held;
            action[i].value = held ? 1.0f : 0.0f;
            if (action[i].pressed) {
                box_input_event(
                    events, it.entities[i], ActionPressed, source, mask);
            }

            if (held) {
                box_input_event(events, it.entities[i], ActionHeld, source, mask);
            }

            if (action[i].released) {
                box_input_event(
                    events, it.entities[i], ActionReleased, source, mask);
            }
        }
    }
}

void box_input_actions_init(
    ecs_world_t *world,
    InputState *state)
{
    ecs_vec_init_t(NULL, &state->bound, box_binding_t, 0);
    ecs_vec_init_t(NULL, &state->active, box_active_t, 0);
    state->bindings = ecs_query(world, {
        .terms = {{ .id = ecs_id(Binding), .inout = EcsIn }}
    });

    state->maps = ecs_query(world, {
        .terms = {{ .id = ecs_id(InputMap), .inout = EcsIn }}
    });

    state->actions = ecs_query(world, {
        .terms = {{ .id = ecs_id(InputAction) }}
    });
}

void box_input_queries_fini(
    InputState *state)
{
    if (state->bindings) {
        ecs_query_fini(state->bindings);
    }

    if (state->maps) {
        ecs_query_fini(state->maps);
    }

    if (state->actions) {
        ecs_query_fini(state->actions);
    }
}

void box_input_emit(
    ecs_world_t *world,
    ecs_entity_t action,
    ecs_entity_t source)
{
    InputAction *ptr = ecs_get_mut(world, action, InputAction);
    ecs_assert(ptr != NULL, ECS_INVALID_PARAMETER, "entity is not an action");
    ActionEvents *events = ecs_get_mut(
        world, ecs_id(ActionEvents), ActionEvents);
    ecs_assert(events != NULL, ECS_INVALID_OPERATION, "box.input is not imported");
    ptr->pressed = true;
    ptr->released = true;
    ptr->value = 1.0f;
    box_input_event(events, action, ActionPressed, source, 0);
    box_input_event(events, action, ActionReleased, source, 0);
    box_input_deliver(world, action, ActionPressed, source);
    box_input_deliver(world, action, ActionReleased, source);
}

int32_t box_input_events(
    ecs_world_t *world,
    const ActionEvent **events)
{
    ActionEvents *list = ecs_get_mut(
        world, ecs_id(ActionEvents), ActionEvents);
    ecs_assert(list != NULL, ECS_INVALID_OPERATION, "box.input is not imported");
    events[0] = ecs_vec_first_t(&list->events, ActionEvent);

    return ecs_vec_count(&list->events);
}

void BoxInputActions(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const Input *in = ecs_singleton_get(world, Input);
    InputState *state = box_input_state(world);
    ActionEvents *events = ecs_get_mut(
        world, ecs_id(ActionEvents), ActionEvents);
    ecs_vec_clear(&events->events);
    ecs_vec_clear(&state->active);
    box_input_collect(world, state);
    uint64_t held = box_input_held_mods(in);
    uint64_t claimed = box_input_resolve_keys(state, in, held);
    box_input_resolve_mods(state, held, claimed);
    box_input_write_actions(world, state, events);
}
