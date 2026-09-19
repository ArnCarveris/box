/**
 * @file ui/scroll.c
 * @brief Clips the content of an element to its box and moves it by an offset.
 *
 * A Scroll names what an element does with content that does not fit it on
 * each axis. The layout measures that content without the box limiting it,
 * clamps the offset of the element to the difference between the two and hands
 * the offset to the pass that places the children and to the one that lays out
 * the glyphs of a text, which is what moves what the element holds.
 *
 * A node the text pass dropped while the layout ran is skipped: the places of
 * the walk are older than the glyphs an element ends the pass with.
 *
 * The clip is a rectangle per node: the pass walks the collected places from
 * the root down, intersects the rectangle of every element that clips with the
 * one it inherited and leaves the result on the node as a RenderClip, which
 * extract packs into the instance and the fragment stage tests every fragment
 * against. A node that nothing clips loses the component again, so a tree
 * without a Scroll draws exactly what it drew before.
 *
 * The wheel of a frame moves the innermost scrollable ancestor of the element
 * under the cursor: an axis that is Scroll always takes it and an axis that is
 * Auto takes it while its content is larger than its box, so a nested list
 * scrolls itself and a panel that cannot scroll hands the wheel up.
 */

#include "ui_internal.h"

ECS_COMPONENT_DECLARE(ScrollImpl);

/** Edge of the rectangle of a node that nothing clips, past every view */
#define BOX_UI_CLIP_FAR (1e9f)

/** Largest offset an axis takes, the content that does not fit its box.
 *
 * @param mode What the axis does with content that does not fit
 * @param content Size the content of the element takes
 * @param box Size of the box of the element
 * @return The largest offset, 0 for an axis that does not scroll
 */
static float box_ui_scroll_max(
    Overflow mode,
    float content,
    float box)
{
    float result = 0;
    if (mode != OverflowVisible && content > box) {
        result = content - box;
    }

    return result;
}

/** Offset an axis keeps after its content and its box were measured.
 *
 * An offset that sat at the end of the content travels with that end, which is
 * what keeps a log that is appended to on its last line, and every other offset
 * is clamped to what the content leaves. An element that was never laid out
 * keeps the offset it was authored with, so a list that starts larger than its
 * box shows its first item and not its last.
 *
 * @param offset Offset the element carries
 * @param was Largest offset of the previous layout
 * @param max Largest offset of this layout
 * @param measured The element was laid out before
 * @return The offset the element ends the pass with
 */
static float box_ui_scroll_offset(
    float offset,
    float was,
    float max,
    bool measured)
{
    float result = offset;
    if (measured && offset >= was && max > was) {
        result = max;
    }

    if (result > max) {
        result = max;
    }

    if (result < 0) {
        result = 0;
    }

    return result;
}

void box_ui_scroll_resolve(
    ecs_world_t *world,
    UiPlace *p)
{
    Scroll *scroll = ecs_get_mut(world, p->e, Scroll);
    if (!scroll) {
        goto done;
    }

    ScrollImpl *impl = ecs_get_mut(world, p->e, ScrollImpl);
    float max_x = box_ui_scroll_max(scroll->x, p->content_width, p->width);
    float max_y = box_ui_scroll_max(scroll->y, p->content_height, p->height);
    bool measured = impl != NULL && impl->measured;
    float x = box_ui_scroll_offset(scroll->offset.x,
        impl ? impl->max_x : 0, max_x, measured);
    float y = box_ui_scroll_offset(scroll->offset.y,
        impl ? impl->max_y : 0, max_y, measured);

    if (impl) {
        impl->content_width = p->content_width;
        impl->content_height = p->content_height;
        impl->max_x = max_x;
        impl->max_y = max_y;
        impl->measured = true;
    }

    scroll->offset.x = x;
    scroll->offset.y = y;
    p->has_scroll = true;
    p->scroll = *scroll;
done:
    return;
}

/** Intersect the clip of a node with the box of the element that clips it.
 *
 * @param scroll The Scroll of the element
 * @param b The rectangle the layout gave the element
 * @param min Left and bottom edge the children inherit
 * @param max Right and top edge the children inherit
 */
static void box_ui_scroll_narrow(
    const Scroll *scroll,
    const Bounds *b,
    float *min,
    float *max)
{
    if (scroll->x != OverflowVisible) {
        float left = b->x - b->width * 0.5f;
        float right = b->x + b->width * 0.5f;

        min[0] = left > min[0] ? left : min[0];
        max[0] = right < max[0] ? right : max[0];
    }

    if (scroll->y != OverflowVisible) {
        float bottom = b->y - b->height * 0.5f;
        float top = b->y + b->height * 0.5f;

        min[1] = bottom > min[1] ? bottom : min[1];
        max[1] = top < max[1] ? top : max[1];
    }
}

/** Read the clip a node already carries, which is what its subtree inherits.
 *
 * The first place of a pass over one subtree has no parent in the places, so
 * the rectangle the elements above it left is the one on the node itself.
 *
 * @param world The world
 * @param p The place of the node
 */
static void box_ui_scroll_inherit(
    ecs_world_t *world,
    UiPlace *p)
{
    const RenderClip *clip = ecs_get(world, p->e, RenderClip);
    p->clip_min[0] = clip ? clip->min[0] : -BOX_UI_CLIP_FAR;
    p->clip_min[1] = clip ? clip->min[1] : -BOX_UI_CLIP_FAR;
    p->clip_max[0] = clip ? clip->max[0] : BOX_UI_CLIP_FAR;
    p->clip_max[1] = clip ? clip->max[1] : BOX_UI_CLIP_FAR;
    p->clipped = clip != NULL;
}

