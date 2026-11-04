/**
 * @file layout/layout_tree.c
 * @brief Sizes and places the elements of one layout tree in logical pixels.
 *
 * Works one axis at a time as Clay does: widths are measured from the leaves up and resolved
 * from the root down, texts wrap to their resolved width, then heights follow the same way.
 * Positions come last, and the layout writes Position, LayoutBounds and Rect.
 */

#include "layout_internal.h"
#include <math.h>

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

/** Test whether an element scrolls on an axis, so its children keep their fit size there.
 *
 * @param n The element
 * @param axis 0 for x, 1 for y
 * @return True when the element scrolls on the axis
 */
static bool box_layout_scrolls(
    const box_layout_node_t *n,
    int32_t axis)
{
    return n->has_scroll && (axis ? n->scroll.vertical : n->scroll.horizontal);
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

    const LayoutScroll *scroll = l ? ecs_get(world, e, LayoutScroll) : NULL;
    if (scroll) {
        n->scroll = *scroll;
        n->has_scroll = true;
    }

    const LayoutFloat *fl = ecs_get(world, e, LayoutFloat);
    if (fl) {
        n->fl = *fl;
        n->is_float = true;
    }

    const Position *p = ecs_get(world, e, Position);
    if (p) {
        n->pos[0] = p->x;
        n->pos[1] = p->y;
        n->pos[2] = p->z;
        n->has_pos = true;
    }

    const Rect *r = ecs_get(world, e, Rect);
    bool text = state->text.measure &&
        state->text.measure(world, e, n->shape, &n->longest, &n->has_text);
    state->has_text |= n->has_text;
    if (text) {
        n->longest = n->has_text ? n->longest : n->shape[0];
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

/** Track a child the tree holds: keep its parent when it is held through Parent, so moving it
 * marks the parent it leaves, and note it when it lacks the child tag the Position change query
 * matches. A prefab child is not part of the tree.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param child The child
 * @param parent Its parent
 * @return False for a prefab child, which the tree skips
 */
static bool box_layout_track(
    const ecs_world_t *world,
    box_layout_state_t *state,
    ecs_entity_t child,
    ecs_entity_t parent)
{
    ecs_table_t *table = ecs_get_table(world, child);
    if (table != state->child_table) {
        state->child_table = table;
        state->child_prefab = table && ecs_table_has_id(world, table, EcsPrefab);
        state->child_kept = table && ecs_table_has_id(world, table, ecs_id(EcsParent));
        state->child_tagged = table && ecs_table_has_id(world, table, state->child_tag);
    }

    if (!state->child_prefab && state->child_kept) {
        ecs_map_ensure(&state->parents, (ecs_map_key_t)child)[0] = (ecs_map_val_t)parent;
    }

    if (!state->child_prefab && !state->child_tagged) {
        *ecs_vec_append_t(NULL, &state->untagged, ecs_entity_t) = child;
    }

    return !state->child_prefab;
}

/** Move the floating children of an element after the others, keeping their order, and link
 * every child to its parent.
 *
 * @param state The state of the layout module
 * @param parent Index of the element
 * @param first Index of its first child
 * @return Number of its floating children
 */
static int32_t box_layout_partition(
    box_layout_state_t *state,
    int32_t parent,
    int32_t first)
{
    box_layout_node_t *nodes = ecs_vec_first_t(&state->nodes, box_layout_node_t);
    int32_t i, kept = first, end = ecs_vec_count(&state->nodes);
    int32_t root = nodes[parent].float_root;
    ecs_vec_clear(&state->floating);
    for (i = first; i < end; i ++) {
        if (nodes[i].is_float) {
            *ecs_vec_append_t(NULL, &state->floating, box_layout_node_t) = nodes[i];
        } else {
            nodes[kept ++] = nodes[i];
        }
    }

    if (ecs_vec_count(&state->floating)) {
        ecs_os_memcpy_n(&nodes[kept], ecs_vec_first(&state->floating), box_layout_node_t,
            end - kept);
    }

    for (i = first; i < end; i ++) {
        nodes[i].parent = parent;
        nodes[i].float_root = nodes[i].is_float ? i : root;
    }

    return end - kept;
}

/** Collect a tree breadth first, so the children of an element are next to each other.
 *
 * Every child is tracked (box_layout_track), and prefab children are left out. The floating
 * children of an element follow its other children.
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
    state->child_table = NULL;
    ecs_vec_clear(&state->untagged);
    box_layout_read(world, state, root);
    box_layout_node_t *r = ecs_vec_first_t(&state->nodes, box_layout_node_t);
    r->is_float = false;
    r->parent = r->float_root = -1;
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
                if (box_layout_track(world, state, it.entities[c], e)) {
                    box_layout_read(world, state, it.entities[c]);
                }
            }
        }

        int32_t floats = box_layout_partition(state, i, first);
        n = ecs_vec_get_t(&state->nodes, box_layout_node_t, i);
        n->first = first;
        n->floats = floats;
        n->count = ecs_vec_count(&state->nodes) - first - floats;
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
 * A fitting element can get its content minimum, a growing one too with content_min and else
 * its min. A fixed element keeps its size.
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
    float result = s->mode == SizeModeFixed ? c->base[axis] : c->cmin[axis];
    if (s->mode == SizeModeGrow && !s->content_min) {
        result = box_layout_clamp(0, s);
    }

    return result;
}

/** Measure the fit size and content minimum of an element on a range of axes.
 *
 * Floating children are left out. Along an axis the element scrolls, its content minimum is
 * its padding.
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
                float v = c->base[axis], l = c->cmin[axis];
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
            if (box_layout_scrolls(n, axis)) {
                least[axis] = edges[axis];
            }
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
 * @return The size of the children and the spacing between them
 */
static float box_layout_grow(
    box_layout_node_t *nodes,
    const box_layout_node_t *n,
    int32_t main,
    float space)
{
    float gaps = n->count > 1 ? n->layout.spacing * (float)(n->count - 1) : 0;
    float free = space - gaps, result = gaps;
    int32_t i, pass, growing = 0;
    for (i = 0; i < n->count; i ++) {
        box_layout_node_t *c = &nodes[n->first + i];
        c->frozen = !box_layout_grows(c, main);
        result += c->frozen ? c->size[main] : 0;
        growing += !c->frozen;
    }

    free -= result - gaps;
    for (pass = 0; growing && pass <= n->count; pass ++) {
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

    for (i = 0; growing && i < n->count; i ++) {
        const box_layout_node_t *c = &nodes[n->first + i];
        result += box_layout_grows(c, main) ? c->size[main] : 0;
    }

    return result;
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

/** Narrow the children of a row or column that overflows it, down to their least size.
 *
 * The widest children narrow first to a common width, as Clay compresses its largest elements
 * first; a child that reaches its least size keeps it and the others narrow further.
 *
 * @param nodes The nodes of the tree
 * @param n The parent
 * @param main The main axis of the parent
 * @param space The size of the content box on the main axis
 * @param total The size of the children and the spacing between them
 */
static void box_layout_shrink(
    box_layout_node_t *nodes,
    const box_layout_node_t *n,
    int32_t main,
    float space,
    float total)
{
    float gaps = n->count > 1 ? n->layout.spacing * (float)(n->count - 1) : 0;
    float avail = space - gaps;
    int32_t i, pass, open = 0;
    for (i = 0; total > space + 1e-4f && i < n->count; i ++) {
        box_layout_node_t *c = &nodes[n->first + i];
        c->frozen = !(c->size[main] > box_layout_least(c, main));
        avail -= c->frozen ? c->size[main] : 0;
        open += !c->frozen;
    }

    for (pass = 0; pass <= n->count && open; pass ++) {
        float cap = avail / (float)open, over = 0;
        for (i = 0; i < n->count; i ++) {
            const box_layout_node_t *c = &nodes[n->first + i];
            float least = box_layout_least(c, main);
            float v = fminf(c->size[main], fmaxf(cap, least));
            over += c->frozen ? 0 : v - cap;
        }

        for (i = 0; i < n->count; i ++) {
            box_layout_node_t *c = &nodes[n->first + i];
            float least = box_layout_least(c, main);
            float v = fminf(c->size[main], fmaxf(cap, least));
            bool keep = (over > 0 && v > cap) || (over < 0 && v < cap);
            if (!c->frozen && (keep || fabsf(over) < 1e-4f)) {
                c->size[main] = v;
                c->frozen = keep;
                avail -= keep ? v : 0;
                open -= keep;
            }
        }

        if (fabsf(over) < 1e-4f) {
            break;
        }
    }
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
 * Along an axis the element scrolls the children keep their fit size. Floating children are
 * sized by box_layout_resolve_floats.
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
            float space = hi[axis] - lo[axis];
            if (axis == main || box_layout_scrolls(n, axis)) {
                continue;
            }

            if (box_layout_grows(c, axis)) {
                c->size[axis] = box_layout_grow_clamp(c, axis, space);
            } else if (c->size[axis] > space) {
                c->size[axis] = fmaxf(space, box_layout_least(c, axis));
            }
        }
    }

    if (main >= first && main < last && !box_layout_scrolls(n, main)) {
        float total = box_layout_grow(nodes, n, main, hi[main] - lo[main]);
        box_layout_shrink(nodes, n, main, hi[main] - lo[main], total);
    }
}

/** Get the content box of a scrolling element on one axis, extended past its high edge on x and
 * its low edge on y when the children overflow it, so overflowing content starts at the left
 * or top edge.
 *
 * @param n The element
 * @param axis 0 for x, 1 for y
 * @param size Size of the children on the axis
 * @param lo The low edge, extended on y
 * @param hi The high edge, extended on x
 */
static void box_layout_overflow(
    const box_layout_node_t *n,
    int32_t axis,
    float size,
    float *lo,
    float *hi)
{
    if (box_layout_scrolls(n, axis) && size > *hi - *lo) {
        if (axis) {
            *lo = *hi - size;
        } else {
            *hi = *lo + size;
        }
    }
}

/** Measure the content of a scrolling element from the sizes its children got, clamp its offset
 * to the content and shift its children by it.
 *
 * @param nodes The nodes of the tree
 * @param n The element, its children placed
 */
static void box_layout_scroll(
    box_layout_node_t *nodes,
    box_layout_node_t *n)
{
    int32_t main = n->layout.direction == DirectionRow ? 0 :
        n->layout.direction == DirectionColumn ? 1 : -1;
    const Padding *pad = &n->layout.padding;
    float edges[2] = { pad->left + pad->right, pad->top + pad->bottom };
    float want[2] = { n->scroll.x, n->scroll.y };
    int32_t i, axis;
    for (axis = 0; axis < 2; axis ++) {
        float fit = 0;
        for (i = 0; i < n->count; i ++) {
            const box_layout_node_t *c = &nodes[n->first + i];
            float v = main == -1 ? box_layout_extent(c, axis, c->size[axis]) : c->size[axis];
            fit = axis == main ? fit + v : fmaxf(fit, v);
        }

        if (axis == main && n->count > 1) {
            fit += n->layout.spacing * (float)(n->count - 1);
        }

        float max = fmaxf(fit + edges[axis] - n->size[axis], 0);
        n->content[axis] = fit + edges[axis];
        n->offset[axis] = isfinite(want[axis]) ? fminf(fmaxf(want[axis], 0), max) : 0;
    }

    for (i = 0; i < n->count; i ++) {
        box_layout_node_t *c = &nodes[n->first + i];
        if (c->placed) {
            c->at[0] -= n->offset[0];
            c->at[1] += n->offset[1];
        }
    }
}

/** Place the children of an element whose children are sized.
 *
 * Floating children are placed by box_layout_floats once the tree is written.
 *
 * @param nodes The nodes of the tree
 * @param n The element
 */
static void box_layout_place(
    box_layout_node_t *nodes,
    box_layout_node_t *n)
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
            c->placed = c->has_item || !c->has_pos;
            if (c->has_item) {
                c->at[0] = box_layout_align(lo[0], hi[0], c->size[0], c->item.h, 0) + c->item.x;
                c->at[1] = box_layout_align(lo[1], hi[1], c->size[1], c->item.v, 1) + c->item.y;
            } else {
                c->at[0] = 0;
                c->at[1] = 0;
            }
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

    box_layout_overflow(n, main, total, &lo[main], &hi[main]);
    float space = hi[main] - lo[main];
    float cursor = box_layout_align(0, space, total, align[main], 0) - total / 2;
    for (i = 0; i < n->count; i ++) {
        box_layout_node_t *c = &nodes[n->first + i];
        float center = cursor + c->size[main] / 2;
        float clo = lo[cross], chi = hi[cross];
        box_layout_overflow(n, cross, c->size[cross], &clo, &chi);
        c->placed = true;
        c->at[main] = main ? hi[1] - center : lo[0] + center;
        c->at[cross] = box_layout_align(clo, chi, c->size[cross], align[cross], cross);
        cursor += c->size[main] + n->layout.spacing;
    }

done:
    if (n->has_scroll) {
        box_layout_scroll(nodes, n);
    }
}

/** Give the texts of a tree their resolved widths and take the heights they get.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param nodes The nodes of the tree
 * @param count Number of nodes
 */
static void box_layout_wrap(
    ecs_world_t *world,
    const box_layout_state_t *state,
    box_layout_node_t *nodes,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_layout_node_t *n = &nodes[i];
        if (n->has_text) {
            n->shape[1] = state->text.wrap(world, n->e, n->size[0]);
        }
    }
}

