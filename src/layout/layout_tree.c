/**
 * @file layout/layout_tree.c
 * @brief Sizes and places the elements of one layout tree in logical pixels.
 *
 * Works one axis at a time as Clay does: widths are measured from the leaves up and resolved
 * from the root down, texts wrap to their resolved width, then heights follow the same way.
 * Positions come last, and the layout writes Position, LayoutBounds and Rect.
 */

#include "layout_internal.h"
#include "../text/text.h"
#include <math.h>

/** Size of a TextBounds value as the layout reads it */
typedef struct {
    float width;  /**< Width of the text */
    float height; /**< Height of the text */
} box_layout_text_t;

/** Get the sizing of an element on one axis.
 *
 * @param n The element
 * @param axis 0 for x, 1 for y
 * @return The sizing
 */
static const Sizing* box_layout_sizing(
    const box_layout_node_t *n,
    int32_t axis)
{
    return axis ? &n->item.height : &n->item.width;
}

/** Bound a size by the min and max of a sizing, a max of 0 being no limit.
 *
 * @param v The size
 * @param s The sizing
 * @return The bounded size, never below 0
 */
static float box_layout_clamp(
    float v,
    const Sizing *s)
{
    float result = v;
    if (s->max > 0 && result > s->max) {
        result = s->max;
    }

    if (result < s->min) {
        result = s->min;
    }

    if (result < 0) {
        result = 0;
    }

    return result;
}

/** Bound the size of a growing element by its min, max and, with content_min, its content.
 *
 * @param c The element
 * @param axis 0 for x, 1 for y
 * @param v The share or stretch of the element
 * @return The bounded size
 */
static float box_layout_grow_clamp(
    const box_layout_node_t *c,
    int32_t axis,
    float v)
{
    const Sizing *s = box_layout_sizing(c, axis);
    float result = box_layout_clamp(v, s);
    if (s->content_min && result < c->cmin[axis]) {
        result = c->cmin[axis];
    }

    return result;
}

/** Test whether an element grows on an axis.
 *
 * @param n The element
 * @param axis 0 for x, 1 for y
 * @return True when the element grows on the axis
 */
static bool box_layout_grows(
    const box_layout_node_t *n,
    int32_t axis)
{
    return box_layout_sizing(n, axis)->mode == SizeModeGrow;
}

/** Read the components of an element into a new node of the tree.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param e The element
 * @return Index of the node
 */
static int32_t box_layout_read(
    const ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t e)
{
    int32_t result = ecs_vec_count(&state->nodes);
    box_layout_node_t *n = ecs_vec_append_t(NULL, &state->nodes, box_layout_node_t);
    ecs_os_zeromem(n);
    n->e = e;
    const Layout *l = ecs_get(world, e, Layout);
    if (l) {
        n->layout = *l;
        n->has_layout = true;
    }

    const LayoutItem *item = ecs_get(world, e, LayoutItem);
    if (item) {
        n->item = *item;
        n->has_item = true;
    }

    const Position *p = ecs_get(world, e, Position);
    if (p) {
        n->pos[0] = p->x;
        n->pos[1] = p->y;
        n->pos[2] = p->z;
        n->has_pos = true;
    }

    const Rect *r = ecs_get(world, e, Rect);
    const box_layout_text_t *t = state->text_bounds ?
        ecs_get_id(world, e, state->text_bounds) : NULL;
    if (t) {
        n->has_text = box_text_measure(world, e, n->shape, &n->longest);
        state->has_text |= n->has_text;
        if (!n->has_text) {
            n->shape[0] = n->longest = t->width;
            n->shape[1] = t->height;
        }
    } else if (r) {
        n->shape[0] = n->item.width.mode == SizeModeGrow ? 0 : r->width;
        n->shape[1] = n->item.height.mode == SizeModeGrow ? 0 : r->height;
    } else {
        const Circle *c = ecs_get(world, e, Circle);
        if (c) {
            n->shape[0] = n->shape[1] = c->radius * 2;
        }
    }

    n->has_rect = r != NULL;
    n->rect_input = !n->has_layout &&
        n->item.width.mode == SizeModeFit && n->item.height.mode == SizeModeFit;

    return result;
}

/** Collect a tree breadth first, so the children of an element are next to each other.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param root The root of the tree
 */
