/**
 * @file picking/trigger.c
 * @brief ActionTrigger: the pointer events an element handles drive an input Action.
 *
 * The router feeds every delivered pointer event here; taps and holds go to box.input through
 * its trigger hooks, which resolve them with the bindings in the next ActionUpdate.
 */

#include "picking.h"

/** Get the trigger a mouse button drives on an element.
 *
 * @param world The world
 * @param e The element, 0 for none
 * @param button Index of the mouse button, 0 for KeyMouseLeft
 * @return The trigger, NULL when the element is not alive, is disabled or a prefab, has no
 *         trigger of the button or its action is not an alive Action
 */
static const ActionTrigger* box_pick_trigger_get(
    const ecs_world_t *world,
    ecs_entity_t e,
    int32_t button)
{
    const ActionTrigger *result = NULL;
    if (!e || !ecs_is_alive(world, e) || ecs_has_id(world, e, EcsDisabled) ||
        ecs_has_id(world, e, EcsPrefab))
    {
        goto done;
    }

    result = ecs_get(world, e, ActionTrigger);
    if (result && (result->button != (Key)(KeyMouseLeft + button) ||
        !ecs_is_valid(world, result->action) || !ecs_is_alive(world, result->action) ||
        !ecs_has(world, result->action, Action)))
    {
        result = NULL;
    }

done:
    return result;
}

/** Check whether an element is on the path of an entity: the entity or one of its ancestors.
 *
 * @param world The world
 * @param e The element
 * @param target The entity
 * @return Whether the element is on the path
 */
