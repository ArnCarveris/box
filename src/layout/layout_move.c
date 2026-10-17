/**
 * @file layout/layout_move.c
 * @brief Moves elements without solving their tree: the children of a scrolling element whose
 * offset changed and the floating elements, placed against their anchors.
 *
 * Both work from the Position, LayoutBounds and LayoutContent the trees wrote, so a scroll or
 * an anchor that moved costs no measuring.
 */

#include "layout_internal.h"
#include <math.h>

/** Most ancestors a walk up the hierarchy visits */
#define BOX_LAYOUT_WALK (256)

void box_layout_scroll_shift(
    ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t e)
{
    const LayoutScroll *scroll = ecs_get(world, e, LayoutScroll);
    const LayoutContent *content = ecs_get(world, e, LayoutContent);
    const Layout *layout = ecs_get(world, e, Layout);
    int32_t i;
    if (!scroll || !content || !layout) {
        goto done;
    }

    float x = isfinite(scroll->x) ? fminf(fmaxf(scroll->x, 0), content->max_x) : 0;
    float y = isfinite(scroll->y) ? fminf(fmaxf(scroll->y, 0), content->max_y) : 0;
    float dx = x - content->x, dy = y - content->y;
    if (dx == 0 && dy == 0) {
        goto done;
    }

    LayoutContent next = *content;
    next.x = x;
    next.y = y;
    state->writing = true;
    ecs_set_ptr(world, e, LayoutContent, &next);
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t c = it.entities[i];
            const Position *p = ecs_get(world, c, Position);
            if (!p || ecs_has_id(world, c, EcsPrefab) || ecs_has(world, c, LayoutFloat) ||
                (layout->direction == DirectionNone && !ecs_has(world, c, LayoutItem)))
            {
                continue;
            }

            ecs_set(world, c, Position, { p->x - dx, p->y + dy, p->z });
        }
    }

    state->writing = false;
done:
    return;
}

/** Get the size of an element: its LayoutBounds, else its Rect.
 *
 * @param world The world
 * @param e The element
 * @param size Receives the size, zero without either
 */
static void box_layout_size_of(
    const ecs_world_t *world,
    ecs_entity_t e,
    float size[2])
{
    const LayoutBounds *lb = ecs_get(world, e, LayoutBounds);
    const Rect *r = lb ? NULL : ecs_get(world, e, Rect);
    size[0] = lb ? lb->width : r ? r->width : 0;
    size[1] = lb ? lb->height : r ? r->height : 0;
}

/** Get the position of an element summed with those of its ancestors, rotation and scale left
 * out, the space floating elements are placed in.
 *
 * @param world The world
 * @param e The element
 * @param pos Receives the position
 */
static void box_layout_sum(
    const ecs_world_t *world,
    ecs_entity_t e,
    float pos[2])
{
    int32_t depth = 0;
    pos[0] = pos[1] = 0;
    for (; e && depth < BOX_LAYOUT_WALK; e = ecs_get_parent(world, e)) {
        const Position *p = ecs_get(world, e, Position);
        if (p) {
            pos[0] += p->x;
            pos[1] += p->y;
        }

        depth ++;
    }
}

/** Get the anchor of a floating element: the one it names, else its parent.
 *
 * @param world The world
 * @param e The element
 * @param fl Its LayoutFloat
 * @param parent Its parent
 * @return The anchor
 */
static ecs_entity_t box_layout_anchor(
    const ecs_world_t *world,
    ecs_entity_t e,
    const LayoutFloat *fl,
    ecs_entity_t parent)
{
    ecs_entity_t a = fl->anchor;

    return a && a != e && ecs_is_alive(world, a) ? a : parent;
}

/** Get the offset of a point of a box from its center on the x axis.
 *
 * @param align The point
 * @param size Width of the box
 * @return The offset
 */
static float box_layout_point_x(
    int32_t align,
    float size)
{
    return (float)(align - 1) * size / 2;
}

/** Get the offset of a point of a box from its center on the y axis, y up.
 *
 * @param align The point, 0 for the top
 * @param size Height of the box
 * @return The offset
 */
static float box_layout_point_y(
    int32_t align,
    float size)
{
    return (float)(1 - align) * size / 2;
}

/** Get how far a span passes out of a range on its two sides.
 *
 * @param center Center of the span
 * @param size Size of the span
 * @param lo Low end of the range
 * @param hi High end of the range
 * @return The length outside the range
 */
static float box_layout_outside(
    float center,
    float size,
    float lo,
    float hi)
{
    return fmaxf(center + size / 2 - hi, 0) + fmaxf(lo - (center - size / 2), 0);
}

/** Place a floating element against its anchor and, with flip, inside the root of its tree.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param e The element
 */