static void box_layout_collect(
    const ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t root)
{
    ecs_vec_clear(&state->nodes);
    state->has_text = false;
    box_layout_read(world, state, root);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->nodes); i ++) {
        box_layout_node_t *n = ecs_vec_get_t(&state->nodes, box_layout_node_t, i);
        if (!n->has_layout) {
            continue;
        }

        ecs_entity_t e = n->e;
        int32_t first = ecs_vec_count(&state->nodes);
        ecs_iter_t it = ecs_children(world, e);
        while (ecs_children_next(&it)) {
            int32_t c;
            for (c = 0; c < it.count; c ++) {
                box_layout_read(world, state, it.entities[c]);
            }
        }

        n = ecs_vec_get_t(&state->nodes, box_layout_node_t, i);
        n->first = first;
        n->count = ecs_vec_count(&state->nodes) - first;
    }
}

/** Get the space a child of a DirectionNone element needs on one axis.
 *
 * An anchored child needs its size plus its offset, twice the offset when centered. A child
 * without LayoutItem needs twice its distance from the origin plus its size.
 *
 * @param c The child
 * @param axis 0 for x, 1 for y
 * @param size The size of the child
 * @return The space the child needs
 */
static float box_layout_extent(
    const box_layout_node_t *c,
    int32_t axis,
    float size)
{
    float result;
    if (c->has_item) {
        int32_t anchor = axis ? (int32_t)c->item.v : (int32_t)c->item.h;
        float offset = fabsf(axis ? c->item.y : c->item.x);
        result = size + (anchor == 1 ? 2 * offset : offset);
    } else {
        result = 2 * fabsf(c->pos[axis]) + size;
    }

    return result;
}

/** Get the smallest size the parent of an element can give it on one axis.
 *
 * A growing element can get its content minimum with content_min, else its min. A fitting or
 * fixed element keeps its size.
 *
 * @param c The element, measured on the axis
 * @param axis 0 for x, 1 for y
 * @return The smallest size
 */
static float box_layout_least(
    const box_layout_node_t *c,
    int32_t axis)
{
    const Sizing *s = box_layout_sizing(c, axis);
    float result = c->base[axis];
    if (s->mode == SizeModeGrow) {
        result = s->content_min ? c->cmin[axis] : box_layout_clamp(0, s);
    }

    return result;
}

/** Measure the fit size and content minimum of an element on a range of axes.
 *
 * @param nodes The nodes of the tree
 * @param n The element, its children already measured on the axes
 * @param first First axis, 0 for x, 1 for y
 * @param last Axis after the last one
 */
static void box_layout_measure(
    box_layout_node_t *nodes,
    box_layout_node_t *n,
    int32_t first,
    int32_t last)
{
    float fit[2] = { n->shape[0], n->shape[1] };
    float least[2] = { n->has_text ? n->longest : n->shape[0], n->shape[1] };
    int32_t axis;
    if (n->has_layout) {
        int32_t main = n->layout.direction == DirectionRow ? 0 :
            n->layout.direction == DirectionColumn ? 1 : -1;
        const Padding *pad = &n->layout.padding;
        float edges[2] = { pad->left + pad->right, pad->top + pad->bottom };
        int32_t i;
        fit[0] = fit[1] = least[0] = least[1] = 0;
        for (i = 0; i < n->count; i ++) {
            const box_layout_node_t *c = &nodes[n->first + i];
            for (axis = first; axis < last; axis ++) {
                float v = c->base[axis], l = box_layout_least(c, axis);
                if (main == -1) {
                    v = box_layout_extent(c, axis, v);
                    l = box_layout_extent(c, axis, l);
                }

                fit[axis] = axis == main ? fit[axis] + v : fmaxf(fit[axis], v);
                least[axis] = axis == main ? least[axis] + l : fmaxf(least[axis], l);
            }
        }

        for (axis = first; axis < last; axis ++) {
            float gaps = axis == main && n->count > 1 ?
                n->layout.spacing * (float)(n->count - 1) : 0;
            fit[axis] += gaps + edges[axis];
            least[axis] += gaps + edges[axis];
        }
    }

    for (axis = first; axis < last; axis ++) {
        const Sizing *s = box_layout_sizing(n, axis);
        n->size[axis] = box_layout_clamp(s->mode == SizeModeFixed ? s->value : fit[axis], s);
        n->base[axis] = n->size[axis];
        n->cmin[axis] = s->mode == SizeModeFixed ?
            n->size[axis] : box_layout_clamp(least[axis], s);
    }
}

/** Share the free space of a row or column among its growing children by weight.
 *
 * A share starts from zero. A child with content_min never gets less than its content
 * minimum, so the children whose share is below it keep it and the others share the rest,
 * which ends where Clay ends by growing its smallest elements first.
 *
 * A child whose share passes a bound keeps the bound, and the rest is shared again.
 *
 * @param nodes The nodes of the tree
 * @param n The parent
 * @param main The main axis of the parent
 * @param space The size of the content box on the main axis
 */
