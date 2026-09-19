/**
 * @file input/actions.c
 * @brief The OnLoad system that resolves the bindings of the maps into actions.
 *
 * The maps of a world are layers: the system walks their priorities from the
 * highest down and a binding that declares consume takes its key and its action
 * away from every layer below it, which is what a UI map over the map of a game
 * needs. A binding that does not say so leaves the layers below alone, so
 * nothing is swallowed by accident and a handler decides the rest. A binding on
 * one of the any families resolves on whatever key of that family is down and
 * reports that key, so a title screen awaits one action.
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

int32_t box_input_mod_key(
    uint64_t bit)
{
    int32_t result = BoxKeyNone;
    int32_t i;
    for (i = 0; i < 8; i ++) {
        if (box_input_mod_bit(box_input_mod_keys[i]) == bit) {
            result = box_input_mod_keys[i];
        }
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
 * @param priority Layer the map resolves at
 * @param consume Whether the bindings of the map keep their key from lower maps
 */
static void box_input_map_pairs(
    ecs_world_t *world,
    InputState *state,
    ecs_entity_t map,
    int32_t priority,
    bool consume)
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
        out->priority = priority;
        out->consume = consume;
    }
}

/** Test whether the map a binding belongs to resolves it, and at which layer.
 *
 * A binding that hangs under no map belongs to no layer and resolves at the
 * priority every map starts at, which is zero.
 *
 * @param world The world
 * @param binding The binding entity
 * @param priority Receives the priority of the map the binding belongs to
 * @param consume Receives whether the map keeps its keys from the maps below
 * @return Whether the parent map is enabled, true when there is no map
 */
