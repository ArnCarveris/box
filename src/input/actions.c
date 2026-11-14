/**
 * @file input/actions.c
 * @brief Resolves bindings, synthetic holds and triggers into ActionState and raises the events.
 */

#include "input.h"

ECS_COMPONENT_DECLARE(InputActions);

/** A binding or hold keeps the action down */
#define BOX_INPUT_BOUND_DOWN (1u)

/** A binding or hold went down this frame */
#define BOX_INPUT_BOUND_PRESSED (2u)

/** What an action event reports, the order in which a frame raises them */
typedef enum box_input_phase_t {
    BoxInputPressed,   /**< The action went down */
    BoxInputHeld,      /**< The action is down */
    BoxInputDragged,   /**< The cursor moved while the action is down */
    BoxInputReleased,  /**< The action came up */
    BoxInputPhaseCount /**< Number of phases */
} box_input_phase_t;

/** Names of the on.* functions, indexed by phase */
static const char *box_input_phase_names[BoxInputPhaseCount] = {
    "pressed", "held", "dragged", "released"
};

/** A script task waiting for an action event */
typedef struct box_input_waiter_t {
    ecs_script_future_t *future; /**< Future the event resolves */
    ecs_entity_t target;         /**< Action, or a parent of the actions it waits for */
    box_input_phase_t phase;     /**< Phase that resolves the wait */
} box_input_waiter_t;

/** One action event of the frame */
typedef struct box_input_action_event_t {
    box_input_phase_t phase; /**< What the event reports */
    ActionEvent event;       /**< The value the waiters resolve with */
} box_input_action_event_t;

/** Get the action state of a world, NULL while it is torn down.
 *
 * @param world The world or a stage
 * @return The action state
 */
static InputActions* box_input_actions(
    ecs_world_t *world)
{
    ecs_world_t *real = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    InputActions *result = NULL;
    if (ecs_id(InputActions) && !ecs_is_fini(real)) {
        result = ecs_singleton_get_mut(real, InputActions);
    }

    return result;
}

/** Release the waiters, events and scratch map of the action state.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_input_actions_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InputActions *st = ptr;
    int32_t i, w;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_input_waiter_t *waiters = ecs_vec_first_t(&st[i].waiters, box_input_waiter_t);
        for (w = 0; w < ecs_vec_count(&st[i].waiters); w ++) {
            ecs_script_future_release(waiters[w].future);
        }

        ecs_vec_fini_t(NULL, &st[i].waiters, box_input_waiter_t);
        ecs_vec_fini_t(NULL, &st[i].events, box_input_action_event_t);
        ecs_vec_fini_t(NULL, &st[i].cursors, box_input_cursor_t);
        ecs_map_fini(&st[i].bound);
    }
}

/** Find the hold of an action by a source, adding it when asked to.
 *
 * @param dev The device state
 * @param source The element of a trigger, 0 for a synthetic hold
 * @param action The action entity
 * @param add Whether a missing hold is added
 * @return The hold, NULL when there is none
 */
static box_input_hold_t* box_input_hold_find(
    InputDevice *dev,
    ecs_entity_t source,
    ecs_entity_t action,
    bool add)
{
    box_input_hold_t *holds = ecs_vec_first_t(&dev->holds, box_input_hold_t);
    box_input_hold_t *result = NULL;
    int32_t i, count = ecs_vec_count(&dev->holds);
    for (i = 0; i < count && !result; i ++) {
        if (holds[i].action == action && holds[i].source == source) {
            result = &holds[i];
        }
    }

    if (!result && add && action) {
        result = ecs_vec_append_t(NULL, &dev->holds, box_input_hold_t);
        ecs_os_zeromem(result);
        result->action = action;
        result->source = source;
    }

    return result;
}