static void box_layout_grow(
    box_layout_node_t *nodes,
    const box_layout_node_t *n,
    int32_t main,
    float space)
{
    float free = space;
    int32_t i, pass;
    if (n->count > 1) {
        free -= n->layout.spacing * (float)(n->count - 1);
    }

    for (i = 0; i < n->count; i ++) {
        box_layout_node_t *c = &nodes[n->first + i];
        c->frozen = !box_layout_grows(c, main);
        if (c->frozen) {
            free -= c->size[main];
        }
    }

    for (pass = 0; pass <= n->count; pass ++) {
        float weights = 0, over = 0;
        for (i = 0; i < n->count; i ++) {
            const box_layout_node_t *c = &nodes[n->first + i];
            float w = box_layout_sizing(c, main)->value;
            weights += c->frozen ? 0 : fmaxf(w, 0.0f);
        }

        if (weights <= 0) {
            break;
        }

        for (i = 0; i < n->count; i ++) {
            box_layout_node_t *c = &nodes[n->first + i];
            const Sizing *s = box_layout_sizing(c, main);
            float raw = free * fmaxf(s->value, 0.0f) / weights;
            if (!c->frozen) {
                c->size[main] = box_layout_grow_clamp(c, main, raw);
                over += c->size[main] - raw;
            }
        }

        if (fabsf(over) < 1e-4f) {
            break;
        }

        float left = free;
        for (i = 0; i < n->count; i ++) {
            box_layout_node_t *c = &nodes[n->first + i];
            const Sizing *s = box_layout_sizing(c, main);
            float raw = free * fmaxf(s->value, 0.0f) / weights;
            if (!c->frozen && ((over > 0 && c->size[main] > raw) ||
                (over < 0 && c->size[main] < raw)))
            {
                c->frozen = true;
                left -= c->size[main];
            }
        }

        free = left;
    }
}

/** Get the center of a child aligned in a box on one axis.
 *
 * @param lo The low edge of the box
 * @param hi The high edge of the box
 * @param size The size of the child
 * @param align 0 for left or top, 1 for the middle, 2 for right or bottom
 * @param axis 0 for x, 1 for y (y up, so top is the high edge)
 * @return The center of the child
 */
static float box_layout_align(
    float lo,
    float hi,
    float size,
    int32_t align,
    int32_t axis)
{
    float space = hi - lo;
    float from = align == 0 ? size / 2 : (align == 1 ? space / 2 : space - size / 2);

    return axis ? hi - from : lo + from;
}

/** Get the content box of an element on one axis, empty when the padding passes the size.
 *
 * @param n The element, its size final
 * @param axis 0 for x, 1 for y
 * @param lo Receives the low edge
 * @param hi Receives the high edge
 */
static void box_layout_content(
    const box_layout_node_t *n,
    int32_t axis,
    float *lo,
    float *hi)
{
    const Padding *pad = &n->layout.padding;
    *lo = -n->size[axis] / 2 + (axis ? pad->bottom : pad->left);
    *hi = n->size[axis] / 2 - (axis ? pad->top : pad->right);
    if (*hi < *lo) {
        *hi = *lo;
    }
}

/** Size the children of an element whose size is final on a range of axes.
 *
 * @param nodes The nodes of the tree
 * @param n The element
 * @param first First axis, 0 for x, 1 for y
 * @param last Axis after the last one
 */
static void box_layout_resolve(
    box_layout_node_t *nodes,
    const box_layout_node_t *n,
    int32_t first,
    int32_t last)
{
    float lo[2], hi[2];
    int32_t i, axis, main = n->layout.direction == DirectionRow ? 0 :
        n->layout.direction == DirectionColumn ? 1 : -1;
    for (axis = first; axis < last; axis ++) {
        box_layout_content(n, axis, &lo[axis], &hi[axis]);
    }

    for (i = 0; i < n->count; i ++) {
        box_layout_node_t *c = &nodes[n->first + i];
        for (axis = first; axis < last; axis ++) {
            if (axis != main && box_layout_grows(c, axis)) {
                c->size[axis] = box_layout_grow_clamp(c, axis, hi[axis] - lo[axis]);
            }
        }
    }

    if (main >= first && main < last) {
        box_layout_grow(nodes, n, main, hi[main] - lo[main]);
    }
}

