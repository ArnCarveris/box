/**
 * @file ui/layout.c
 * @brief Sizes the nodes of a UI tree and places them in rows and columns.
 *
 * The pass runs over the places the walk collected, which are in pre-order.
 * Measuring goes backward over them, so a node is sized after the children that
 * size it, and placing goes forward, so a node is placed after the parent that
 * places it and the row of that parent is still where its earlier children left
 * it. Everything is worked out in the pixels of the view, and a node that the
 * layout placed gets the Position that difference asks for, which is what the
 * transform of the frame and the extract after it read.
 *
 * The places the pass runs over are those of a whole tree or those of one
 * subtree of it, which a change that cannot resize the box of that subtree
 * asks for. The node the walk started at keeps the box and the place the pass
 * that laid out its parent gave it, so the rest of the tree stays where it is.
 */

#include "ui_internal.h"

/** Read the components the layout of a node needs into its place.
 *
 * The size of the node is the size of its Layout, and a side of that size that
 * is zero takes the side of the Rect of the node, which leaves the sides the
 * content has to fill at zero.
 *
 * @param world The world
 * @param p The place of the node
 * @param shadows The world holds box shadows, so a node may cast one
 */
static void box_ui_read(
    ecs_world_t *world,
    UiPlace *p,
    bool shadows)
{
    const Layout *l = ecs_get(world, p->e, Layout);
    const Anchor *a = ecs_get(world, p->e, Anchor);
    const Rect *r = ecs_get(world, p->e, Rect);
    const Position *pos = ecs_get(world, p->e, Position);

    p->has_layout = l != NULL;
    p->has_anchor = a != NULL;
    p->has_rect = r != NULL;
    p->has_shadow = shadows && ecs_has(world, p->e, BoxShadow);
    if (l) {
        p->layout = *l;
        p->width = l->width;
        p->height = l->height;
    }

    if (a) {
        p->anchor = *a;
    }

    if (pos) {
        p->local_x = pos->x;
        p->local_y = pos->y;
    }

    if (r && p->width == 0) {
        p->width = r->width;
    }

    if (r && p->height == 0) {
        p->height = r->height;
    }

    if (p->fill_width > 0) {
        p->width = p->fill_width;
    }

    if (p->fill_height > 0) {
        p->height = p->fill_height;
    }
}

/** Box a Fill sizes a child against: the box a Fill already gave the parent,
 * an explicit Layout or Rect of the parent, or, for the root of a tree that
 * has neither, the view that draws it, inside the padding of the parent when
 * it has a Layout.
 *
 * @param world The world
 * @param root The root of the tree being walked
 * @param parent The place of the parent the Fill of a child resolves against
 * @param width Receives the width of the box, 0 when none is known
 * @param height Receives the height of the box, 0 when none is known
 */
static void box_ui_fill_box(
    ecs_world_t *world,
    ecs_entity_t root,
    const UiPlace *parent,
    float *width,
    float *height)
{
    const Layout *l = ecs_get(world, parent->e, Layout);
    const Rect *r = ecs_get(world, parent->e, Rect);
    const UiImpl *impl = parent->e == root ? ecs_get(world, root, UiImpl) : NULL;
    float pad = l ? l->padding * 2 : 0;

    *width = parent->fill_width > 0 ? parent->fill_width :
        (l && l->width > 0 ? l->width : (r ? r->width : (impl ? impl->width : 0)));
    *height = parent->fill_height > 0 ? parent->fill_height :
        (l && l->height > 0 ? l->height : (r ? r->height : (impl ? impl->height : 0)));
    if (*width > 0) {
        *width -= pad;
    }

    if (*height > 0) {
        *height -= pad;
    }
}

/** Give every node with a Fill the box its fraction of its parent works out to.
 *
 * The places of a tree are in pre-order, so a parent always comes before the
 * children a forward walk gives their fraction to, which is what lets a child
 * of a child fill a box its own parent just resolved.
 *
 * @param world The world
 * @param root The root of the tree being walked
 */