/** Get the size of the anchor of a floating element on one axis.
 *
 * The anchor is its parent when it names none or a deleted one, the node of the anchor when it
 * is in the tree, else the LayoutBounds or Rect of the anchor.
 *
 * @param world The world
 * @param nodes The nodes of the tree
 * @param count Number of nodes
 * @param f The floating element
 * @param axis 0 for x, 1 for y
 * @return The size
 */
static float box_layout_anchor_size(
    const ecs_world_t *world,
    const box_layout_node_t *nodes,
    int32_t count,
    const box_layout_node_t *f,
    int32_t axis)
{
    ecs_entity_t anchor = f->fl.anchor;
    float result = nodes[f->parent].size[axis];
    int32_t i;
    if (!anchor || anchor == f->e || !ecs_is_alive(world, anchor)) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        if (nodes[i].e == anchor) {
            result = nodes[i].size[axis];
            goto done;
        }
    }

    const LayoutBounds *lb = ecs_get(world, anchor, LayoutBounds);
    const Rect *r = lb ? NULL : ecs_get(world, anchor, Rect);
    result = lb ? (axis ? lb->height : lb->width) : r ? (axis ? r->height : r->width) : 0;
done:
    return result;
}

/** Size the children of the elements of a tree on a range of axes: first the elements outside
 * floating elements, then every floating element in tree order, whose grow axes take the size of
 * its anchor, and the elements below it.
 *
 * @param world The world
 * @param nodes The nodes of the tree, measured on the axes
 * @param count Number of nodes
 * @param first First axis, 0 for x, 1 for y
 * @param last Axis after the last one
 */