/** Place the children of an element whose children are sized.
 *
 * @param nodes The nodes of the tree
 * @param n The element
 */
static void box_layout_place(
    box_layout_node_t *nodes,
    const box_layout_node_t *n)
{
    float lo[2], hi[2];
    int32_t main = n->layout.direction == DirectionRow ? 0 :
        n->layout.direction == DirectionColumn ? 1 : -1;
    int32_t align[2] = { (int32_t)n->layout.h, (int32_t)n->layout.v };
    int32_t i;
    box_layout_content(n, 0, &lo[0], &hi[0]);
    box_layout_content(n, 1, &lo[1], &hi[1]);
    if (main == -1) {
        for (i = 0; i < n->count; i ++) {
            box_layout_node_t *c = &nodes[n->first + i];
            c->placed = c->has_item;
            c->at[0] = box_layout_align(lo[0], hi[0], c->size[0], c->item.h, 0) + c->item.x;
            c->at[1] = box_layout_align(lo[1], hi[1], c->size[1], c->item.v, 1) + c->item.y;
        }

        goto done;
    }

    int32_t cross = 1 - main;
    float total = 0;
    for (i = 0; i < n->count; i ++) {
        total += nodes[n->first + i].size[main];
    }

    if (n->count > 1) {
        total += n->layout.spacing * (float)(n->count - 1);
    }

    float space = hi[main] - lo[main];
    float cursor = box_layout_align(0, space, total, align[main], 0) - total / 2;
    for (i = 0; i < n->count; i ++) {
        box_layout_node_t *c = &nodes[n->first + i];
        float center = cursor + c->size[main] / 2;
        c->placed = true;
        c->at[main] = main ? hi[1] - center : lo[0] + center;
        c->at[cross] = box_layout_align(
            lo[cross], hi[cross], c->size[cross], align[cross], cross);
        cursor += c->size[main] + n->layout.spacing;
    }

done:
    return;
}

/** Wrap the texts of a tree to their resolved widths and take the heights they get.
 *
 * A text with a fit width keeps the lines of its Text.width.
 *
 * @param world The world
 * @param nodes The nodes of the tree
 * @param count Number of nodes
 */
static void box_layout_wrap(
    ecs_world_t *world,
    box_layout_node_t *nodes,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_layout_node_t *n = &nodes[i];
        if (n->has_text) {
            float width = n->item.width.mode == SizeModeFit ? 0 : n->size[0];
            n->shape[1] = box_text_wrap_to(world, n->e, width);
        }
    }
}

/** Write the computed size and position of an element where they changed.
 *
 * @param world The world
 * @param n The element
 */
static void box_layout_write(
    ecs_world_t *world,
    const box_layout_node_t *n)
{
    const LayoutBounds *b = ecs_get(world, n->e, LayoutBounds);
    if (!b || b->width != n->size[0] || b->height != n->size[1]) {
        ecs_set(world, n->e, LayoutBounds, { n->size[0], n->size[1] });
    }

    if (n->placed && (!n->has_pos || n->pos[0] != n->at[0] || n->pos[1] != n->at[1])) {
        ecs_set(world, n->e, Position, { n->at[0], n->at[1], n->pos[2] });
    }

    if (n->has_rect && !n->rect_input) {
        const Rect *r = ecs_get(world, n->e, Rect);
        if (r->width != n->size[0] || r->height != n->size[1]) {
            ecs_set(world, n->e, Rect, { n->size[0], n->size[1] });
        }
    }
}

void box_layout_tree(
    ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t root)
{
    box_layout_collect(world, state, root);
    box_layout_node_t *nodes = ecs_vec_first_t(&state->nodes, box_layout_node_t);
    int32_t i, first, count = ecs_vec_count(&state->nodes);
    int32_t axes = state->has_text ? 1 : 2;
    for (first = 0; first < 2; first += axes) {
        if (first == 1) {
            state->writing = true;
            box_layout_wrap(world, nodes, count);
            state->writing = false;
        }

        for (i = count - 1; i >= 0; i --) {
            box_layout_measure(nodes, &nodes[i], first, first + axes);
        }

        for (i = 0; i < count; i ++) {
            if (nodes[i].has_layout) {
                box_layout_resolve(nodes, &nodes[i], first, first + axes);
            }

            if (nodes[i].has_layout && first + axes == 2) {
                box_layout_place(nodes, &nodes[i]);
            }
        }
    }

    state->writing = true;
    for (i = 0; i < count; i ++) {
        box_layout_write(world, &nodes[i]);
    }

    state->writing = false;
}
