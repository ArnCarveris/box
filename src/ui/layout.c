/**
 * @file ui/layout.c
 * @brief Sizes the nodes of a UI tree and places them in rows and columns.
 *
 * Measures backward over the pre-order places of the walk and places forward, in
 * view pixels, writing the Position of every placed node. The places are a whole tree
 * or one subtree whose box a change cannot resize.
 *
 * Trees with growing elements or parent width text wrap run three extra passes:
 * widths down, heights up and grown heights down.
 */

#include "ui_internal.h"

/** One side of the padding of a Layout, which takes the padding when it is zero.
 *
 * @param side The padding of the side
 * @param all The padding of every side
 * @return The padding of the side
 */
static float box_ui_side(
    float side,
    float all)
{
    return side != 0 ? side : all;
}

/** Bound a size by a minimum and a maximum, either of which is 0 for none.
 *
 * @param size The size
 * @param min The smallest size, 0 for none
 * @param max The largest size, 0 for none
 * @return The bounded size
 */
static float box_ui_bound(
    float size,
    float min,
    float max)
{
    float result = size;
    if (max > 0 && result > max) {
        result = max;
    }

    if (min > 0 && result < min) {
        result = min;
    }

    return result;
}

/** Read the components the layout of a node needs into its place.
 *
 * The size comes from Layout; a zero side takes the side of Rect. A Rect the layout
 * wrote itself, or a side the node grows on, gives no side, so the content is measured
 * again.
 *
 * @param world The world
 * @param p The place of the node
 * @param shadows The world holds box shadows, so a node may cast one
 * @param grows The world holds elements that grow, so a node may be one
 */
static void box_ui_read(
    ecs_world_t *world,
    UiPlace *p,
    bool shadows,
    bool grows)
{
    const Layout *l = ecs_get(world, p->e, Layout);
    const Anchor *a = ecs_get(world, p->e, Anchor);
    const WorldAnchor *wa = ecs_get(world, p->e, WorldAnchor);
    const Rect *r = ecs_get(world, p->e, Rect);
    const Position *pos = ecs_get(world, p->e, Position);

    p->has_layout = l != NULL;
    p->has_anchor = a != NULL;
    p->has_world = wa != NULL;
    p->has_rect = r != NULL;
    p->has_shadow = shadows && ecs_has(world, p->e, BoxShadow);
    if (l) {
        p->layout = *l;
        p->width = l->width;
        p->height = l->height;
        p->pad_left = box_ui_side(l->padding_left, l->padding);
        p->pad_right = box_ui_side(l->padding_right, l->padding);
        p->pad_top = box_ui_side(l->padding_top, l->padding);
        p->pad_bottom = box_ui_side(l->padding_bottom, l->padding);
    }

    const Grow *g = grows ? ecs_get(world, p->e, Grow) : NULL;
    if (g) {
        p->grow_width = g->width > 0 ? g->width : 0;
        p->grow_height = g->height > 0 ? g->height : 0;
    }

    if (a) {
        p->anchor = *a;
    }

    if (wa) {
        p->world = *wa;
    }

    if (pos) {
        p->local_x = pos->x;
        p->local_y = pos->y;
    }

    bool content = l && ecs_has_id(world, p->e, UiContentRect);
    if (r && !content && p->width == 0 && p->grow_width == 0) {
        p->width = r->width;
    }

    if (r && !content && p->height == 0 && p->grow_height == 0) {
        p->height = r->height;
    }

    if (p->fill_width > 0) {
        p->width = p->fill_width;
    }

    if (p->fill_height > 0) {
        p->height = p->fill_height;
    }
}