static void box_layout_resolve_all(
    const ecs_world_t *world,
    box_layout_node_t *nodes,
    int32_t count,
    int32_t first,
    int32_t last)
{
    int32_t i, f, axis;
    for (i = 0; i < count; i ++) {
        if (nodes[i].has_layout && nodes[i].float_root == -1) {
            box_layout_resolve(nodes, &nodes[i], first, last);
        }
    }

    for (f = 0; f < count; f ++) {
        box_layout_node_t *n = &nodes[f];
        if (!n->is_float) {
            continue;
        }

        for (axis = first; axis < last; axis ++) {
            n->anchor[axis] = box_layout_anchor_size(world, nodes, count, n, axis);
            if (box_layout_grows(n, axis)) {
                n->size[axis] = box_layout_grow_clamp(n, axis, n->anchor[axis]);
            }
        }

        for (i = f; i < count; i ++) {
            if (nodes[i].has_layout && nodes[i].float_root == f) {
                box_layout_resolve(nodes, &nodes[i], first, last);
            }
        }
    }
}

/** Write the computed size and position of an element where they changed, the content of a
 * scrolling element and the anchor size of a floating one.
 *
 * The layout runs immediately, so the observers of an earlier write run before this one: an
 * element they deleted, such as the scroll bar a scroll view removes when its content fits, is
 * skipped.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param n The element
 */