static void box_ui_resolve_fill(
    ecs_world_t *world,
    ecs_entity_t root)
{
    UiState *state = box_ui_state(world);
    int32_t i, count = ecs_vec_count(&state->places);
    for (i = 0; i < count; i ++) {
        UiPlace *p = ecs_vec_get_t(&state->places, UiPlace, i);
        const UiPlace *parent;
        const Fill *fill;
        float pw, ph;
        if (p->aux || p->parent < 0) {
            continue;
        }

        fill = ecs_get(world, p->e, Fill);
        if (!fill || (fill->width <= 0 && fill->height <= 0)) {
            continue;
        }

        parent = ecs_vec_get_t(&state->places, UiPlace, p->parent);
        box_ui_fill_box(world, root, parent, &pw, &ph);
        if (fill->width > 0 && pw > 0) {
            p->fill_width = pw * fill->width;
        }

        if (fill->height > 0 && ph > 0) {
            p->fill_height = ph * fill->height;
        }
    }
}

/** Give the sides of a box that are still zero the size of its content.
 *
 * @param p The place of the node
 */
static void box_ui_fit(
    UiPlace *p)
{
    if (!p->has_layout) {
        goto done;
    }

    const Layout *l = &p->layout;
    float pad = l->padding * 2;
    float gap = p->flow > 1 ? l->spacing * (float)(p->flow - 1) : 0;
    if (p->width == 0) {
        p->width = pad + (l->direction == DirectionRow ?
            p->sum_width + gap : p->max_width);
    }

    if (p->height == 0) {
        p->height = pad + (l->direction == DirectionColumn ?
            p->sum_height + gap : p->max_height);
    }

done:
    return;
}

/** Add the box of a child to the content of its parent.
 *
 * @param parent The place of the parent
 * @param child The place of the child
 */
static void box_ui_add(
    UiPlace *parent,
    const UiPlace *child)
{
    parent->sum_width += child->width;
    parent->sum_height += child->height;
    parent->flow ++;
    if (child->width > parent->max_width) {
        parent->max_width = child->width;
    }

    if (child->height > parent->max_height) {
        parent->max_height = child->height;
    }
}

/** X of a box of a given width against an edge of a box of the given width.
 *
 * @param parent_x X of the centre of the box of the parent
 * @param parent_width Width of the box of the parent
 * @param pad Padding that insets the box of the parent
 * @param width Width of the box that is aligned
 * @param h Edge of the parent the box is aligned against
 * @return X of the centre of the aligned box
 */
static float box_ui_align_x(
    float parent_x,
    float parent_width,
    float pad,
    float width,
    Align h)
{
    float half = parent_width * 0.5f - pad - width * 0.5f;
    float result = parent_x;
    if (h == AlignLeft) {
        result = parent_x - half;
    } else if (h == AlignRight) {
        result = parent_x + half;
    }

    return result;
}

/** Y of a box of a given height against an edge of a box of the given height.
 *
 * @param parent_y Y of the centre of the box of the parent
 * @param parent_height Height of the box of the parent
 * @param pad Padding that insets the box of the parent
 * @param height Height of the box that is aligned
 * @param v Edge of the parent the box is aligned against
 * @return Y of the centre of the aligned box
 */
static float box_ui_align_y(
    float parent_y,
    float parent_height,
    float pad,
    float height,
    VAlign v)
{
    float half = parent_height * 0.5f - pad - height * 0.5f;
    float result = parent_y;
    if (v == VAlignTop) {
        result = parent_y + half;
    } else if (v == VAlignBottom) {
        result = parent_y - half;
    }

    return result;
}

/** Put the cursor of a layout where its first child goes.
 *
 * The cursor is the edge the next child starts at, so the whole group of
 * children is moved by the alignment along the axis of the layout.
 *
 * @param p The place of the node
 */