void box_input_hold(
    InputDevice *dev,
    ecs_entity_t action,
    bool down)
{
    box_input_hold_t *hold = box_input_hold_find(dev, 0, action, down);
    if (hold) {
        hold->pressed = hold->pressed || (down && !hold->down);
        hold->down = down;
    }
}

void box_input_hold_release_all(
    InputDevice *dev)
{
    box_input_hold_t *holds = ecs_vec_first_t(&dev->holds, box_input_hold_t);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&dev->holds); i ++) {
        holds[i].down = false;
        holds[i].tapped = false;
        holds[i].taps = 0;
    }
}

void box_input_trigger_tap(
    InputDevice *dev,
    ecs_entity_t source,
    ecs_entity_t action,
    const vec2 cursor)
{
    box_input_hold_t *hold = box_input_hold_find(dev, source, action, true);
    if (hold) {
        hold->taps ++;
        hold->cursor[0] = cursor[0];
        hold->cursor[1] = cursor[1];
    }
}

void box_input_trigger_hold(
    InputDevice *dev,
    ecs_entity_t source,
    ecs_entity_t action,
    bool down,
    const vec2 cursor)
{
    box_input_hold_t *hold = box_input_hold_find(dev, source, action, down);
    if (hold) {
        hold->pressed = hold->pressed || (down && !hold->down);
        hold->down = down;
        hold->cursor[0] = cursor[0];
        hold->cursor[1] = cursor[1];
    }
}

void box_input_trigger_cursor(
    InputDevice *dev,
    ecs_entity_t source,
    const vec2 cursor)
{
    box_input_hold_t *holds = ecs_vec_first_t(&dev->holds, box_input_hold_t);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&dev->holds); i ++) {
        if (source && holds[i].source == source && holds[i].down) {
            holds[i].cursor[0] = cursor[0];
            holds[i].cursor[1] = cursor[1];
        }
    }
}

/** Number of modifier keys, KeyShiftLeft to KeySuperRight */
#define BOX_INPUT_MOD_KEYS (KeySuperRight - KeyShiftLeft + 1)

/** Bits of both keys of the shift modifier in a modifier key mask */
#define BOX_INPUT_MOD_SHIFT (0x03u)

/** Bits of both keys of the control modifier in a modifier key mask */
#define BOX_INPUT_MOD_CTRL (0x0Cu)

/** Bits of both keys of the alt modifier in a modifier key mask */
#define BOX_INPUT_MOD_ALT (0x30u)

/** Bits of both keys of the super modifier in a modifier key mask */
#define BOX_INPUT_MOD_META (0xC0u)

/** Modifier keys that are the key of a binding, the modifier keys held and the most specific
 * chord of every key */
typedef struct box_input_chords_t {
    uint8_t bound;              /**< Mask of the modifier keys that are the key of a binding */
    uint8_t live;               /**< Mask of the modifier keys held this frame */
    const uint8_t *press;       /**< Mask of the modifier keys held when each key went down */
    int8_t best[BOX_KEY_COUNT]; /**< Most modifiers a matching binding of the key needs */
} box_input_chords_t;

/** Get the modifier keys that are held.
 *
 * @param in The Input singleton
 * @return Mask with bit i set when KeyShiftLeft + i is held
 */
static uint8_t box_input_mod_mask(
    const Input *in)
{
    uint8_t result = 0;
    int32_t i;
    for (i = 0; i < BOX_INPUT_MOD_KEYS; i ++) {
        result |= (uint8_t)(in->keys[KeyShiftLeft + i].down ? 1u << i : 0);
    }

    return result;
}

/** Get the modifiers a binding requires as a modifier key mask.
 *
 * @param binding The binding
 * @return Mask with both keys of every required modifier set
 */
static uint8_t box_input_binding_required(
    const Binding *binding)
{
    return (uint8_t)((binding->shift ? BOX_INPUT_MOD_SHIFT : 0) |
        (binding->ctrl ? BOX_INPUT_MOD_CTRL : 0) |
        (binding->alt ? BOX_INPUT_MOD_ALT : 0) |
        (binding->meta ? BOX_INPUT_MOD_META : 0));
}