static void box_layout_write(
    ecs_world_t *world,
    box_layout_state_t *state,
    const box_layout_node_t *n)
{
    if (!ecs_is_alive(world, n->e)) {
        goto done;
    }

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
            state->writing_rect = n->e;
            ecs_set(world, n->e, Rect, { n->size[0], n->size[1] });
            state->writing_rect = 0;
        }
    }

    if (n->has_scroll) {
        const LayoutContent *c = ecs_get(world, n->e, LayoutContent);
        LayoutContent v = {
            .width = n->content[0], .height = n->content[1],
            .max_x = fmaxf(n->content[0] - n->size[0], 0),
            .max_y = fmaxf(n->content[1] - n->size[1], 0),
            .x = n->offset[0], .y = n->offset[1]
        };
        if (!c || ecs_os_memcmp_t(c, &v, LayoutContent)) {
            ecs_set_ptr(world, n->e, LayoutContent, &v);
        }

        *ecs_map_ensure(&state->scrolled, (ecs_map_key_t)n->e) = (ecs_map_val_t)(4u |
            (n->scroll.horizontal ? 1u : 0u) | (n->scroll.vertical ? 2u : 0u));
    }

    if (n->is_float) {
        uint32_t w, h;
        ecs_os_memcpy(&w, &n->anchor[0], ECS_SIZEOF(uint32_t));
        ecs_os_memcpy(&h, &n->anchor[1], ECS_SIZEOF(uint32_t));
        *ecs_map_ensure(&state->anchor_sizes, (ecs_map_key_t)n->e) =
            ((ecs_map_val_t)w << 32) | h;
    }