/** Box a Fill sizes a child against.
 *
 * The box a Fill gave the parent, else its Layout or Rect, else for a tree root the
 * view that draws it, inside the padding of the parent when it has a Layout.
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
    float pad_x = l ? box_ui_side(l->padding_left, l->padding) +
        box_ui_side(l->padding_right, l->padding) : 0;
    float pad_y = l ? box_ui_side(l->padding_top, l->padding) +
        box_ui_side(l->padding_bottom, l->padding) : 0;

    *width = parent->fill_width > 0 ? parent->fill_width :
        (l && l->width > 0 ? l->width : (r ? r->width : (impl ? impl->width : 0)));
    *height = parent->fill_height > 0 ? parent->fill_height :
        (l && l->height > 0 ? l->height : (r ? r->height : (impl ? impl->height : 0)));
    if (*width > 0) {
        *width -= pad_x;
    }

    if (*height > 0) {
        *height -= pad_y;
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
 * The size the content asks for is kept whether the box took it or not, so an
 * element that clips what it holds knows how much of it does not fit. The box
 * is then bounded by the minimum and the maximum of its Layout.
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
    float gap = p->flow > 1 ? l->spacing * (float)(p->flow - 1) : 0;
    float width = p->pad_left + p->pad_right + (l->direction == DirectionRow ?
        p->sum_width + gap : p->widest_child);
    float height = p->pad_top + p->pad_bottom + (l->direction == DirectionColumn ?
        p->sum_height + gap : p->tallest_child);
    if (width > p->content_width) {
        p->content_width = width;
    }

    if (height > p->content_height) {
        p->content_height = height;
    }

    if (p->width == 0) {
        p->width = width;
        p->fit_width = true;
    }

    if (p->height == 0) {
        p->height = height;
        p->fit_height = true;
    }

    p->width = box_ui_bound(p->width, p->layout.min_width, p->layout.max_width);
    p->height = box_ui_bound(p->height, p->layout.min_height, p->layout.max_height);
done:
    return;
}

/** Move a child of a scrolled element by the offset of that element.
 *
 * An anchored child is pinned to the box of its parent and is not part of what
 * the parent scrolls, so it stays where the pin put it.
 *
 * @param parent The place of the parent
 * @param child The place of the child
 */