/** Check whether one key of every required modifier is held.
 *
 * @param required Mask of the required modifiers
 * @param held Mask of the held modifier keys
 * @return Whether every required modifier is held
 */
static bool box_input_mods_held(
    uint8_t required,
    uint8_t held)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < BOX_INPUT_MOD_KEYS; i += 2) {
        uint8_t mod = (uint8_t)(3u << i);
        result = result && (!(required & mod) || (held & mod));
    }

    return result;
}

/** Get both keys of the modifier a key is, so a binding of ShiftLeft ignores ShiftRight.
 *
 * @param key The key
 * @return Mask of the keys of its modifier, 0 when the key is not a modifier
 */
static uint8_t box_input_own_mods(
    Key key)
{
    uint8_t result = 0;
    if (key >= KeyShiftLeft && key <= KeySuperRight) {
        result = (uint8_t)(3u << ((key - KeyShiftLeft) / 2 * 2));
    }

    return result;
}

/** Count the modifiers a binding requires.
 *
 * @param binding The binding
 * @return Number of required modifiers, 0 to 4
 */
static int8_t box_input_binding_mods(
    const Binding *binding)
{
    return (int8_t)(binding->ctrl + binding->shift + binding->alt + binding->meta);
}

/** Check whether a binding matches its key and the modifier keys held when the key went down:
 * every required modifier was held, and no other modifier key was held that is not the key of
 * a binding, unless the binding ignores modifiers.
 *
 * @param in The Input singleton
 * @param chords The bound modifier keys and the modifier keys held when each key went down
 * @param binding The binding
 * @return Whether the binding matches, ignoring more specific bindings of its key
 */
static bool box_input_binding_match(
    const Input *in,
    const box_input_chords_t *chords,
    const Binding *binding)
{
    bool result = false;
    const KeyState *key;
    uint8_t required, held, extra;
    if (binding->key <= KeyNone || binding->key >= BOX_KEY_COUNT) {
        goto done;
    }

    key = &in->keys[binding->key];
    required = box_input_binding_required(binding);
    held = chords->press[binding->key];
    extra = (uint8_t)(held & ~required & ~chords->bound & ~box_input_own_mods(binding->key));
    result = (key->down || key->pressed) && box_input_mods_held(required, held) &&
        (binding->ignore_modifiers || !extra);

done:
    return result;
}

/** Compute what a binding contributes to its action this frame.
 *
 * A binding applies while it is the most specific match of its key and its required modifiers
 * are still held, so releasing a modifier first ends the chord without handing the key to a
 * less specific binding.
 *
 * @param in The Input singleton
 * @param chords The bound modifier keys, the held modifier keys and the most specific chords
 * @param binding The binding
 * @return BOX_INPUT_BOUND_* bits
 */
static uint64_t box_input_binding_bits(
    const Input *in,
    const box_input_chords_t *chords,
    const Binding *binding)
{
    uint64_t result = 0;
    if (box_input_binding_match(in, chords, binding) &&
        box_input_binding_mods(binding) == chords->best[binding->key] &&
        box_input_mods_held(box_input_binding_required(binding), chords->live))
    {
        const KeyState *key = &in->keys[binding->key];
        result = (key->down ? BOX_INPUT_BOUND_DOWN : 0) |
            (key->pressed ? BOX_INPUT_BOUND_PRESSED : 0);
    }

    return result;
}

/** Record the modifier keys held by the keys that went down this frame, then find the modifier
 * keys that are the key of a binding and, for every key, the most modifiers a matching binding
 * of it requires.
 *
 * @param world The world
 * @param st The action state
 * @param in The Input singleton
 * @param chords Receives the bound and held modifier keys and the most specific chords
 */