static void box_layout_float_place(
    ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t e)
{
    const LayoutFloat *fl = ecs_get(world, e, LayoutFloat);
    ecs_entity_t parent = fl ? ecs_get_parent(world, e) : 0;
    if (!parent || !ecs_has(world, parent, Layout)) {
        goto done;
    }

    ecs_entity_t anchor = box_layout_anchor(world, e, fl, parent);
    float as[2], fs[2], ap[2], pp[2];
    box_layout_size_of(world, anchor, as);
    box_layout_size_of(world, e, fs);
    box_layout_sum(world, anchor, ap);
    box_layout_sum(world, parent, pp);
    float x = ap[0] + box_layout_point_x(fl->anchor_h, as[0]) + fl->x -
        box_layout_point_x(fl->h, fs[0]);
    float y = ap[1] + box_layout_point_y(fl->anchor_v, as[1]) + fl->y -
        box_layout_point_y(fl->v, fs[1]);
    ecs_entity_t root = fl->flip ? box_layout_root(world, e) : 0;
    if (root) {
        float rs[2], rp[2];
        box_layout_size_of(world, root, rs);
        box_layout_sum(world, root, rp);
        float lo[2] = { rp[0] - rs[0] / 2, rp[1] - rs[1] / 2 };
        float hi[2] = { rp[0] + rs[0] / 2, rp[1] + rs[1] / 2 };
        float out = box_layout_outside(y, fs[1], lo[1], hi[1]);
        if (out > 0) {
            float flipped = ap[1] + box_layout_point_y(2 - (int32_t)fl->anchor_v, as[1]) -
                fl->y - box_layout_point_y(2 - (int32_t)fl->v, fs[1]);
            if (box_layout_outside(flipped, fs[1], lo[1], hi[1]) < out) {
                y = flipped;
            }
        }

        if (x + fs[0] / 2 > hi[0]) {
            x = hi[0] - fs[0] / 2;
        }

        if (x - fs[0] / 2 < lo[0]) {
            x = lo[0] + fs[0] / 2;
        }
    }

    x -= pp[0];
    y -= pp[1];
    const Position *p = ecs_get(world, e, Position);
    if (!p || p->x != x || p->y != y) {
        state->writing = true;
        ecs_set(world, e, Position, { x, y, p ? p->z : 0 });
        state->writing = false;
    }

done:
    return;
}

/** Check whether the anchor of a floating element that grows changed size since its tree was
 * laid out, which happens when the anchor is in another tree.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param e The element
 * @return Whether its tree needs to be laid out again
 */
static bool box_layout_float_stale(
    const ecs_world_t *world,
    const box_layout_state_t *state,
    ecs_entity_t e)
{
    const LayoutFloat *fl = ecs_get(world, e, LayoutFloat);
    const LayoutItem *item = ecs_get(world, e, LayoutItem);
    ecs_entity_t parent = fl ? ecs_get_parent(world, e) : 0;
    bool result = false;
    if (!item || (item->width.mode != SizeModeGrow && item->height.mode != SizeModeGrow) ||
        !parent || !ecs_has(world, parent, Layout))
    {
        goto done;
    }

    float as[2];
    uint32_t w, h;
    box_layout_size_of(world, box_layout_anchor(world, e, fl, parent), as);
    ecs_os_memcpy(&w, &as[0], ECS_SIZEOF(uint32_t));
    ecs_os_memcpy(&h, &as[1], ECS_SIZEOF(uint32_t));
    const ecs_map_val_t *known = ecs_map_get(&state->anchor_sizes, (ecs_map_key_t)e);
    result = !known || *known != (((ecs_map_val_t)w << 32) | h);
done:
    return result;
}

/** Order floats to place by depth, then id.
 *
 * @param a The first float
 * @param b The second float
 * @return Negative, zero or positive
 */
static int box_layout_placing_compare(
    const void *a,
    const void *b)
{
    const box_layout_placing_t *x = a, *y = b;
    int result = (x->depth > y->depth) - (x->depth < y->depth);
    if (!result) {
        result = (x->e > y->e) - (x->e < y->e);
    }

    return result;
}

void box_layout_floats(
    ecs_world_t *world,
    box_layout_state_t *state)
{
    int32_t i, count = ecs_vec_count(&state->floats);
    if (!count) {
        goto done;
    }

    ecs_vec_clear(&state->placing);
    for (i = 0; i < ecs_vec_count(&state->floats); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&state->floats, ecs_entity_t, i);
        if (!ecs_is_alive(world, e) || ecs_has_id(world, e, EcsPrefab)) {
            continue;
        }

        if (box_layout_float_stale(world, state, e)) {
            ecs_entity_t root = box_layout_root(world, e);
            if (root) {
                box_layout_tree(world, state, root);
            }
        }

        box_layout_placing_t *p = ecs_vec_append_t(NULL, &state->placing, box_layout_placing_t);
        p->e = e;
        p->depth = 0;
        ecs_entity_t cur = ecs_get_parent(world, e);
        for (; cur && p->depth < BOX_LAYOUT_WALK; cur = ecs_get_parent(world, cur)) {
            p->depth ++;
        }
    }

    count = ecs_vec_count(&state->placing);
    box_layout_placing_t *placing = ecs_vec_first_t(&state->placing, box_layout_placing_t);
    if (count > 1) {
        qsort(placing, (size_t)count, sizeof(box_layout_placing_t), box_layout_placing_compare);
    }

    for (i = 0; i < count; i ++) {
        box_layout_float_place(world, state, placing[i].e);
    }

done:
    return;
}
