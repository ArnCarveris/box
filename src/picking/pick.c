/**
 * @file picking/pick.c
 * @brief Picks under the cursor once per frame and raises the events it implies.
 *
 * The pick of a frame is one ray query per view that renders into the surface,
 * and only while the cursor moves: a still cursor answers from the hit the last
 * query left behind, so a frame that changes nothing costs no query.
 *
 * The update runs immediately on the world, so a hover that re-evaluates a
 * template restarts its tasks before the press of the same frame is raised, and
 * a press that found no waiter is kept while the button is held for the task.
 */

#include "picking_internal.h"
#include <string.h>

/** Walk the views of the world until one of them picks something.
 *
 * @param world The world
 * @param state The module state
 * @param cursor Normalized cursor position of the frame
 */
static void box_pick_query(
    const ecs_world_t *world,
    PickState *state,
    vec2 cursor)
{
    box_pick_view_t views[BOX_PICK_VIEW_MAX];
    int32_t i, count = box_picking_views(world, state, views, BOX_PICK_VIEW_MAX);
    PickHit hit;

    memset(&state->hit, 0, sizeof(PickHit));
    memset(&state->fallback, 0, sizeof(PickHit));
    state->hit.cursor = cursor;
    state->fallback_valid = false;
    for (i = 0; i < count; i ++) {
        state->queries ++;
        if (box_picking_pick(world, views[i].entity, cursor, &hit)) {
            state->hit = hit;
            break;
        }
    }

    bool through = state->hit.entity && !state->hit.collider &&
        state->through && state->through(world, state->hit.entity);
    for (i = i + 1; through && i < count; i ++) {
        state->queries ++;
        if (box_picking_pick(world, views[i].entity, cursor, &hit)) {
            state->fallback = hit;
            state->fallback_valid = true;
            break;
        }
    }

    state->cursor = cursor;
    state->valid = true;
}

/** Raise an event on the hit, on the hit behind it and last on Unhandled.
 *
 * @param world The world
 * @param state The module state
 * @param event The interaction to raise
 * @param hit The hit the handlers receive
 * @return The entity that took the event, 0 when nothing did
 */
static ecs_entity_t box_pick_raise(
    ecs_world_t *world,
    PickState *state,
    PickEvent event,
    const PickHit *hit)
{
    ecs_entity_t result = box_picking_raise(world, event, hit);
    if (!result && state->fallback_valid && hit == &state->hit) {
        result = box_picking_raise(world, event, &state->fallback);
    }

    if (!result) {
        result = box_picking_raise_unhandled(world, event, hit);
    }

    return result;
}

/** Move the Hovered tag to the entity the cursor points at.
 *
 * A raise without a waiter marks the hover missed, so a waiter registered later
 * in the same hover resolves on its next update. One delivery clears the miss.
 *
 * @param it Iterator of the system, which runs immediately on the world
 * @param state The module state
 * @param entity The entity the cursor points at, 0 for none
 */
static void box_pick_hover(
    ecs_iter_t *it,
    PickState *state,
    ecs_entity_t entity)
{
    if (state->hovered == entity) {
        if (state->hover_missed &&
            box_pick_raise(it->world, state, PickHover, &state->hit))
        {
            state->hover_missed = false;
        }

        goto done;
    }

    if (state->hovered && ecs_is_alive(it->world, state->hovered)) {
        ecs_remove_id(it->world, state->hovered, Hovered);
    }

    state->hovered = entity;
    state->hover_missed = false;
    if (entity) {
        ecs_add_id(it->world, entity, Hovered);
        state->hover_missed =
            box_pick_raise(it->world, state, PickHover, &state->hit) == 0;
    }

done:
    return;
}

/** Build the hit a drag reports from the ray of the cursor of this frame.
 *
 * @param world The world
 * @param state The module state
 * @param cursor Normalized cursor position of the frame
 * @param out Receives the hit of the drag
 */
static void box_pick_drag_hit(
    const ecs_world_t *world,
    const PickState *state,
    vec2 cursor,
    PickHit *out)
{
    vec3 origin, dir;
    *out = state->press;
    out->cursor = cursor;
    if (box_picking_ray(world, state->press.view, cursor, &origin, &dir)) {
        out->point = vec3_add(origin, vec3_scale(dir, state->press.distance));
    }
}

/** Raise the press, drag, release and click events of the frame.
 *
 * @param it Iterator of the system
 * @param state The module state
 * @param in The input state of the frame
 * @param threshold Window points a press moves before it drags
 */
static void box_pick_buttons(
    ecs_iter_t *it,
    PickState *state,
    const Input *in,
    float threshold)
{
    bool down = in->keys[BoxKeyMouseLeft].state;
    bool pressed = in->keys[BoxKeyMouseLeft].pressed;
    bool released = (state->held || pressed) && !down;

    if (pressed) {
        state->press = state->hit;
        state->press_window = in->mouse.window;
        state->pressed = true;
        state->dragging = false;
        state->press_missed = state->hit.entity != 0 &&
            box_pick_raise(it->world, state, PickPress, &state->hit) == 0;
    }

    if (down && state->pressed) {
        float dx = in->mouse.window.x - state->press_window.x;
        float dy = in->mouse.window.y - state->press_window.y;
        bool far_enough = (dx * dx + dy * dy) > (threshold * threshold);
        bool moved = in->mouse.delta.x != 0 || in->mouse.delta.y != 0;
        state->dragging = state->dragging || far_enough;
        if (state->dragging && moved) {
            PickHit drag;
            box_pick_drag_hit(it->world, state, in->mouse.normalized, &drag);
            box_pick_raise(it->world, state, PickDrag, &drag);
        }
    }

    if (released) {
        box_pick_raise(it->world, state, PickRelease, &state->hit);
        if (!state->dragging && state->hit.entity == state->press.entity) {
            box_pick_raise(it->world, state, PickClick, &state->hit);
        }

        state->pressed = false;
        state->press_missed = false;

        memset(&state->press, 0, sizeof(PickHit));
        state->dragging = false;
    }

    state->held = down;
}

void BoxPickingUpdate(
    ecs_iter_t *it)
{
    PickState *state = box_picking_state(it->world);
    const Input *in = ecs_singleton_get(it->world, Input);
    const PickSettings *settings = ecs_singleton_get(it->world, PickSettings);
    state->queries = 0;
    if (!in || !settings || !settings->enabled) {
        box_pick_hover(it, state, 0);
        state->valid = false;
        goto done;
    }

    vec2 cursor = in->mouse.normalized;
    bool moved = in->mouse.delta.x != 0 || in->mouse.delta.y != 0;
    if (!state->valid || moved || cursor.x != state->cursor.x ||
        cursor.y != state->cursor.y)
    {
        box_pick_query(it->world, state, cursor);
    }

    if (state->hit.entity && !ecs_is_alive(it->world, state->hit.entity)) {
        memset(&state->hit, 0, sizeof(PickHit));
        state->hit.cursor = cursor;
    }

    box_pick_hover(it, state, state->hit.entity);
    box_pick_buttons(it, state, in, settings->drag_threshold);
done:
    return;
}

bool box_picking_hovered(
    const ecs_world_t *world,
    PickHit *out)
{
    PickState *state = box_picking_state(world);
    bool result = state && state->hit.entity != 0;
    if (out) {
        if (state) {
            *out = state->hit;
        } else {
            memset(out, 0, sizeof(PickHit));
        }
    }

    return result;
}