static void box_input_chords(
    ecs_world_t *world,
    InputActions *st,
    const Input *in,
    box_input_chords_t *chords)
{
    ecs_iter_t it = ecs_query_iter(world, st->bindings);
    int32_t i;
    ecs_os_zeromem(chords);
    chords->live = box_input_mod_mask(in);
    chords->press = st->press_mods;
    for (i = 0; i < BOX_KEY_COUNT; i ++) {
        if (in->keys[i].pressed) {
            st->press_mods[i] = chords->live;
        }
    }

    while (ecs_query_next(&it)) {
        const Binding *b = ecs_field(&it, Binding, 0);
        for (i = 0; i < it.count; i ++) {
            if (b[i].key >= KeyShiftLeft && b[i].key <= KeySuperRight) {
                chords->bound |= (uint8_t)(1u << (b[i].key - KeyShiftLeft));
            }
        }
    }

    it = ecs_query_iter(world, st->bindings);
    while (ecs_query_next(&it)) {
        const Binding *b = ecs_field(&it, Binding, 0);
        for (i = 0; i < it.count; i ++) {
            int8_t mods = box_input_binding_mods(&b[i]);
            if (box_input_binding_match(in, chords, &b[i]) && mods > chords->best[b[i].key]) {
                chords->best[b[i].key] = mods;
            }
        }
    }
}

/** Get the action a binding drives.
 *
 * @param world The world
 * @param entity The binding entity
 * @param binding The binding
 * @return The action entity, 0 when there is none
 */
static ecs_entity_t box_input_binding_action(
    ecs_world_t *world,
    ecs_entity_t entity,
    const Binding *binding)
{
    ecs_entity_t result = binding->action;
    if (!result) {
        result = ecs_has(world, entity, Action) ? entity : ecs_get_parent(world, entity);
    }

    return result;
}

void box_input_action_keys(
    ecs_world_t *world,
    ecs_entity_t action,
    bool *keys)
{
    InputActions *st = action ? box_input_actions(world) : NULL;
    ecs_iter_t it;
    int32_t i;
    if (!st) {
        goto done;
    }

    it = ecs_query_iter(world, st->bindings);
    while (ecs_query_next(&it)) {
        const Binding *b = ecs_field(&it, Binding, 0);
        for (i = 0; i < it.count; i ++) {
            if (box_input_binding_action(world, it.entities[i], &b[i]) != action ||
                b[i].key <= KeyNone || b[i].key >= BOX_KEY_COUNT)
            {
                continue;
            }

            keys[b[i].key] = true;
            keys[KeyCtrlLeft] = keys[KeyCtrlRight] = keys[KeyCtrlLeft] || b[i].ctrl;
            keys[KeyShiftLeft] = keys[KeyShiftRight] = keys[KeyShiftLeft] || b[i].shift;
            keys[KeyAltLeft] = keys[KeyAltRight] = keys[KeyAltLeft] || b[i].alt;
            keys[KeySuperLeft] = keys[KeySuperRight] = keys[KeySuperLeft] || b[i].meta;
        }
    }

done:
    return;
}

/** Zero the bits of the last frame, emptying the map once an action in it was not bound.
 *
 * Keeping the entries of a steady set of bound actions avoids allocating every frame.
 *
 * @param bound Action entity to BOX_INPUT_BOUND_* bits
 */
static void box_input_bound_reset(
    ecs_map_t *bound)
{
    ecs_map_iter_t mit = ecs_map_iter(bound);
    bool stale = false;
    while (ecs_map_next(&mit)) {
        stale = stale || ecs_map_value(&mit) == 0;
        ecs_map_value(&mit) = 0;
    }

    if (stale) {
        ecs_map_clear(bound);
    }
}

/** Collect the bits of every binding, synthetic hold and trigger per action, and the cursors
 * of the triggers.
 *
 * @param world The world
 * @param st The action state
 * @param dev The device state
 * @param in The Input singleton
 */
