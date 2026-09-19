/**
 * @file picking/pick.c
 * @brief Picks under the cursor once per frame and raises the events it implies.
 *
 * The pick of a frame is one ray query per view that renders into the surface,
 * and only while the cursor moves: a still cursor answers from the hit the last
 * query left behind, so a frame that changes nothing costs no query.
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
    state->hit.cursor = cursor;
    for (i = 0; i < count; i ++) {
        state->queries ++;
        if (box_picking_pick(world, views[i].entity, cursor, &hit)) {
            state->hit = hit;
            break;
        }
    }

    state->cursor = cursor;
    state->valid = true;
}

/** Move the Hovered tag to the entity the cursor points at.
 *
 * A raise that finds no waiter leaves the hover missed for this entity, so a
 * waiter that registers later in the same hover resolves on its next update
 * instead of waiting for a hit that will never change again. One delivery
 * clears the miss, so a handler that awaits hover again keeps single shot.
 *
 * @param it Iterator of the system, whose stage carries the change
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
            box_picking_raise(it->stage, PickHover, &state->hit))
        {
            state->hover_missed = false;
        }

        goto done;
    }

    if (state->hovered && ecs_is_alive(it->world, state->hovered)) {
        ecs_remove_id(it->stage, state->hovered, Hovered);
    }

    state->hovered = entity;
    state->hover_missed = false;
    if (entity) {
        ecs_add_id(it->stage, entity, Hovered);
        state->hover_missed =
            box_picking_raise(it->stage, PickHover, &state->hit) == 0;
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
        state->dragging = false;
        if (state->hit.entity) {
            box_picking_raise(it->stage, PickPress, &state->hit);
        }
    }

    if (down && state->press.entity) {
        float dx = in->mouse.window.x - state->press_window.x;
        float dy = in->mouse.window.y - state->press_window.y;
        bool far_enough = (dx * dx + dy * dy) > (threshold * threshold);
        bool moved = in->mouse.delta.x != 0 || in->mouse.delta.y != 0;
        state->dragging = state->dragging || far_enough;
        if (state->dragging && moved) {
            PickHit drag;
            box_pick_drag_hit(it->world, state, in->mouse.normalized, &drag);
            box_picking_raise(it->stage, PickDrag, &drag);
        }
    }

    if (released) {
        if (state->hit.entity) {
            box_picking_raise(it->stage, PickRelease, &state->hit);
        }

        if (!state->dragging && state->hit.entity == state->press.entity) {
            box_picking_raise(it->stage, PickClick, &state->hit);
        }

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