done:
    return;
}

/** Size and place a tree without texts, both axes in one pass.
 *
 * @param world The world
 * @param nodes The nodes of the tree
 * @param count Number of nodes
 */
static void box_layout_solve(
    const ecs_world_t *world,
    box_layout_node_t *nodes,
    int32_t count)
{
    int32_t i;
    for (i = count - 1; i >= 0; i --) {
        box_layout_measure(nodes, &nodes[i], 0, 2);
    }

    box_layout_resolve_all(world, nodes, count, 0, 2);
    for (i = 0; i < count; i ++) {
        if (nodes[i].has_layout) {
            box_layout_place(nodes, &nodes[i]);
        }
    }
}

/** Size and place a tree with texts: widths, then the texts wrap, then heights.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param nodes The nodes of the tree
 * @param count Number of nodes
 */
static void box_layout_solve_text(
    ecs_world_t *world,
    box_layout_state_t *state,
    box_layout_node_t *nodes,
    int32_t count)
{
    int32_t i, axis;
    for (axis = 0; axis < 2; axis ++) {
        if (axis == 1) {
            state->writing = true;
            box_layout_wrap(world, state, nodes, count);
            state->writing = false;
        }

        for (i = count - 1; i >= 0; i --) {
            box_layout_measure(nodes, &nodes[i], axis, axis + 1);
        }

        box_layout_resolve_all(world, nodes, count, axis, axis + 1);
    }

    for (i = 0; i < count; i ++) {
        if (nodes[i].has_layout) {
            box_layout_place(nodes, &nodes[i]);
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
    int32_t i, count = ecs_vec_count(&state->nodes);
    for (i = 0; i < ecs_vec_count(&state->untagged); i ++) {
        ecs_add_id(world, ecs_vec_get_t(&state->untagged, ecs_entity_t, i)[0], state->child_tag);
    }

    if (ecs_vec_count(&state->untagged)) {
        box_layout_observe_children(world, state);
    }

    if (state->has_text) {
        box_layout_solve_text(world, state, nodes, count);
    } else {
        box_layout_solve(world, nodes, count);
    }

    state->writing = true;
    for (i = 0; i < count; i ++) {
        box_layout_write(world, state, &nodes[i]);
    }

    state->writing = false;
}