static bool box_input_map_enabled(
    ecs_world_t *world,
    ecs_entity_t binding,
    int32_t *priority,
    bool *consume)
{
    bool result = true;
    ecs_entity_t parent = ecs_get_parent(world, binding);
    const InputMap *map = parent ? ecs_get(world, parent, InputMap) : NULL;
    priority[0] = 0;
    consume[0] = false;
    if (map) {
        result = map->enabled;
        priority[0] = map->priority;
        consume[0] = map->consume;
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
        int32_t i, priority;
        bool consume;
        for (i = 0; i < it.count; i ++) {
            if (!binding[i].action ||
                !box_input_map_enabled(world, it.entities[i], &priority, &consume))
            {
                continue;
            }

            box_binding_t *out = ecs_vec_append_t(
                NULL, &state->bound, box_binding_t);
            out->mask = binding[i].mask;
            out->action = binding[i].action;
            out->entity = it.entities[i];
            out->priority = priority;
            out->consume = consume || binding[i].consume;
        }
    }

    it = ecs_query_iter(world, state->maps);
    while (ecs_query_next(&it)) {
        const InputMap *map = ecs_field(&it, InputMap, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (map[i].enabled) {
                box_input_map_pairs(world, state, it.entities[i],
                    map[i].priority, map[i].consume);
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

/** What one layer of maps resolves against and what the layers above it took */
typedef struct box_layer_t {
    int32_t priority;           /**< Priority of the maps of this layer */
    int32_t taken;              /**< Bindings that fired before this layer */
    uint64_t claimed;           /**< Modifier families the layers above claimed */
    bool consumed[BOX_KEY_COUNT]; /**< Keys the layers above this one took */
} box_layer_t;

/** Test whether a consuming binding of a layer above already took an action.
 *
 * @param state The module state
 * @param action The action entity
 * @param taken Bindings that fired before the layer being resolved
 * @return Whether the action is spoken for
 */
static bool box_input_taken(
    const InputState *state,
    ecs_entity_t action,
    int32_t taken)
{
    bool result = false;
    int32_t i;
    const box_active_t *active = ecs_vec_first_t(&state->active, box_active_t);
    for (i = 0; i < taken && !result; i ++) {
        result = active[i].consume && active[i].action == action;
    }

    return result;
}

/** Test whether a binding belongs to the layer being resolved and is still free.
 *
 * @param state The module state
 * @param layer The layer being resolved
 * @param binding The binding
 * @param key Key the binding needs, 0 for a pure modifier binding
 * @return Whether the binding takes part in this layer
 */
static bool box_input_in_layer(
    const InputState *state,
    const box_layer_t *layer,
    const box_binding_t *binding,
    int32_t key)
{
    bool result = binding->priority == layer->priority;
    if (result && key > 0 && key < BOX_KEY_COUNT && layer->consumed[key]) {
        result = false;
    }

    if (result && box_input_taken(state, binding->action, layer->taken)) {
        result = false;
    }

    return result;
}

/** Resolve the key bindings of one layer, keeping the most specific ones per key.
 *
 * @param state The module state
 * @param layer The layer being resolved
 * @param in The Input singleton
 * @param held Modifier bits of the frame
 * @return The modifier families the resolved bindings claim
 */
static uint64_t box_input_resolve_keys(
    InputState *state,
    const box_layer_t *layer,
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

        if (!box_input_in_layer(state, layer, &bound[i], key)) {
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

        if (!box_input_in_layer(state, layer, &bound[i], key)) {
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
        out->priority = bound[i].priority;
        out->consume = bound[i].consume;
        claimed |= box_input_families_of(mods);
    }

    return claimed;
}

/** Resolve the pure modifier bindings of one layer that nothing else claims.
 *
 * @param state The module state
 * @param layer The layer being resolved
 * @param held Modifier bits of the frame
 * @param claimed Modifier families the key bindings claim
 * @return The modifier families the resolved bindings claim
 */
static uint64_t box_input_resolve_mods(
    InputState *state,
    const box_layer_t *layer,
    uint64_t held,
    uint64_t claimed)
{
    uint64_t result = 0;
    int32_t i, count = ecs_vec_count(&state->bound);
    box_binding_t *bound = ecs_vec_first_t(&state->bound, box_binding_t);
    for (i = 0; i < count; i ++) {
        uint64_t mods = 0;
        int32_t key = box_input_split(bound[i].mask, &mods);
        if (key || !mods) {
            continue;
        }

        if (!box_input_in_layer(state, layer, &bound[i], key)) {
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
        out->priority = bound[i].priority;
        out->consume = bound[i].consume;
        result |= box_input_families_of(mods);
    }

    return result;
}

/** Slot range one of the any families of the keys scope covers.
 *
 * @param code The key code of the binding
 * @param first Receives the first slot of the range
 * @param last Receives the slot after the range
 * @return Whether the code is one of the any families
 */
static bool box_input_any_range(
    int32_t code,
    int32_t *first,
    int32_t *last)
{
    bool result = true;
    first[0] = 1;
    last[0] = BOX_KEY_COUNT;
    switch (code) {
    case BoxKeyAny: break;
    case BoxKeyAnyKey: last[0] = BoxKeyMouseLeft; break;
    case BoxKeyAnyMouse: first[0] = BoxKeyMouseLeft; break;
    default: result = false; break;
    }

    return result;
}

/** Find the key of a range that fires an any binding of a layer.
 *
 * A key that went down this frame wins over one that is only still held, so the
 * event of the binding names the key a screen is waiting for, and a key a layer
 * above took is left out the way it is for a binding that names it.
 *
 * @param in The Input singleton
 * @param layer The layer being resolved
 * @param first First slot of the range
 * @param last Slot after the range
 * @return The key code that fires the binding, or 0 when none does
 */
static int32_t box_input_any_key(
    const Input *in,
    const box_layer_t *layer,
    int32_t first,
    int32_t last)
{
    int32_t i, result = 0;
    for (i = first; i < last; i ++) {
        if (!in->keys[i].state || layer->consumed[i]) {
            continue;
        }

        if (in->keys[i].pressed) {
            result = i;
            break;
        }

        result = result ? result : i;
    }

    return result;
}

/** Resolve the bindings of one layer that any key of a family fires.
 *
 * @param state The module state
 * @param layer The layer being resolved
 * @param in The Input singleton
 * @param held Modifier bits of the frame
 * @return The modifier families the resolved bindings claim
 */
static uint64_t box_input_resolve_any(
    InputState *state,
    const box_layer_t *layer,
    const Input *in,
    uint64_t held)
{
    uint64_t result = 0;
    int32_t i, count = ecs_vec_count(&state->bound);
    box_binding_t *bound = ecs_vec_first_t(&state->bound, box_binding_t);
    for (i = 0; i < count; i ++) {
        uint64_t mods = 0;
        int32_t first, last, fired;
        int32_t key = box_input_split(bound[i].mask, &mods);
        if (!box_input_any_range(key, &first, &last)) {
            continue;
        }

        fired = box_input_any_key(in, layer, first, last);
        if (!fired || !box_input_in_layer(state, layer, &bound[i], fired)) {
            continue;
        }

        if (!box_input_mods_match(mods, held)) {
            continue;
        }

        box_active_t *out = ecs_vec_append_t(NULL, &state->active, box_active_t);
        out->action = bound[i].action;
        out->source = bound[i].entity;
        out->mask = (uint64_t)fired | mods;
        out->priority = bound[i].priority;
        out->consume = bound[i].consume;
        result |= box_input_families_of(mods);
    }

    return result;
}

/** Find the highest priority of the bindings of the frame below a priority.
 *
 * @param state The module state
 * @param below Priority the layer above resolved at
 * @param out Receives the priority of the next layer
 * @return Whether a layer is left
 */
static bool box_input_next_layer(
    const InputState *state,
    int64_t below,
    int32_t *out)
{
    bool result = false;
    int32_t i, count = ecs_vec_count(&state->bound);
    const box_binding_t *bound = ecs_vec_first_t(&state->bound, box_binding_t);
    for (i = 0; i < count; i ++) {
        int64_t priority = bound[i].priority;
        if (priority < below && (!result || priority > out[0])) {
            out[0] = bound[i].priority;
            result = true;
        }
    }

    return result;
}

/** Take the keys of the consuming bindings of a layer from the layers below it.
 *
 * @param state The module state
 * @param layer The layer that resolved, whose taken count is moved on
 */
static void box_input_take(
    InputState *state,
    box_layer_t *layer)
{
    int32_t i, count = ecs_vec_count(&state->active);
    const box_active_t *active = ecs_vec_first_t(&state->active, box_active_t);
    for (i = layer->taken; i < count; i ++) {
        uint64_t mods = 0;
        int32_t key = box_input_split(active[i].mask, &mods);
        if (active[i].consume && key > 0 && key < BOX_KEY_COUNT) {
            layer->consumed[key] = true;
        }
    }

    layer->taken = count;
}

/** Resolve every layer of maps, from the highest priority down.
 *
 * @param state The module state
 * @param in The Input singleton
 * @param held Modifier bits of the frame
 */
static void box_input_resolve(
    InputState *state,
    const Input *in,
    uint64_t held)
{
    box_layer_t layer;
    int64_t below = (int64_t)INT32_MAX + 1;
    ecs_os_zeromem(&layer);
    while (box_input_next_layer(state, below, &layer.priority)) {
        uint64_t claimed = box_input_resolve_keys(state, &layer, in, held);
        claimed |= layer.claimed;
        claimed |= box_input_resolve_any(state, &layer, in, held);
        layer.claimed = claimed |
            box_input_resolve_mods(state, &layer, held, claimed);
        box_input_take(state, &layer);
        below = layer.priority;
    }
}

/** Add the actions that code holds to the active list and age their frames.
 *
 * @param state The module state
 */
static void box_input_resolve_holds(
    InputState *state)
{
    int32_t i = 0;
    while (i < ecs_vec_count(&state->holds)) {
        box_hold_t *hold = ecs_vec_get_t(&state->holds, box_hold_t, i);
        box_active_t *out = ecs_vec_append_t(NULL, &state->active, box_active_t);
        out->action = hold->action;
        out->source = hold->source;
        out->mask = 0;
        if (hold->frames > 0 && !--hold->frames) {
            ecs_vec_remove_ordered_t(&state->holds, box_hold_t, i);
        } else {
            i ++;
        }
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
 * @param priority Priority of the map the binding belongs to
 */
static void box_input_event(
    ActionEvents *events,
    ecs_entity_t action,
    ActionPhase phase,
    ecs_entity_t source,
    uint64_t mask,
    int32_t priority)
{
    ActionEvent *out = ecs_vec_append_t(NULL, &events->events, ActionEvent);
    out->action = action;
    out->phase = phase;
    out->source = source;
    out->mask = mask;
    out->priority = priority;
}

/** Find the drag an action is tracked by.
 *
 * @param state The module state
 * @param action The action entity
 * @return Index of the drag, or -1 when the action is not held
 */
static int32_t box_input_drag_find(
    const InputState *state,
    ecs_entity_t action)
{
    int32_t i, count = ecs_vec_count(&state->drags);
    int32_t result = -1;
    const box_drag_t *drags = ecs_vec_first_t(&state->drags, box_drag_t);
    for (i = 0; i < count && result < 0; i ++) {
        if (drags[i].action == action) {
            result = i;
        }
    }

    return result;
}

/** Follow the drag of one action over the frame its state was written in.
 *
 * A press anchors the drag at the mouse of that frame, a hold arms it once the
 * cursor left the threshold around the anchor and anything else drops it.
 *
 * @param state The module state
 * @param in The Input singleton
 * @param action The action entity
 * @param act State the action was just given
 */
static void box_input_track_drag(
    InputState *state,
    const Input *in,
    ecs_entity_t action,
    const InputAction *act)
{
    int32_t index = box_input_drag_find(state, action);
    box_drag_t *drag = index >= 0
        ? ecs_vec_get_t(&state->drags, box_drag_t, index)
        : NULL;
    if (act->pressed) {
        drag = drag ? drag : ecs_vec_append_t(NULL, &state->drags, box_drag_t);
        drag->action = action;
        drag->start = in->mouse;
        drag->dragging = false;
    } else if (!act->held) {
        if (index >= 0) {
            ecs_vec_remove_ordered_t(&state->drags, box_drag_t, index);
        }
    } else if (drag && !drag->dragging) {
        float dx = in->mouse.window.x - drag->start.window.x;
        float dy = in->mouse.window.y - drag->start.window.y;
        float limit = BOX_INPUT_DRAG_THRESHOLD * BOX_INPUT_DRAG_THRESHOLD;
        drag->dragging = (dx * dx + dy * dy) > limit;
    }
}

/** Write the state of every action of the world and append its events.
 *
 * @param world The world
 * @param state The module state
 * @param in The Input singleton
 * @param events The event list of the frame
 */
static void box_input_write_actions(
    ecs_world_t *world,
    InputState *state,
    const Input *in,
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
            int32_t priority = active ? active->priority : 0;
            action[i].pressed = held && !was;
            action[i].released = !held && was;
            action[i].held = held;
            action[i].value = held ? 1.0f : 0.0f;
            box_input_track_drag(state, in, it.entities[i], &action[i]);
            if (action[i].pressed) {
                box_input_event(events, it.entities[i], ActionPressed,
                    source, mask, priority);
            }

            if (held) {
                box_input_event(events, it.entities[i], ActionHeld,
                    source, mask, priority);
            }

            if (action[i].released) {
                box_input_event(events, it.entities[i], ActionReleased,
                    source, mask, priority);
            }

            if (box_input_drag_of(state, it.entities[i])) {
                box_input_event(events, it.entities[i], ActionDragged,
                    source, mask, priority);
            }
        }
    }
}

/** Give back the held state of the actions a consume of the last frame cleared.
 *
 * A consume only takes its frame, so an action whose key is still down carries
 * on as held instead of pressing again the frame after.
 *
 * @param world The world
 * @param state The module state
 */
static void box_input_restore(
    ecs_world_t *world,
    InputState *state)
{
    int32_t i, count = ecs_vec_count(&state->rejected);
    for (i = 0; i < count; i ++) {
        const box_reject_t *reject = ecs_vec_get_t(
            &state->rejected, box_reject_t, i);
        InputAction *action = ecs_is_alive(world, reject->action)
            ? ecs_get_mut(world, reject->action, InputAction)
            : NULL;
        if (action) {
            action->held = reject->held;
        }
    }

    ecs_vec_clear(&state->rejected);
}

void box_input_actions_init(
    ecs_world_t *world,
    InputState *state)
{
    ecs_vec_init_t(NULL, &state->bound, box_binding_t, 0);
    ecs_vec_init_t(NULL, &state->active, box_active_t, 0);
    ecs_vec_init_t(NULL, &state->drags, box_drag_t, 0);
    ecs_vec_init_t(NULL, &state->rejected, box_reject_t, 0);
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

int32_t box_input_bindings(
    ecs_world_t *world,
    ecs_entity_t action,
    uint64_t *out,
    int32_t max)
{
    InputState *state = box_input_state(world);
    box_input_collect(world, state);

    int32_t i, count = 0, bound = ecs_vec_count(&state->bound);
    for (i = 0; i < bound && count < max; i ++) {
        const box_binding_t *binding = ecs_vec_get_t(
            &state->bound, box_binding_t, i);
        if (binding->action == action) {
            out[count ++] = binding->mask;
        }
    }

    return count;
}

void box_input_hold(
    ecs_world_t *world,
    ecs_entity_t action,
    ecs_entity_t source,
    int32_t frames)
{
    ecs_assert(ecs_get(world, action, InputAction) != NULL,
        ECS_INVALID_PARAMETER, "entity is not an action");
    InputState *state = box_input_state(world);
    box_input_release(world, action);
    box_hold_t *hold = ecs_vec_append_t(NULL, &state->holds, box_hold_t);
    hold->action = action;
    hold->source = source;
    hold->frames = frames;
}

bool box_input_release(
    ecs_world_t *world,
    ecs_entity_t action)
{
    InputState *state = box_input_state(world);
    bool result = false;
    int32_t i, count = ecs_vec_count(&state->holds);
    for (i = 0; i < count && !result; i ++) {
        if (ecs_vec_get_t(&state->holds, box_hold_t, i)->action == action) {
            ecs_vec_remove_ordered_t(&state->holds, box_hold_t, i);
            result = true;
        }
    }

    return result;
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
    ActionEvent raise = { .action = action, .source = source };
    bool consumed = false;
    box_input_event(events, action, ActionPressed, source, 0, 0);
    box_input_event(events, action, ActionReleased, source, 0, 0);
    raise.phase = ActionPressed;
    box_input_deliver(world, &raise, &consumed);
    raise.phase = ActionReleased;
    box_input_deliver(world, &raise, &consumed);
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

const box_drag_t* box_input_drag_of(
    const InputState *state,
    ecs_entity_t action)
{
    int32_t index = box_input_drag_find(state, action);
    const box_drag_t *result = index >= 0
        ? ecs_vec_get_t(&state->drags, box_drag_t, index)
        : NULL;

    return result && result->dragging ? result : NULL;
}

/** Drop the events of an action from the list of the frame.
 *
 * @param events The event list of the frame
 * @param action The action a handler took
 */
static void box_input_drop_events(
    ActionEvents *events,
    ecs_entity_t action)
{
    int32_t i = 0;
    while (i < ecs_vec_count(&events->events)) {
        if (ecs_vec_get_t(&events->events, ActionEvent, i)->action == action) {
            ecs_vec_remove_ordered_t(&events->events, ActionEvent, i);
        } else {
            i ++;
        }
    }
}

/** Drop the bindings of an action from the resolution of the frame.
 *
 * @param state The module state
 * @param action The action a handler took
 */
static void box_input_drop_active(
    InputState *state,
    ecs_entity_t action)
{
    int32_t i = 0;
    while (i < ecs_vec_count(&state->active)) {
        if (ecs_vec_get_t(&state->active, box_active_t, i)->action == action) {
            ecs_vec_remove_ordered_t(&state->active, box_active_t, i);
        } else {
            i ++;
        }
    }
}

/** Actions one key taken by a handler covers in the maps below it, in one frame */
#define BOX_INPUT_BELOW_MAX (16)

void box_input_stop_below(
    ecs_world_t *world,
    uint64_t mask,
    int32_t priority)
{
    ecs_entity_t below[BOX_INPUT_BELOW_MAX];
    const ActionEvent *events;
    uint64_t key = mask & BOX_KEY_MASK;
    int32_t i, count = box_input_events(world, &events), found = 0;
    for (i = 0; key && i < count && found < BOX_INPUT_BELOW_MAX; i ++) {
        if (events[i].priority < priority &&
            (events[i].mask & BOX_KEY_MASK) == key)
        {
            below[found ++] = events[i].action;
        }
    }

    for (i = 0; i < found; i ++) {
        box_input_consume(world, below[i]);
    }
}

/** Find the key and the layer an action fired from this frame.
 *
 * @param events The event list of the frame
 * @param action The action entity
 * @param mask Receives the key mask that fired it, 0 when it did not fire
 * @param priority Receives the priority of the map the binding belongs to
 */
static void box_input_event_of(
    const ActionEvents *events,
    ecs_entity_t action,
    uint64_t *mask,
    int32_t *priority)
{
    int32_t i, count = ecs_vec_count(&events->events);
    const ActionEvent *list = ecs_vec_first_t(&events->events, ActionEvent);
    mask[0] = 0;
    priority[0] = 0;
    for (i = 0; i < count; i ++) {
        if (list[i].action == action) {
            mask[0] = list[i].mask;
            priority[0] = list[i].priority;
            break;
        }
    }
}

bool box_input_consume(
    ecs_world_t *world,
    ecs_entity_t action)
{
    InputState *state = box_input_state(world);
    InputAction *act = action && ecs_is_alive(world, action)
        ? ecs_get_mut(world, action, InputAction)
        : NULL;
    ActionEvents *events = ecs_get_mut(
        world, ecs_id(ActionEvents), ActionEvents);
    bool result = act && (act->pressed || act->held || act->released);
    box_reject_t *reject;
    uint64_t mask;
    int32_t index, priority;
    if (!result) {
        goto done;
    }

    box_input_event_of(events, action, &mask, &priority);
    reject = ecs_vec_append_t(NULL, &state->rejected, box_reject_t);
    reject->action = action;
    reject->held = act->held;
    act->pressed = false;
    act->held = false;
    act->released = false;
    act->value = 0;
    index = box_input_drag_find(state, action);
    if (index >= 0) {
        ecs_vec_remove_ordered_t(&state->drags, box_drag_t, index);
    }

    box_input_drop_active(state, action);
    box_input_drop_events(events, action);
    box_input_forget(world, action);
    box_input_stop_below(world, mask, priority);
done:
    return result;
}

/** Order the events of the frame by the priority of the map they came from.
 *
 * The highest map is delivered first, so a handler that stops a raise is the one
 * that runs before the maps below it hear anything, and an action of a lower map
 * is still on the list to be taken away.
 *
 * @param events The event list of the frame
 */
static void box_input_sort_events(
    ActionEvents *events)
{
    int32_t i, j, count = ecs_vec_count(&events->events);
    ActionEvent *list = ecs_vec_first_t(&events->events, ActionEvent);
    for (i = 1; i < count; i ++) {
        ActionEvent event = list[i];
        for (j = i - 1; j >= 0 && list[j].priority < event.priority; j --) {
            list[j + 1] = list[j];
        }

        list[j + 1] = event;
    }
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
    box_input_restore(world, state);
    box_input_collect(world, state);
    box_input_resolve(state, in, box_input_held_mods(in));
    box_input_resolve_holds(state);
    box_input_write_actions(world, state, in, events);
    box_input_sort_events(events);
}