static void box_input_bind(
    ecs_world_t *world,
    InputActions *st,
    InputDevice *dev,
    const Input *in)
{
    box_input_hold_t *holds = ecs_vec_first_t(&dev->holds, box_input_hold_t);
    int32_t i, kept = 0, count = ecs_vec_count(&dev->holds);
    box_input_chords_t chords;
    ecs_iter_t it;
    box_input_chords(world, st, in, &chords);
    box_input_bound_reset(&st->bound);
    it = ecs_query_iter(world, st->bindings);
    while (ecs_query_next(&it)) {
        const Binding *b = ecs_field(&it, Binding, 0);
        for (i = 0; i < it.count; i ++) {
            uint64_t bits = box_input_key_blocked(dev, b[i].key) ?
                0 : box_input_binding_bits(in, &chords, &b[i]);
            ecs_entity_t action = bits ? box_input_binding_action(world, it.entities[i], &b[i]) : 0;
            if (action) {
                ecs_map_ensure(&st->bound, action)[0] |= bits;
            }
        }
    }

    ecs_vec_clear(&st->cursors);
    for (i = 0; i < count; i ++) {
        uint64_t bits;
        if (holds[i].tapped) {
            holds[i].tapped = false;
            holds[i].taps --;
        } else if (holds[i].taps > 0) {
            holds[i].tapped = true;
        }

        bits = (holds[i].down || holds[i].tapped ? BOX_INPUT_BOUND_DOWN : 0) |
            (holds[i].pressed ? BOX_INPUT_BOUND_PRESSED : 0);
        if (bits) {
            ecs_map_ensure(&st->bound, holds[i].action)[0] |= bits;
        }

        if (holds[i].source) {
            box_input_cursor_t *c = ecs_vec_append_t(NULL, &st->cursors, box_input_cursor_t);
            c->action = holds[i].action;
            c->cursor[0] = holds[i].cursor[0];
            c->cursor[1] = holds[i].cursor[1];
        }

        holds[i].pressed = false;
        if (holds[i].down || holds[i].taps > 0) {
            holds[kept ++] = holds[i];
        }
    }

    ecs_vec_set_count_t(NULL, &dev->holds, box_input_hold_t, kept);
}

/** Append an action event to the events of the frame.
 *
 * @param st The action state
 * @param phase What the event reports
 * @param action The action entity
 * @param state The state of the action this frame
 * @param cursor The cursor the event reports
 */
static void box_input_action_raise(
    InputActions *st,
    box_input_phase_t phase,
    ecs_entity_t action,
    const ActionState *state,
    const float *cursor)
{
    box_input_action_event_t *ev = ecs_vec_append_t(
        NULL, &st->events, box_input_action_event_t);
    ev->phase = phase;
    ev->event = (ActionEvent){
        .action = action,
        .screen_x = cursor[0],
        .screen_y = cursor[1],
        .delta_x = state->delta[0],
        .delta_y = state->delta[1],
        .drag_x = state->drag[0],
        .drag_y = state->drag[1]
    };
}

/** Find the cursor of the pointer event that triggered an action this frame.
 *
 * @param st The action state
 * @param action The action entity
 * @return The cursor of the last trigger of the action, NULL when no trigger fed it
 */
static const float* box_input_action_cursor(
    const InputActions *st,
    ecs_entity_t action)
{
    const box_input_cursor_t *cursors = ecs_vec_first_t(&st->cursors, box_input_cursor_t);
    const float *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&st->cursors); i ++) {
        if (cursors[i].action == action) {
            result = cursors[i].cursor;
        }
    }

    return result;
}

/** Advance the state of one action and append the events it raises.
 *
 * An action a trigger fed starts at and reports the cursor of the pointer event of the trigger.
 *
 * @param st The action state
 * @param action The action entity
 * @param state The state of the action
 * @param bits The BOX_INPUT_BOUND_* bits of the action this frame
 * @param mouse The cursor state of the frame
 */