static void box_ui_start(
    UiPlace *p)
{
    const Layout *l = &p->layout;
    float gap = p->flow > 1 ? l->spacing * (float)(p->flow - 1) : 0;
    if (l->direction == DirectionRow) {
        float total = p->sum_width + gap;

        p->cursor = p->x - p->width * 0.5f + l->padding;
        if (l->h == AlignCenter) {
            p->cursor = p->x - total * 0.5f;
        } else if (l->h == AlignRight) {
            p->cursor = p->x + p->width * 0.5f - l->padding - total;
        }
    } else {
        float total = p->sum_height + gap;

        p->cursor = p->y + p->height * 0.5f - l->padding;
        if (l->v == VAlignMiddle) {
            p->cursor = p->y + total * 0.5f;
        } else if (l->v == VAlignBottom) {
            p->cursor = p->y - p->height * 0.5f + l->padding + total;
        }
    }
}

/** Place a child at the cursor of the layout of its parent and move it on.
 *
 * @param parent The place of the parent
 * @param child The place of the child
 */
static void box_ui_flow(
    UiPlace *parent,
    UiPlace *child)
{
    const Layout *l = &parent->layout;
    if (l->direction == DirectionRow) {
        child->x = parent->cursor + child->width * 0.5f;
        child->y = box_ui_align_y(parent->y, parent->height, l->padding,
            child->height, l->v);
        parent->cursor += child->width + l->spacing;
    } else {
        child->y = parent->cursor - child->height * 0.5f;
        child->x = box_ui_align_x(parent->x, parent->width, l->padding,
            child->width, l->h);
        parent->cursor -= child->height + l->spacing;
    }
}

/** Pin a child against the edges of its parent its anchor names.
 *
 * A parent that is the root of a tree and has neither a Layout nor a Rect of
 * its own has no box of its own either: the edges an anchored child of such a
 * root pins against are those of the view that draws the tree instead, which
 * does not touch the Bounds the root itself publishes.
 *
 * @param world The world
 * @param root The root of the tree being walked
 * @param parent The place of the parent
 * @param child The place of the child
 */
static void box_ui_pin(
    ecs_world_t *world,
    ecs_entity_t root,
    const UiPlace *parent,
    UiPlace *child)
{
    const Anchor *a = &child->anchor;
    float pad = parent->has_layout ? parent->layout.padding : 0;
    float pw = parent->width;
    float ph = parent->height;
    if (pw <= 0 && ph <= 0 && parent->e == root) {
        const UiImpl *impl = ecs_get(world, root, UiImpl);
        if (impl) {
            pw = impl->width;
            ph = impl->height;
        }
    }

    child->x = box_ui_align_x(parent->x, pw, pad, child->width, a->h);
    child->y = box_ui_align_y(parent->y, ph, pad, child->height, a->v);
    child->x += a->h == AlignRight ? -a->x : a->x;
    child->y += a->v == VAlignTop ? -a->y : a->y;
}

/** Give a node the position the layout worked out for it.
 *
 * A node the layout did not move keeps the position it was authored with, and
 * one that has none is given the origin of its parent, because the transform of
 * a node is only written when its Position is set: a node without one keeps the
 * zero matrix its WorldTransform was created with and its subtree collapses.
 *
 * @param world The world
 * @param e The node
 * @param x X relative to the parent of the node
 * @param y Y relative to the parent of the node
 */
static void box_ui_move(
    ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float y)
{
    Position *pos = ecs_get_mut(world, e, Position);
    if (!pos) {
        ecs_set(world, e, Position, { x, y, 0 });
        goto done;
    }

    if (pos->x == x && pos->y == y) {
        goto done;
    }

    pos->x = x;
    pos->y = y;
    ecs_modified(world, e, Position);
done:
    return;
}

/** Give the rectangle of a node the size of the box its layout worked out.
 *
 * @param world The world
 * @param e The node
 * @param width Width of the box
 * @param height Height of the box
 */