static bool box_pick_trigger_on_path(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t target)
{
    ecs_entity_t cur;
    int32_t depth = 0;
    bool result = false;
    for (cur = target; cur && !result && depth < BOX_PICK_CHAIN_MAX; depth ++) {
        result = cur == e;
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Check whether an element is under a cursor, ignoring the pointer capture: on the path from
 * the entity a pick there finds up to the root.
 *
 * @param world The world
 * @param st The module state
 * @param e The element
 * @param cursor Cursor in window points
 * @return Whether the element is under the cursor
 */
static bool box_pick_trigger_under(
    ecs_world_t *world,
    box_pick_state_t *st,
    ecs_entity_t e,
    const vec2 cursor)
{
    box_pick_t pick;
    box_pick_at(world, st, cursor, &pick);

    return box_pick_trigger_on_path(world, e, pick.target);
}

/** End the hold of a button.
 *
 * @param dev The device state
 * @param hold The hold
 * @param cursor Cursor of the event that ends it
 */
static void box_pick_trigger_release(
    InputDevice *dev,
    box_pick_hold_t *hold,
    const vec2 cursor)
{
    box_input_trigger_hold(dev, hold->source, hold->action, false, cursor);
    ecs_os_zeromem(hold);
}

/** Start the trigger of an element that a button went down on.
 *
 * @param dev The device state
 * @param st The module state
 * @param e The element
 * @param trigger Its trigger
 * @param button Index of the button
 * @param cursor Cursor of the press
 */
static void box_pick_trigger_press(
    InputDevice *dev,
    box_pick_state_t *st,
    ecs_entity_t e,
    const ActionTrigger *trigger,
    int32_t button,
    const vec2 cursor)
{
    box_pick_hold_t *hold = &st->holds[button];
    switch (trigger->mode) {
    case ActionTriggerClick:
        st->clicks[button] = e;
        break;
    case ActionTriggerPress:
        box_input_trigger_tap(dev, e, trigger->action, cursor);
        break;
    case ActionTriggerHold:
        if (hold->source) {
            box_pick_trigger_release(dev, hold, cursor);
        }

        hold->source = e;
        hold->action = trigger->action;
        box_input_trigger_hold(dev, e, trigger->action, true, cursor);
        break;
    }
}

/** Drive the triggers from a pointer event.
 *
 * @param world The world
 * @param st The module state
 * @param src The event
 * @param pick The pick the event was delivered by
 * @param pressed The buttons that went down with the event
 * @param released The buttons that came up with the event
 * @param direct The event went to the target of the pick without a pick at its cursor, so a
 *        release counts over that target
 */
static void box_pick_triggers_run(
    ecs_world_t *world,
    box_pick_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed,
    uint32_t released,
    bool direct)
{
    InputDevice *dev = box_input_device(world);
    vec2 cursor = { src->screen_x, src->screen_y };
    int32_t b;
    for (b = 0; b < BOX_PICK_BUTTONS; b ++) {
        uint32_t bit = 1u << b;
        box_pick_hold_t *hold = &st->holds[b];
        const ActionTrigger *trigger;
        if (hold->source && (src->buttons & bit)) {
            box_input_trigger_cursor(dev, hold->source, cursor);
        } else if (hold->source) {
            box_pick_trigger_release(dev, hold, cursor);
        }

        if ((released & bit) && st->clicks[b]) {
            trigger = box_pick_trigger_get(world, st->clicks[b], b);
            if (trigger && trigger->mode == ActionTriggerClick && (direct ?
                box_pick_trigger_on_path(world, st->clicks[b], pick->target) :
                box_pick_trigger_under(world, st, st->clicks[b], cursor)))
            {
                box_input_trigger_tap(dev, st->clicks[b], trigger->action, cursor);
            }

            st->clicks[b] = 0;
        }

        trigger = (pressed & bit) ? box_pick_trigger_get(world, pick->handler, b) : NULL;
        if (trigger) {
            box_pick_trigger_press(dev, st, pick->handler, trigger, b, cursor);
        }
    }
}

void box_pick_triggers(
    ecs_world_t *world,
    box_pick_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed,
    uint32_t released)
{
    box_pick_triggers_run(world, st, src, pick, pressed, released, false);
}

void box_pick_triggers_direct(
    ecs_world_t *world,
    const EcsScriptMouseEvent *evt,
    uint32_t pressed,
    uint32_t released)
{
    const PickingState *ps = ecs_id(PickingState) ?
        ecs_singleton_get(world, PickingState) : NULL;
    box_pick_t pick = {0};
    ecs_entity_t cur;
    int32_t depth = 0;
    if (!ps || !ps->state || !evt->target || !ecs_is_alive(world, evt->target)) {
        goto done;
    }

    pick.target = evt->target;
    for (cur = evt->target; cur && !pick.handler && depth < BOX_PICK_CHAIN_MAX; depth ++) {
        pick.handler = ecs_has_id(world, cur, Interactive) ? cur : 0;
        cur = ecs_get_parent(world, cur);
    }

    box_pick_triggers_run(world, ps->state, evt, &pick, pressed, released, true);

done:
    return;
}

void box_pick_triggers_check(
    ecs_world_t *world,
    box_pick_state_t *st)
{
    InputDevice *dev = NULL;
    int32_t b;
    for (b = 0; b < BOX_PICK_BUTTONS; b ++) {
        box_pick_hold_t *hold = &st->holds[b];
        const ActionTrigger *trigger = hold->source ?
            box_pick_trigger_get(world, hold->source, b) : NULL;
        if (hold->source && (!trigger || trigger->mode != ActionTriggerHold ||
            trigger->action != hold->action))
        {
            dev = dev ? dev : box_input_device(world);
            box_pick_trigger_release(dev, hold, st->cursor);
        }
    }
}

/** Set the button of a new trigger to KeyMouseLeft, its other members to 0.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_pick_trigger_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ActionTrigger *t = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_os_zeromem(&t[i]);
        t[i].button = KeyMouseLeft;
    }
}

/** Release the hold of a trigger that is removed or whose entity is deleted, so the action
 * comes up in the next ActionUpdate. Nothing is released once the world quits, because ecs_fini
 * may have freed the module state before it deletes the triggers.
 *
 * @param it Iterator of the hook, whose ctx is the module state
 */
static void box_pick_trigger_on_remove(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_pick_state_t *st = it->ctx;
    int32_t i, b;
    if (ecs_should_quit(world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        for (b = 0; b < BOX_PICK_BUTTONS; b ++) {
            if (st->holds[b].source == it->entities[i]) {
                box_pick_trigger_release(box_input_device(world), &st->holds[b], st->cursor);
            }

            if (st->clicks[b] == it->entities[i]) {
                st->clicks[b] = 0;
            }
        }
    }

done:
    return;
}

void box_pick_triggers_import(
    ecs_world_t *world,
    box_pick_state_t *st)
{
    ecs_set_hooks(world, ActionTrigger, {
        .ctor = box_pick_trigger_ctor,
        .on_remove = box_pick_trigger_on_remove,
        .ctx = st
    });
    ecs_add_pair(world, ecs_id(ActionTrigger), EcsWith, Interactive);
}