static void box_input_action_resolve(
    InputActions *st,
    ecs_entity_t action,
    ActionState *state,
    uint64_t bits,
    const Mouse *mouse)
{
    bool down = (bits & BOX_INPUT_BOUND_DOWN) != 0;
    bool tap = (bits & BOX_INPUT_BOUND_PRESSED) != 0;
    bool was = state->down;
    const float *cursor = box_input_action_cursor(st, action);
    int32_t i;
    cursor = cursor ? cursor : mouse->position;
    state->pressed = !was && (down || tap);
    state->released = was ? !down : (tap && !down);
    state->down = down;
    for (i = 0; i < 2; i ++) {
        if (state->pressed) {
            state->start[i] = cursor[i];
            state->delta[i] = state->drag[i] = 0;
        } else if (down || state->released) {
            state->delta[i] = mouse->delta[i];
            state->drag[i] = mouse->position[i] - state->start[i];
        } else {
            state->delta[i] = state->drag[i] = 0;
        }
    }

    if (state->pressed) {
        box_input_action_raise(st, BoxInputPressed, action, state, cursor);
    }

    if (down) {
        box_input_action_raise(st, BoxInputHeld, action, state, cursor);
    }

    if (down && !state->pressed && (state->delta[0] != 0 || state->delta[1] != 0)) {
        box_input_action_raise(st, BoxInputDragged, action, state, cursor);
    }

    if (state->released) {
        box_input_action_raise(st, BoxInputReleased, action, state, cursor);
    }
}

/** Resolve the waiters of one phase on one target.
 *
 * @param st The action state
 * @param ev The event
 * @param target The action or one of its parents
 * @return Number of waiters resolved
 */
static int32_t box_input_deliver_one(
    InputActions *st,
    const box_input_action_event_t *ev,
    ecs_entity_t target)
{
    ActionEvent value = ev->event;
    int32_t i = 0, result = 0;
    while (i < ecs_vec_count(&st->waiters)) {
        box_input_waiter_t w = *ecs_vec_get_t(&st->waiters, box_input_waiter_t, i);
        if (w.phase != ev->phase || w.target != target) {
            i ++;
            continue;
        }

        ecs_vec_remove_ordered_t(&st->waiters, box_input_waiter_t, i);
        ecs_script_future_resolve(w.future,
            &(ecs_value_t){ .type = ecs_id(ActionEvent), .ptr = &value });
        ecs_script_future_release(w.future);
        result ++;
    }

    return result;
}

/** Resolve the waiters of an event on its action and the parents of the action.
 *
 * @param world The world
 * @param st The action state
 * @param ev The event
 * @return Number of waiters resolved
 */
static int32_t box_input_deliver(
    ecs_world_t *world,
    InputActions *st,
    const box_input_action_event_t *ev)
{
    ecs_entity_t target = ev->event.action;
    int32_t result = 0;
    for (; target && ecs_is_alive(world, target); target = ecs_get_parent(world, target)) {
        result += box_input_deliver_one(st, ev, target);
    }

    return result;
}

/** Raise the action events of the frame, resuming the tasks each one resolves.
 *
 * @param world The world
 */
static void box_input_actions_emit(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&box_input_actions(world)->events); i ++) {
        InputActions *st = box_input_actions(world);
        box_input_action_event_t ev = *ecs_vec_get_t(&st->events, box_input_action_event_t, i);
        if (box_input_deliver(world, st, &ev)) {
            ecs_script_tasks_progress(world);
        }
    }

    ecs_vec_clear(&box_input_actions(world)->events);
}