static void box_ui_scrolled(
    const UiPlace *parent,
    UiPlace *child)
{
    if (!parent->has_scroll || child->has_anchor || child->has_world) {
        goto done;
    }

    if (parent->scroll.x != OverflowVisible) {
        child->x -= parent->scroll.offset.x;
    }

    if (parent->scroll.y != OverflowVisible) {
        child->y += parent->scroll.offset.y;
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
    if (child->shrinks) {
        parent->shrinks = true;
    }

    parent->sum_width += child->width;
    parent->sum_height += child->height;
    parent->flow ++;
    if (child->width > parent->widest_child) {
        parent->widest_child = child->width;
    }

    if (child->height > parent->tallest_child) {
        parent->tallest_child = child->height;
    }
}

/** X of a box of a given width against an edge of a box of the given width.
 *
 * @param parent The place of the parent, whose padding insets its box
 * @param parent_width Width of the box of the parent
 * @param width Width of the box that is aligned
 * @param h Edge of the parent the box is aligned against
 * @return X of the centre of the aligned box
 */
static float box_ui_align_x(
    const UiPlace *parent,
    float parent_width,
    float width,
    Align h)
{
    float half = parent_width * 0.5f - width * 0.5f;
    float result = parent->x + (parent->pad_left - parent->pad_right) * 0.5f;
    if (h == AlignLeft) {
        result = parent->x - half + parent->pad_left;
    } else if (h == AlignRight) {
        result = parent->x + half - parent->pad_right;
    }

    return result;
}

/** Y of a box of a given height against an edge of a box of the given height.
 *
 * @param parent The place of the parent, whose padding insets its box
 * @param parent_height Height of the box of the parent
 * @param height Height of the box that is aligned
 * @param v Edge of the parent the box is aligned against
 * @return Y of the centre of the aligned box
 */
static float box_ui_align_y(
    const UiPlace *parent,
    float parent_height,
    float height,
    VAlign v)
{
    float half = parent_height * 0.5f - height * 0.5f;
    float result = parent->y + (parent->pad_bottom - parent->pad_top) * 0.5f;
    if (v == VAlignTop) {
        result = parent->y + half - parent->pad_top;
    } else if (v == VAlignBottom) {
        result = parent->y - half + parent->pad_bottom;
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

        p->cursor = p->x - p->width * 0.5f + p->pad_left;
        if (l->h == AlignCenter) {
            p->cursor = p->x + (p->pad_left - p->pad_right) * 0.5f - total * 0.5f;
        } else if (l->h == AlignRight) {
            p->cursor = p->x + p->width * 0.5f - p->pad_right - total;
        }
    } else {
        float total = p->sum_height + gap;

        p->cursor = p->y + p->height * 0.5f - p->pad_top;
        if (l->v == VAlignMiddle) {
            p->cursor = p->y + (p->pad_bottom - p->pad_top) * 0.5f + total * 0.5f;
        } else if (l->v == VAlignBottom) {
            p->cursor = p->y - p->height * 0.5f + p->pad_bottom + total;
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
        child->y = box_ui_align_y(parent, parent->height, child->height, l->v);
        parent->cursor += child->width + l->spacing;
    } else {
        child->y = parent->cursor - child->height * 0.5f;
        child->x = box_ui_align_x(parent, parent->width, child->width, l->h);
        parent->cursor -= child->height + l->spacing;
    }
}

/** Coordinate of the point of a box an attachment names, on one axis.
 *
 * @param centre Centre of the box on the axis
 * @param size Size of the box on the axis
 * @param point 1 for the low edge, 2 for the middle and 3 for the high edge
 * @return The coordinate of the point
 */
static float box_ui_attach_point(
    float centre,
    float size,
    int32_t point)
{
    float result = centre;
    if (point == 1) {
        result = centre - size * 0.5f;
    } else if (point == 3) {
        result = centre + size * 0.5f;
    }

    return result;
}

/** Pin a child against the edges of its parent its anchor names.
 *
 * A tree root without Layout or Rect pins against the view that draws the tree
 * instead, leaving the Bounds of the root untouched. An axis with an attach point puts
 * the edge of the child on that point of the parent box.
 *
 * The padding of the parent insets the edges, except on an axis the child grows
 * on, where it was given the whole box and covers the padding.
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
    UiPlace edges = *parent;
    float pw = parent->width;
    float ph = parent->height;
    if (pw <= 0 && ph <= 0 && parent->e == root) {
        const UiImpl *impl = ecs_get(world, root, UiImpl);
        if (impl) {
            pw = impl->width;
            ph = impl->height;
        }
    }

    if (child->grow_width > 0) {
        edges.pad_left = 0;
        edges.pad_right = 0;
    }

    if (child->grow_height > 0) {
        edges.pad_top = 0;
        edges.pad_bottom = 0;
    }

    child->x = box_ui_align_x(&edges, pw, child->width, a->h);
    child->y = box_ui_align_y(&edges, ph, child->height, a->v);
    child->x += a->h == AlignRight ? -a->x : a->x;
    child->y += a->v == VAlignTop ? -a->y : a->y;
    if (a->parent_h != AttachHSame) {
        float edge = a->h == AlignLeft ? 1.0f : (a->h == AlignRight ? -1.0f : 0);
        child->x = box_ui_attach_point(parent->x, pw, (int32_t)a->parent_h) +
            edge * child->width * 0.5f + a->x;
    }

    if (a->parent_v != AttachVSame) {
        float edge = a->v == VAlignTop ? -1.0f : (a->v == VAlignBottom ? 1.0f : 0);
        child->y = box_ui_attach_point(parent->y, ph, 4 - (int32_t)a->parent_v) +
            edge * child->height * 0.5f + a->y;
    }
}

/** Give a node the position the layout worked out for it.
 *
 * A node the layout did not move keeps its authored position, or gets the origin of
 * its parent when it has none, so its WorldTransform is always written.
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
    if (!ecs_has_id(world, e, UiContentRect)) {
        ecs_add_id(world, e, UiContentRect);
    }

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
 * @return True when a node grows or wraps at the width its parent leaves it
 */
static bool box_ui_measure(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    int32_t i;
    bool texts = state->texts != 0;
    bool shadows = state->shadows != 0;
    bool grows = state->grows != 0;
    bool scrolls = state->scrolls != 0;
    bool result = false;
    ecs_vec_clear(&state->scrolled);
    for (i = ecs_vec_count(&state->places) - 1; i >= 0; i --) {
        UiPlace *p = ecs_vec_get_t(&state->places, UiPlace, i);
        if (p->aux) {
            continue;
        }

        box_ui_read(world, p, shadows, grows);
        if (texts) {
            box_ui_text_measure(world, p);
        }

        box_ui_fit(p);
        if (scrolls && ecs_has(world, p->e, Scroll)) {
            ecs_vec_append_t(NULL, &state->scrolled, int32_t)[0] = i;
        }

        p->shrinks = (p->shrinks || p->wraps) && p->fit_width;
        result = result || p->wraps || p->grow_width > 0 || p->grow_height > 0;
        if (p->parent >= 0 && !p->has_anchor && !p->has_world) {
            box_ui_add(ecs_vec_get_t(&state->places, UiPlace, p->parent), p);
        }
    }

    return result;
}

/** Link every collected node to its first child and its next sibling.
 *
 * @param places The places of the walk, in pre-order
 */
static void box_ui_link(
    ecs_vec_t *places)
{
    int32_t i, count = ecs_vec_count(places);
    UiPlace *first = ecs_vec_first_t(places, UiPlace);
    for (i = 0; i < count; i ++) {
        first[i].child = -1;
        first[i].sibling = -1;
    }

    for (i = count - 1; i >= 0; i --) {
        UiPlace *p = &first[i];
        if (p->aux || p->parent < 0) {
            continue;
        }

        p->sibling = first[p->parent].child;
        first[p->parent].child = i;
    }
}

/** True when a child takes room in the row or the column of its parent.
 *
 * @param child The place of the child
 * @return True for a child the layout of its parent places
 */
static bool box_ui_flows(
    const UiPlace *child)
{
    return !child->aux && !child->has_anchor && !child->has_world;
}

/** Size of the box of a parent on one axis, padding included.
 *
 * The root of a tree without a box of its own offers the view that draws it,
 * like it does to the children it anchors.
 *
 * @param world The world
 * @param root The root of the tree being walked
 * @param p The place of the parent
 * @param x True for the width, false for the height
 * @return The size, 0 when the parent has no box on the axis
 */
static float box_ui_outer(
    ecs_world_t *world,
    ecs_entity_t root,
    const UiPlace *p,
    bool x)
{
    float size = x ? p->width : p->height;
    if (size <= 0 && p->e == root) {
        const UiImpl *impl = ecs_get(world, root, UiImpl);
        size = impl ? (x ? impl->width : impl->height) : 0;
    }

    return size > 0 ? size : 0;
}

/** Size of the box of a parent inside its padding on one axis.
 *
 * @param world The world
 * @param root The root of the tree being walked
 * @param p The place of the parent
 * @param x True for the width, false for the height
 * @return The inner size, 0 when the parent has no box on the axis
 */
static float box_ui_inner(
    ecs_world_t *world,
    ecs_entity_t root,
    const UiPlace *p,
    bool x)
{
    float size = box_ui_outer(world, root, p, x);
    float result = 0;
    if (size > 0) {
        result = size - (x ? p->pad_left + p->pad_right : p->pad_top + p->pad_bottom);
    }

    return result < 0 ? 0 : result;
}

/** Give a node a new width, wrapping the lines of a text that wraps again.
 *
 * @param world The world
 * @param p The place of the node
 * @param width The new width
 */
static void box_ui_set_width(
    ecs_world_t *world,
    UiPlace *p,
    float width)
{
    if (width == p->width) {
        goto done;
    }

    if (p->has_text && p->wraps) {
        box_ui_text_rewrap(world, p, width);
    }

    p->width = width;
done:
    return;
}

/** Share the room a row or a column leaves among the children that grow along it.
 *
 * The room goes to the children by weight, and a child that reaches its
 * maximum keeps it and leaves the rest of its share to the others.
 *
 * @param world The world
 * @param places The places of the walk
 * @param parent The place of the parent
 * @param room Logical pixels the children leave along the axis
 * @param x True for a row, false for a column
 * @return Logical pixels the children grew by together
 */
static float box_ui_grow_along(
    ecs_world_t *world,
    ecs_vec_t *places,
    const UiPlace *parent,
    float room,
    bool x)
{
    float result = 0;
    int32_t round, c;
    for (round = 0; round < 8 && room > 0.001f; round ++) {
        float weights = 0, given = 0;
        for (c = parent->child; c >= 0; c = ecs_vec_get_t(places, UiPlace, c)->sibling) {
            const UiPlace *ch = ecs_vec_get_t(places, UiPlace, c);
            float max = x ? ch->layout.max_width : ch->layout.max_height;
            float size = x ? ch->width : ch->height;
            float weight = x ? ch->grow_width : ch->grow_height;
            if (box_ui_flows(ch) && weight > 0 && (max <= 0 || size < max)) {
                weights += weight;
            }
        }

        if (weights <= 0) {
            break;
        }

        for (c = parent->child; c >= 0; c = ecs_vec_get_t(places, UiPlace, c)->sibling) {
            UiPlace *ch = ecs_vec_get_t(places, UiPlace, c);
            float max = x ? ch->layout.max_width : ch->layout.max_height;
            float size = x ? ch->width : ch->height;
            float weight = x ? ch->grow_width : ch->grow_height;
            float add = room * weight / weights;
            if (!box_ui_flows(ch) || weight <= 0 || (max > 0 && size >= max)) {
                continue;
            }

            if (max > 0 && size + add > max) {
                add = max - size;
            }

            if (x) {
                box_ui_set_width(world, ch, size + add);
            } else {
                ch->height = size + add;
            }

            given += add;
        }

        room -= given;
        result += given;
    }

    return result;
}

/** Narrow the children of a row that give way when they do not fit it.
 *
 * The width the row is short of is taken from the children that shrink, in
 * proportion to their width, down to their minimum.
 *
 * @param world The world
 * @param places The places of the walk
 * @param parent The place of the row
 * @param short_of Logical pixels the children are wider than the row
 */
static void box_ui_shrink_row(
    ecs_world_t *world,
    ecs_vec_t *places,
    const UiPlace *parent,
    float short_of)
{
    float total = 0;
    int32_t c;
    for (c = parent->child; c >= 0; c = ecs_vec_get_t(places, UiPlace, c)->sibling) {
        const UiPlace *ch = ecs_vec_get_t(places, UiPlace, c);
        if (box_ui_flows(ch) && ch->shrinks) {
            total += ch->width;
        }
    }

    for (c = parent->child; c >= 0 && total > 0; c = ecs_vec_get_t(
        places, UiPlace, c)->sibling)
    {
        UiPlace *ch = ecs_vec_get_t(places, UiPlace, c);
        if (!box_ui_flows(ch) || !ch->shrinks) {
            continue;
        }

        float width = ch->width - short_of * ch->width / total;
        box_ui_set_width(world, ch, box_ui_bound(width < 0 ? 0 : width,
            ch->layout.min_width, 0));
    }
}

/** Give the children of a node the widths the room of the node leaves them.
 *
 * In a row, growing children share the leftover room and shrinking ones narrow when it
 * is short. In a column or a node that places nothing, children are stretched or
 * narrowed to the box of the node.
 *
 * An anchored child is pinned to the edges of the node, so it stretches over the
 * padding too.
 *
 * @param world The world
 * @param root The root of the tree being walked
 * @param places The places of the walk
 * @param p The place of the node
 */
static void box_ui_widths_of(
    ecs_world_t *world,
    ecs_entity_t root,
    ecs_vec_t *places,
    UiPlace *p)
{
    float inner = box_ui_inner(world, root, p, true);
    bool row = p->has_layout && p->layout.direction == DirectionRow;
    int32_t c;
    if (row && inner > 0) {
        float gap = p->flow > 1 ? p->layout.spacing * (float)(p->flow - 1) : 0;
        float room = inner - p->sum_width - gap;
        if (room > 0) {
            box_ui_grow_along(world, places, p, room, true);
        } else if (room < 0) {
            box_ui_shrink_row(world, places, p, -room);
        }
    }

    for (c = p->child; c >= 0 && inner > 0; c = ecs_vec_get_t(
        places, UiPlace, c)->sibling)
    {
        UiPlace *ch = ecs_vec_get_t(places, UiPlace, c);
        if (ch->aux || ch->has_world || (row && !ch->has_anchor)) {
            continue;
        }

        float room = ch->has_anchor ? box_ui_outer(world, root, p, true) : inner;
        if (ch->grow_width > 0) {
            box_ui_set_width(world, ch, box_ui_bound(room,
                ch->layout.min_width, ch->layout.max_width));
        } else if (ch->shrinks && ch->width > room) {
            box_ui_set_width(world, ch, box_ui_bound(room, ch->layout.min_width, 0));
        }
    }
}

/** Work out the widths of a tree from the root down.
 *
 * A node has its final width when it is visited, because the node above it
 * gave it that width, so it hands the room it holds on to its children. The
 * sums the measure kept are cleared for the heights that follow.
 *
 * @param world The world
 * @param root The root of the tree being walked
 */
static void box_ui_widths(
    ecs_world_t *world,
    ecs_entity_t root)
{
    ecs_vec_t *places = &box_ui_state(world)->places;
    int32_t i, count = ecs_vec_count(places);
    for (i = 0; i < count; i ++) {
        UiPlace *p = ecs_vec_get_t(places, UiPlace, i);
        if (p->aux) {
            continue;
        }

        if (p->child >= 0) {
            box_ui_widths_of(world, root, places, p);
        }

        p->sum_width = 0;
        p->sum_height = 0;
        p->widest_child = 0;
        p->tallest_child = 0;
        p->flow = 0;
    }
}

/** Measure the heights of a tree again from the leaves up, at the final widths.
 *
 * @param world The world
 */
static void box_ui_refit(
    ecs_world_t *world)
{
    ecs_vec_t *places = &box_ui_state(world)->places;
    int32_t i;
    for (i = ecs_vec_count(places) - 1; i >= 0; i --) {
        UiPlace *p = ecs_vec_get_t(places, UiPlace, i);
        if (p->aux) {
            continue;
        }

        if (p->has_layout && !p->has_text) {
            const Layout *l = &p->layout;
            float gap = p->flow > 1 ? l->spacing * (float)(p->flow - 1) : 0;

            p->content_width = p->pad_left + p->pad_right +
                (l->direction == DirectionRow ? p->sum_width + gap : p->widest_child);
            p->content_height = p->pad_top + p->pad_bottom +
                (l->direction == DirectionColumn ? p->sum_height + gap : p->tallest_child);
            if (p->fit_height) {
                p->height = box_ui_bound(p->content_height,
                    p->layout.min_height, p->layout.max_height);
            }
        }

        if (p->parent >= 0 && box_ui_flows(p)) {
            box_ui_add(ecs_vec_get_t(places, UiPlace, p->parent), p);
        }
    }
}

/** Grow the heights of a tree from the root down.
 *
 * Along a column the children that grow share the room the others leave, and
 * across a row, and in a node that places nothing, a child that grows is
 * stretched to the box of the node.
 *
 * An anchored child is pinned to the edges of the node, so it stretches over the
 * padding too.
 *
 * @param world The world
 * @param root The root of the tree being walked
 */
static void box_ui_heights(
    ecs_world_t *world,
    ecs_entity_t root)
{
    ecs_vec_t *places = &box_ui_state(world)->places;
    int32_t i, c, count = ecs_vec_count(places);
    for (i = 0; i < count; i ++) {
        UiPlace *p = ecs_vec_get_t(places, UiPlace, i);
        float inner = p->aux || p->child < 0 ? 0 : box_ui_inner(world, root, p, false);
        bool column = p->has_layout && p->layout.direction == DirectionColumn;
        if (inner <= 0) {
            continue;
        }

        if (column) {
            float gap = p->flow > 1 ? p->layout.spacing * (float)(p->flow - 1) : 0;
            float room = inner - p->sum_height - gap;
            if (room > 0) {
                p->sum_height += box_ui_grow_along(world, places, p, room, false);
            }
        }

        for (c = p->child; c >= 0; c = ecs_vec_get_t(places, UiPlace, c)->sibling) {
            UiPlace *ch = ecs_vec_get_t(places, UiPlace, c);
            if (ch->aux || ch->has_world || ch->grow_height <= 0 ||
                (column && !ch->has_anchor))
            {
                continue;
            }

            float room = ch->has_anchor ? box_ui_outer(world, root, p, false) : inner;
            ch->height = box_ui_bound(room, ch->layout.min_height, ch->layout.max_height);
        }
    }
}

/** Resolve the offset of every node with a Scroll against its final box.
 *
 * @param world The world
 */
static void box_ui_scrolls(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    int32_t i, count = ecs_vec_count(&state->scrolled);
    for (i = 0; i < count; i ++) {
        int32_t place = ecs_vec_get_t(&state->scrolled, int32_t, i)[0];
        box_ui_scroll_resolve(world, ecs_vec_get_t(&state->places, UiPlace, place));
    }
}

/** Place every collected node, a node before the nodes below it.
 *
 * The node without a parent is where the collect started: the tree root, placed at
 * its own Position, or a subtree root, which keeps the place its parent pass gave it.
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
        } else if (p->has_world) {
            box_ui_world_pin(world, root, p);
        } else if (p->has_anchor) {
            box_ui_pin(world, root, parent, p);
        } else if (parent->has_layout &&
            parent->layout.direction != DirectionNone)
        {
            box_ui_flow(parent, p);
        } else {
            p->x = parent->x + p->local_x;
            p->y = parent->y + p->local_y;
        }

        if (parent) {
            box_ui_scrolled(parent, p);
            box_ui_move(world, p->e, p->x - parent->x, p->y - parent->y);
        } else {
            box_ui_move(world, p->e, p->local_x, p->local_y);
        }

        if (p->has_layout) {
            box_ui_start(p);
        }

        if (p->has_rect && (p->has_layout || p->grow_width > 0 || p->grow_height > 0)) {
            box_ui_resize(world, p->e, p->width, p->height);
        }

        box_ui_bounds(world, p);
    }
}

void box_ui_layout(
    ecs_world_t *world,
    ecs_entity_t root)
{
    box_ui_resolve_fill(world, root);
    if (box_ui_measure(world)) {
        box_ui_link(&box_ui_state(world)->places);
        box_ui_widths(world, root);
        box_ui_refit(world);
        box_ui_heights(world, root);
    }

    if (box_ui_state(world)->scrolls) {
        box_ui_scrolls(world);
    }

    box_ui_arrange(world, root);
    box_ui_text_build(world);
    box_ui_shadow_build(world);
    box_ui_group_build(world, root);
    UiState *state = box_ui_state(world);
    if (state->scrolls || state->unclip) {
        box_ui_scroll_clip(world);
    }
}