static void box_ui_resize(
    ecs_world_t *world,
    ecs_entity_t e,
    float width,
    float height)
{
    Rect *r = ecs_get_mut(world, e, Rect);
    if (!r || (r->width == width && r->height == height)) {
        goto done;
    }

    r->width = width;
    r->height = height;
    ecs_modified(world, e, Rect);
done:
    return;
}

/** Publish the box of a node as the Bounds authors read.
 *
 * @param world The world
 * @param p The place of the node
 */
static void box_ui_bounds(
    ecs_world_t *world,
    const UiPlace *p)
{
    Bounds *b = ecs_get_mut(world, p->e, Bounds);
    if (!b) {
        ecs_set(world, p->e, Bounds, {
            .x = p->x, .y = p->y, .width = p->width, .height = p->height
        });
        goto done;
    }

    b->x = p->x;
    b->y = p->y;
    b->width = p->width;
    b->height = p->height;
done:
    return;
}

/** Size every collected node, the children of a node before the node itself.
 *
 * @param world The world
 */
static void box_ui_measure(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    int32_t i;
    bool texts = state->texts != 0;
    bool shadows = state->shadows != 0;
    for (i = ecs_vec_count(&state->places) - 1; i >= 0; i --) {
        UiPlace *p = ecs_vec_get_t(&state->places, UiPlace, i);
        if (p->aux) {
            continue;
        }

        box_ui_read(world, p, shadows);
        if (texts) {
            box_ui_text_measure(world, p);
        }

        box_ui_fit(p);
        if (p->parent >= 0 && !p->has_anchor) {
            box_ui_add(ecs_vec_get_t(&state->places, UiPlace, p->parent), p);
        }
    }
}

/** Place every collected node, a node before the nodes below it.
 *
 * The node without a parent is the one the collect started at. It is the root
 * of the tree when the whole tree is laid out, which sits where its own
 * Position puts it, and the root of a subtree otherwise, which keeps the place
 * in the tree that the pass that laid out its parent gave it.
 *
 * @param world The world
 * @param root The root of the tree being walked
 */
static void box_ui_arrange(
    ecs_world_t *world,
    ecs_entity_t root)
{
    UiState *state = box_ui_state(world);
    int32_t i, count = ecs_vec_count(&state->places);
    for (i = 0; i < count; i ++) {
        UiPlace *p = ecs_vec_get_t(&state->places, UiPlace, i);
        UiPlace *parent = p->parent >= 0 ?
            ecs_vec_get_t(&state->places, UiPlace, p->parent) : NULL;
        bool placed = false;
        if (p->aux) {
            continue;
        }

        const Bounds *b = !parent && p->e != root ?
            ecs_get(world, p->e, Bounds) : NULL;
        if (b) {
            p->x = b->x;
            p->y = b->y;
        } else if (!parent) {
            p->x = p->local_x;
            p->y = p->local_y;
        } else if (p->has_anchor) {
            box_ui_pin(world, root, parent, p);
            placed = true;
        } else if (parent->has_layout &&
            parent->layout.direction != DirectionNone)
        {
            box_ui_flow(parent, p);
            placed = true;
        } else {
            p->x = parent->x + p->local_x;
            p->y = parent->y + p->local_y;
        }

        if (placed) {
            box_ui_move(world, p->e, p->x - parent->x, p->y - parent->y);
        } else {
            box_ui_move(world, p->e, p->local_x, p->local_y);
        }

        if (p->has_layout) {
            box_ui_start(p);
            if (p->has_rect) {
                box_ui_resize(world, p->e, p->width, p->height);
            }
        }

        box_ui_bounds(world, p);
    }
}

void box_ui_layout(
    ecs_world_t *world,
    ecs_entity_t root)
{
    box_ui_resolve_fill(world, root);
    box_ui_measure(world);
    box_ui_arrange(world, root);
    box_ui_text_build(world);
    box_ui_shadow_build(world);
    box_ui_group_build(world, root);
}