/** Resolve the bindings and synthetic holds into ActionState and raise the action events.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxInputActionUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    InputActions *st = box_input_actions(world);
    const Input *in = ecs_singleton_get(world, Input);
    ecs_iter_t qit;
    int32_t i;
    box_input_bind(world, st, box_input_device(world), in);
    qit = ecs_query_iter(world, st->actions);
    while (ecs_query_next(&qit)) {
        ActionState *state = ecs_field(&qit, ActionState, 0);
        for (i = 0; i < qit.count; i ++) {
            ecs_map_val_t *bits = ecs_map_get(&st->bound, qit.entities[i]);
            box_input_action_resolve(
                st, qit.entities[i], &state[i], bits ? *bits : 0, &in->mouse);
        }
    }

    box_input_actions_emit(world);
}

/** Suspend a task until an action, or an action under the target, raises a phase.
 *
 * @param ctx Call context, whose ctx holds the phase
 * @param argc Number of arguments
 * @param argv The target entity
 * @param future Future the event resolves
 */
static void box_input_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    box_input_phase_t phase = (box_input_phase_t)(uintptr_t)ctx->ctx;
    ecs_entity_t target = *(ecs_entity_t*)argv[0].ptr;
    InputActions *st = box_input_actions(ctx->world);
    char msg[64];
    (void)argc;
    if (!target || !st) {
        ecs_os_snprintf(msg, sizeof(msg), "on.%s expects an action",
            box_input_phase_names[phase]);
        ecs_script_future_reject(future, msg);
        ecs_script_future_release(future);
        goto done;
    }

    ecs_vec_append_t(NULL, &st->waiters, box_input_waiter_t)[0] =
        (box_input_waiter_t){ future, target, phase };
done:
    return;
}

/** Forget the waiter of a cancelled task.
 *
 * @param ctx Call context
 * @param future The cancelled future
 */
static void box_input_wait_cancel(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    InputActions *st = box_input_actions(ctx->world);
    int32_t i, count = st ? ecs_vec_count(&st->waiters) : 0;
    for (i = 0; i < count; i ++) {
        if (ecs_vec_get_t(&st->waiters, box_input_waiter_t, i)->future == future) {
            ecs_script_future_release(future);
            ecs_vec_remove_ordered_t(&st->waiters, box_input_waiter_t, i);
            break;
        }
    }
}

void box_input_actions_import(
    ecs_world_t *world)
{
    ecs_entity_t on;
    int32_t i;
    ecs_set_name_prefix(world, "Input");
    ECS_COMPONENT_DEFINE(world, InputActions);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, ActionState, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Binding, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ActionEvent, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, InputActions, {
        .ctor = flecs_default_ctor,
        .dtor = box_input_actions_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });
    ecs_add_pair(world, ecs_id(Action), EcsWith, ecs_id(ActionState));

    InputActions *st = ecs_singleton_ensure(world, InputActions);
    ecs_vec_init_t(NULL, &st->waiters, box_input_waiter_t, 0);
    ecs_vec_init_t(NULL, &st->events, box_input_action_event_t, 0);
    ecs_vec_init_t(NULL, &st->cursors, box_input_cursor_t, 0);
    ecs_map_init(&st->bound, NULL);
    st->bindings = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ActionBindings" }),
        .terms = {{ .id = ecs_id(Binding), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });
    st->actions = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "ActionStates" }),
        .terms = {{ .id = ecs_id(ActionState), .inout = EcsInOut }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ActionUpdate" }),
        .phase = EcsOnLoad,
        .run = BoxInputActionUpdate,
        .immediate = true
    });

    on = ecs_entity(world, { .name = "on", .parent = ecs_lookup(world, "flecs.script") });
    for (i = 0; i < BoxInputPhaseCount; i ++) {
        ecs_async_function(world, {
            .name = box_input_phase_names[i],
            .parent = on,
            .return_type = ecs_id(ActionEvent),
            .params = {{ "action", ecs_id(ecs_entity_t) }},
            .callback = box_input_wait,
            .cancel = box_input_wait_cancel,
            .ctx = (void*)(uintptr_t)i
        });
    }
}