/** Give the children of a node the clip the node leaves them.
 *
 * The rectangle of the element and its Scroll are read from the world and not
 * from the place, because the pass also runs over a walk that was collected
 * after the layout, which is what gives the glyphs an element just emitted the
 * clip of that element.
 *
 * @param world The world
 * @param parent The place of the node that holds the children
 * @param p The place of the child
 */
static void box_ui_scroll_descend(
    ecs_world_t *world,
    const UiPlace *parent,
    UiPlace *p)
{
    const Scroll *scroll = ecs_get(world, parent->e, Scroll);
    const Bounds *b = scroll ? ecs_get(world, parent->e, Bounds) : NULL;

    p->clip_min[0] = parent->clip_min[0];
    p->clip_min[1] = parent->clip_min[1];
    p->clip_max[0] = parent->clip_max[0];
    p->clip_max[1] = parent->clip_max[1];
    p->clipped = parent->clipped;
    if (b) {
        box_ui_scroll_narrow(scroll, b, p->clip_min, p->clip_max);
        p->clipped = p->clipped || scroll->x != OverflowVisible ||
            scroll->y != OverflowVisible;
    }
}

void box_ui_scroll_clip(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    int32_t i, count = ecs_vec_count(&state->places);
    for (i = 0; i < count; i ++) {
        UiPlace *p = ecs_vec_get_t(&state->places, UiPlace, i);
        const UiPlace *parent = p->parent >= 0 ?
            ecs_vec_get_t(&state->places, UiPlace, p->parent) : NULL;
        if (!ecs_is_alive(world, p->e)) {
            continue;
        }

        if (!parent) {
            box_ui_scroll_inherit(world, p);
            continue;
        }

        box_ui_scroll_descend(world, parent, p);
        if (p->clipped) {
            box_render_set_clip(world, p->e, p->clip_min, p->clip_max);
        } else {
            box_render_clear_clip(world, p->e);
        }
    }

    state = box_ui_state(world);
    if (!state->scrolls) {
        state->unclip = false;
    }
}

/** Node of a tree the pick of this frame left the Hovered tag on.
 *
 * @param world The world
 * @return The node under the cursor, or 0 when the cursor is beside every tree
 */
static ecs_entity_t box_ui_scroll_hovered(
    const ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each_id(world, Hovered);
    while (!result && ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_has(it.world, it.entities[i], UiNode)) {
                result = it.entities[i];
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Whether an axis of an element takes the wheel.
 *
 * @param mode What the axis does with content that does not fit
 * @param max Largest offset the axis takes
 * @return True when the wheel moves the offset of that axis
 */
static bool box_ui_scroll_takes(
    Overflow mode,
    float max)
{
    return mode == OverflowScroll || (mode == OverflowAuto && max > 0);
}

/** Move the offset of one axis of the innermost element that takes the wheel.
 *
 * @param world The world
 * @param from The element under the cursor
 * @param axis 0 for the x axis and 1 for the y axis
 * @param delta Logical pixels the wheel asks for
 */
static void box_ui_scroll_wheel(
    ecs_world_t *world,
    ecs_entity_t from,
    int32_t axis,
    float delta)
{
    ecs_entity_t cur = from;
    while (cur) {
        const Scroll *scroll = ecs_get(world, cur, Scroll);
        const ScrollImpl *impl = scroll ? ecs_get(world, cur, ScrollImpl) : NULL;
        Overflow mode = scroll ? (axis ? scroll->y : scroll->x) : OverflowVisible;
        float max = impl ? (axis ? impl->max_y : impl->max_x) : 0;
        if (scroll && box_ui_scroll_takes(mode, max)) {
            vec2 offset = scroll->offset;
            float value = (axis ? offset.y : offset.x) + delta;

            value = value < 0 ? 0 : (value > max ? max : value);
            if (axis) {
                offset.y = value;
            } else {
                offset.x = value;
            }

            ecs_set(world, cur, Scroll, {
                .x = scroll->x, .y = scroll->y, .offset = offset });
            cur = 0;
            continue;
        }

        cur = box_hierarchy_parent(world, cur);
    }
}

void box_ui_scroll_update(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    const Input *in = ecs_id(Input) ?
        ecs_singleton_get(world, Input) : NULL;
    if (!state->scrolls || !in) {
        goto done;
    }

    vec2 wheel = in->mouse.scroll_delta;
    if (wheel.x == 0 && wheel.y == 0) {
        goto done;
    }

    ecs_entity_t hovered = box_ui_scroll_hovered(world);
    if (!hovered) {
        goto done;
    }

    if (wheel.x != 0) {
        box_ui_scroll_wheel(world, hovered, 0, -wheel.x * BOX_UI_SCROLL_STEP);
    }

    if (wheel.y != 0) {
        box_ui_scroll_wheel(world, hovered, 1, -wheel.y * BOX_UI_SCROLL_STEP);
    }

done:
    return;
}

/** Scroll observer: count the elements so a world without one pays nothing.
 *
 * A world that loses its last Scroll still has to walk its trees once, because
 * the nodes below the element keep the clip the pass left on them.
 *
 * @param it Iterator over the elements that gained or lost their Scroll
 */
static void box_ui_scroll_on_tree(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);

    state->scrolls += it->event == EcsOnAdd ? it->count : -it->count;
    if (it->event == EcsOnRemove) {
        state->unclip = true;
    }
}

void box_ui_scroll_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, ScrollImpl);

    ecs_set_hooks(world, ScrollImpl, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(Scroll), EcsWith, ecs_id(ScrollImpl));
    ecs_add_pair(world, ecs_id(ScrollImpl), EcsOnInstantiate, EcsDontInherit);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Scroll), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_ui_scroll_on_tree
    });
}
