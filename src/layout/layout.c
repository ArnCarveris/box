/**
 * @file layout/layout.c
 * @brief Registers the box.layout module and lays out the trees that changed.
 *
 * Observers mark the elements whose layout input changed; the LayoutUpdate system lays out
 * the tree of every marked element in EcsPostUpdate, before transforms propagate.
 */

#define BOX_LAYOUT_IMPL
#include "layout_internal.h"
#include <stdlib.h>

ECS_TAG_DECLARE(ClipChildren);

static ECS_COMPONENT_DECLARE(LayoutRegistry);

/** Mark an element whose layout input changed.
 *
 * @param state The state of the layout module
 * @param e The element
 */
static void box_layout_mark(
    box_layout_state_t *state,
    ecs_entity_t e)
{
    *ecs_vec_append_t(NULL, &state->marks, ecs_entity_t) = e;
}

/** Element observer: a Layout, LayoutItem, shape or text size changed.
 *
 * @param it Iterator over the elements
 */
static void box_layout_on_element(
    ecs_iter_t *it)
{
    box_layout_state_t *state = it->ctx;
    int32_t i;
    if (!state->writing) {
        for (i = 0; i < it->count; i ++) {
            box_layout_mark(state, it->entities[i]);
        }
    }
}

void box_layout_observe_children(
    ecs_world_t *world,
    box_layout_state_t *state)
{
    if (!state->moved) {
        state->moved = ecs_query(world, {
            .terms = {
                { .id = ecs_id(Position), .inout = EcsIn },
                { .id = state->child_tag, .inout = EcsInOutFilter }
            },
            .cache_kind = EcsQueryCacheAuto,
            .flags = EcsQueryDetectChanges
        });
    }
}

/** Mark the children whose Position changed since the layout last looked, for a DirectionNone
 * parent fits them.
 *
 * Change detection is per table: a Position set outside a layout tree costs nothing, and a
 * write through a query field is seen too. The children of a changed table are marked when
 * their parent has a Layout.
 *
 * @param world The world
 * @param state The state of the layout module
 * @param mark Mark the children, false to only take in the changes
 */
static void box_layout_moved(
    ecs_world_t *world,
    box_layout_state_t *state,
    bool mark)
{
    int32_t i;
    if (!state->moved || !ecs_query_changed(state->moved)) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->moved);
    while (ecs_query_next(&it)) {
        ecs_entity_t prev = 0;
        bool laid_out = false;
        if (!mark || !ecs_iter_changed(&it)) {
            continue;
        }

        for (i = 0; i < it.count; i ++) {
            ecs_entity_t parent = ecs_get_parent(world, it.entities[i]);
            if (parent != prev) {
                laid_out = parent && ecs_has(world, parent, Layout);
                prev = parent;
            }

            if (laid_out) {
                box_layout_mark(state, it.entities[i]);
            }
        }
    }

done:
    return;
}

/** Remove the child tag from elements that leave their parent, unless they are deleted.
 *
 * @param it Iterator over the elements, an OnRemove or OnSet of their hierarchy
 * @param row First element
 * @param count Number of elements
 */
static void box_layout_untag(
    ecs_iter_t *it,
    int32_t row,
    int32_t count)
{
    box_layout_state_t *state = it->ctx;
    int32_t i;
    bool deleted = it->event == EcsOnRemove &&
        (!it->other_table || !ecs_table_get_type(it->other_table)->count);
    if (deleted || !ecs_table_has_id(it->world, it->table, state->child_tag)) {
        goto done;
    }

    for (i = row; i < row + count; i ++) {
        ecs_remove_id(it->stage, it->entities[i], state->child_tag);
    }

done:
    return;
}

/** Mark the parent and the elements of a hierarchy change.
 *
 * @param it Iterator over the children
 * @param row First child to mark
 * @param count Number of children to mark
 * @param parent The parent the children gained or lost
 */
static void box_layout_mark_children(
    ecs_iter_t *it,
    int32_t row,
    int32_t count,
    ecs_entity_t parent)
{
    box_layout_state_t *state = it->ctx;
    int32_t i;
    if (parent && ecs_is_alive(it->world, parent) && ecs_has(it->world, parent, Layout)) {
        box_layout_mark(state, parent);
    }

    ecs_entity_t text = state->text.component;
    if (ecs_table_has_id(it->world, it->table, ecs_id(Layout)) ||
        ecs_table_has_id(it->world, it->table, ecs_id(LayoutItem)) ||
        (text && ecs_table_has_id(it->world, it->table, text)))
    {
        for (i = row; i < row + count; i ++) {
            box_layout_mark(state, it->entities[i]);
        }
    }
}

/** ChildOf observer: children were added to or removed from a parent.
 *
 * @param it Iterator over the children
 */
static void box_layout_on_childof(
    ecs_iter_t *it)
{
    ecs_entity_t parent = ecs_pair_second(it->world, ecs_field_id(it, 0));
    box_layout_mark_children(it, 0, it->count, parent);
    if (it->event == EcsOnRemove) {
        box_layout_untag(it, 0, it->count);
    }
}

/** Parent observer: children were added to or removed from a parent.
 *
 * An OnSet only sees the new parent, so the layout keeps the parent of the elements it lays
 * out through Parent (box_layout_collect) to mark the parent an element moves away from.
 *
 * @param it Iterator over the children
 */
static void box_layout_on_parent(
    ecs_iter_t *it)
{
    box_layout_state_t *state = it->ctx;
    const EcsParent *p = ecs_field(it, EcsParent, 0);
    bool kept = ecs_map_count(&state->parents) != 0;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_map_key_t key = (ecs_map_key_t)it->entities[i];
        ecs_map_val_t *prev = kept ? ecs_map_get(&state->parents, key) : NULL;
        if (prev && (ecs_entity_t)*prev != p[i].value) {
            box_layout_mark_children(it, i, 1, (ecs_entity_t)*prev);
        }

        if (prev && (it->event == EcsOnRemove || (ecs_entity_t)*prev != p[i].value)) {
            ecs_map_remove(&state->parents, key);
        }

        box_layout_mark_children(it, i, 1, p[i].value);
        if (it->event == EcsOnRemove || !p[i].value ||
            !ecs_is_alive(it->world, p[i].value) || !ecs_has(it->world, p[i].value, Layout))
        {
            box_layout_untag(it, i, 1);
        }
    }
}

/** Find the root of the layout tree of an element.
 *
 * The root is the highest ancestor reached through parents with Layout. An element outside
 * any tree has no root, and neither has a tree whose root is a prefab: only instances are laid
 * out. flecs tags every descendant of a prefab with Prefab, so checking the root is enough.
 *
 * @param world The world
 * @param e The element
 * @return The root, 0 when the element is not laid out
 */
ecs_entity_t box_layout_root(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = e;
    ecs_entity_t parent = ecs_get_parent(world, result);
    while (parent && ecs_has(world, parent, Layout)) {
        result = parent;
        parent = ecs_get_parent(world, result);
    }

    if (result == e && !ecs_has(world, e, Layout) && !ecs_has(world, e, LayoutItem)) {
        result = 0;
    }

    if (result && ecs_has_id(world, result, EcsPrefab)) {
        result = 0;
    }

    return result;
}

/** Order entities by id.
 *
 * @param a The first entity
 * @param b The second entity
 * @return Negative, zero or positive
 */
static int box_layout_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t ea = *(const ecs_entity_t*)a;
    ecs_entity_t eb = *(const ecs_entity_t*)b;

    return (ea > eb) - (ea < eb);
}

/** LayoutScroll observer: a new or removed offset lays the tree out again, a set one only
 * moves the children unless the axes that scroll changed (LayoutUpdate decides).
 *
 * @param it Iterator over the elements
 */
static void box_layout_on_scroll(
    ecs_iter_t *it)
{
    box_layout_state_t *state = it->ctx;
    int32_t i;
    if (state->writing) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        if (it->event == EcsOnSet) {
            *ecs_vec_append_t(NULL, &state->scrolls, ecs_entity_t) = it->entities[i];
        } else {
            ecs_map_remove(&state->scrolled, (ecs_map_key_t)it->entities[i]);
            box_layout_mark(state, it->entities[i]);
        }
    }

done:
    return;
}

/** LayoutFloat observer: keep the floating elements and lay out the tree of a changed one.
 *
 * @param it Iterator over the elements
 */
static void box_layout_on_float(
    ecs_iter_t *it)
{
    box_layout_state_t *state = it->ctx;
    int32_t i, f;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (it->event == EcsOnAdd) {
            *ecs_vec_append_t(NULL, &state->floats, ecs_entity_t) = e;
        } else if (it->event == EcsOnRemove) {
            ecs_entity_t *floats = ecs_vec_first_t(&state->floats, ecs_entity_t);
            for (f = 0; f < ecs_vec_count(&state->floats); f ++) {
                if (floats[f] == e) {
                    ecs_vec_remove_t(&state->floats, ecs_entity_t, f);
                    break;
                }
            }

            ecs_map_remove(&state->anchor_sizes, (ecs_map_key_t)e);
        }

        if (!state->writing) {
            box_layout_mark(state, e);
        }
    }
}

/** Sort the elements whose LayoutScroll was set: those whose scrolling axes changed or that
 * were not laid out as scrolling are marked, the others only move their children.
 *
 * @param world The world
 * @param state The state of the layout module
 */
static void box_layout_scrolls(
    ecs_world_t *world,
    box_layout_state_t *state)
{
    int32_t i, count = ecs_vec_count(&state->scrolls);
    ecs_vec_clear(&state->shifts);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&state->scrolls, ecs_entity_t, i);
        const LayoutScroll *s = ecs_is_alive(world, e) ? ecs_get(world, e, LayoutScroll) : NULL;
        if (!s) {
            continue;
        }

        const ecs_map_val_t *axes = ecs_map_get(&state->scrolled, (ecs_map_key_t)e);
        ecs_map_val_t want = 4u | (s->horizontal ? 1u : 0u) | (s->vertical ? 2u : 0u);
        if (!axes || *axes != want) {
            box_layout_mark(state, e);
        } else {
            *ecs_vec_append_t(NULL, &state->shifts, ecs_entity_t) = e;
        }
    }

    ecs_vec_clear(&state->scrolls);
}

/** Layout observer: an element lost its Layout, so its children left the tree.
 *
 * @param it Iterator over the elements
 */
static void box_layout_on_layout_remove(
    ecs_iter_t *it)
{
    box_layout_state_t *state = it->ctx;
    int32_t i, c;
    if (state->writing || !it->other_table || !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_iter_t ci = ecs_children(it->world, it->entities[i]);
        while (ecs_children_next(&ci)) {
            for (c = 0; c < ci.count; c ++) {
                box_layout_mark(state, ci.entities[c]);
            }
        }
    }

done:
    return;
}

void box_layout_text_register(
    ecs_world_t *world,
    const box_layout_text_t *text)
{
    box_layout_state_t *state = ecs_singleton_get(world, LayoutRegistry)->state;
    state->text = *text;
    ecs_observer(world, {
        .query.terms = {{ .id = text->bounds, .inout = EcsInOutNone }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = box_layout_on_element,
        .ctx = state
    });
}

/** Compare two entities by creation, the index part of their ids.
 *
 * @param a The first entity
 * @param b The second entity
 * @return Negative, zero or positive as a was created before, with or after b
 */
static int box_layout_compare_created(
    const void *a,
    const void *b)
{
    uint32_t ia = (uint32_t)*(const ecs_entity_t*)a;
    uint32_t ib = (uint32_t)*(const ecs_entity_t*)b;

    return (ia > ib) - (ia < ib);
}

/** Put the children of the parents that got OrderedChildren since the last layout in the order
 * they were created.
 *
 * flecs lists the children a parent had before it got the trait table by table, so the children
 * of a template instance whose body adds the Layout, created by the scene before the body ran,
 * would otherwise be placed in an order that depends on their components.
 *
 * @param world The world
 * @param state The layout state
 */
static void box_layout_order(
    ecs_world_t *world,
    box_layout_state_t *state)
{
    ecs_vec_t sorted;
    int32_t i;
    ecs_vec_init_t(NULL, &sorted, ecs_entity_t, 0);
    for (i = 0; i < ecs_vec_count(&state->ordered); i ++) {
        ecs_entity_t parent = *ecs_vec_get_t(&state->ordered, ecs_entity_t, i);
        ecs_entities_t children = { 0 };
        if (ecs_is_alive(world, parent) && ecs_has_id(world, parent, EcsOrderedChildren)) {
            children = ecs_get_ordered_children(world, parent);
        }

        if (children.count < 2) {
            continue;
        }

        ecs_vec_set_count_t(NULL, &sorted, ecs_entity_t, children.count);
        ecs_os_memcpy_n(ecs_vec_first(&sorted), children.ids, ecs_entity_t, children.count);
        qsort(ecs_vec_first(&sorted), (size_t)children.count, sizeof(ecs_entity_t),
            box_layout_compare_created);
        if (ecs_os_memcmp(ecs_vec_first(&sorted), children.ids,
            ECS_SIZEOF(ecs_entity_t) * children.count))
        {
            ecs_set_child_order(world, parent, ecs_vec_first(&sorted), children.count);
            box_layout_mark(state, parent);
        }
    }

    ecs_vec_clear(&state->ordered);
    ecs_vec_fini_t(NULL, &sorted, ecs_entity_t);
}

/** Queue the parents that got OrderedChildren for box_layout_order.
 *
 * @param it Iterator of the observer
 */
static void box_layout_on_ordered(
    ecs_iter_t *it)
{
    box_layout_state_t *state = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        *ecs_vec_append_t(NULL, &state->ordered, ecs_entity_t) = it->entities[i];
    }
}

/** Size the elements that fill a surface to the size of the surface in points.
 *
 * @param it The system iterator
 */
static void LayoutFillUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const LayoutFill *fill = ecs_field(it, LayoutFill, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        bool alive = ecs_is_alive(world, fill[i].surface);
        const SurfaceStatus *st = alive ? ecs_get(world, fill[i].surface, SurfaceStatus) : NULL;
        const Surface *s = alive ? ecs_get(world, fill[i].surface, Surface) : NULL;
        float w = 0, h = 0;
        if (st && st->point_width > 0 && st->point_height > 0) {
            w = (float)st->point_width;
            h = (float)st->point_height;
        } else if (s) {
            w = (float)s->width;
            h = (float)s->height;
        }

        LayoutItem *item = ecs_get_mut(world, it->entities[i], LayoutItem);
        if (item->width.mode != SizeModeFixed || item->height.mode != SizeModeFixed ||
            item->width.value != w || item->height.value != h)
        {
            item->width.mode = item->height.mode = SizeModeFixed;
            item->width.value = w;
            item->height.value = h;
            ecs_modified(world, it->entities[i], LayoutItem);
        }
    }
}

/** Lay out the trees of the elements marked since the last frame, move the children of the
 * elements that only scrolled and place the floating elements.
 *
 * @param it The system iterator
 */
static void LayoutUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_layout_state_t *state = it->ctx;
    int32_t i, count;
    box_layout_order(world, state);
    box_layout_moved(world, state, true);
    box_layout_scrolls(world, state);
    count = ecs_vec_count(&state->marks);
    if (!count) {
        goto moved;
    }

    ecs_entity_t *marks = ecs_vec_first_t(&state->marks, ecs_entity_t);
    ecs_vec_clear(&state->roots);
    for (i = 0; i < count; i ++) {
        bool alive = ecs_is_alive(world, marks[i]);
        ecs_entity_t root = alive ? box_layout_root(world, marks[i]) : 0;
        if (root) {
            *ecs_vec_append_t(NULL, &state->roots, ecs_entity_t) = root;
        } else if (alive && state->text.wrap &&
            ecs_has_id(world, marks[i], state->text.component))
        {
            state->writing = true;
            state->text.wrap(world, marks[i], 0);
            state->writing = false;
        }
    }

    ecs_vec_clear(&state->marks);
    ecs_entity_t *roots = ecs_vec_first_t(&state->roots, ecs_entity_t);
    count = ecs_vec_count(&state->roots);
    qsort(roots, (size_t)count, sizeof(ecs_entity_t), box_layout_compare);
    for (i = 0; i < count; i ++) {
        if (!i || roots[i] != roots[i - 1]) {
            box_layout_tree(world, state, roots[i]);
        }
    }

moved:
    for (i = 0; i < ecs_vec_count(&state->shifts); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&state->shifts, ecs_entity_t, i);
        if (ecs_is_alive(world, e)) {
            box_layout_scroll_shift(world, state, e);
        }
    }

    ecs_vec_clear(&state->shifts);
    box_layout_floats(world, state);
    box_layout_moved(world, state, false);
}

/** Free the states of LayoutRegistry values.
 *
 * The world destroys components after the OnRemove observers of its last entities ran, so the
 * observers of the module never see a freed state.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info
 */
static void box_layout_registry_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    LayoutRegistry *r = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_layout_state_t *state = r[i].state;
        if (state) {
            ecs_vec_fini_t(NULL, &state->marks, ecs_entity_t);
            ecs_vec_fini_t(NULL, &state->roots, ecs_entity_t);
            ecs_vec_fini_t(NULL, &state->nodes, box_layout_node_t);
            ecs_vec_fini_t(NULL, &state->untagged, ecs_entity_t);
            ecs_vec_fini_t(NULL, &state->floating, box_layout_node_t);
            ecs_vec_fini_t(NULL, &state->scrolls, ecs_entity_t);
            ecs_vec_fini_t(NULL, &state->shifts, ecs_entity_t);
            ecs_vec_fini_t(NULL, &state->floats, ecs_entity_t);
            ecs_vec_fini_t(NULL, &state->ordered, ecs_entity_t);
            ecs_vec_fini_t(NULL, &state->placing, box_layout_placing_t);
            ecs_map_fini(&state->parents);
            ecs_map_fini(&state->scrolled);
            ecs_map_fini(&state->anchor_sizes);
            ecs_os_free(state);
        }
    }
}

/** Move LayoutRegistry values, the source no longer owns its state.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti Type info
 */
static void box_layout_registry_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

/** Construct LayoutItem values whose sizes have a weight of one and keep their content.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_layout_item_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    LayoutItem *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (LayoutItem){
            .width = { .value = 1, .content_min = true },
            .height = { .value = 1, .content_min = true }
        };
    }
}

void BoxLayoutImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLayout);

    ECS_META_COMPONENT(world, Align);
    ECS_META_COMPONENT(world, VAlign);
    ECS_META_COMPONENT(world, Direction);
    ECS_META_COMPONENT(world, Padding);
    ECS_META_COMPONENT(world, Layout);
    ECS_META_COMPONENT(world, SizeMode);
    ECS_META_COMPONENT(world, Sizing);
    ECS_META_COMPONENT(world, LayoutItem);
    ECS_META_COMPONENT(world, LayoutBounds);
    ECS_META_COMPONENT(world, LayoutFill);
    ECS_META_COMPONENT(world, LayoutScroll);
    ECS_META_COMPONENT(world, LayoutContent);
    ECS_META_COMPONENT(world, LayoutFloat);
    ECS_TAG_DEFINE(world, ClipChildren);
    ecs_set_hooks(world, LayoutItem, { .ctor = box_layout_item_ctor });

    ecs_add_pair(world, ecs_id(Layout), EcsWith, ecs_id(LayoutBounds));
    ecs_add_pair(world, ecs_id(Layout), EcsWith, EcsOrderedChildren);
    ecs_add_pair(world, ecs_id(LayoutItem), EcsWith, ecs_id(LayoutBounds));
    ecs_add_pair(world, ecs_id(LayoutFill), EcsWith, ecs_id(LayoutItem));
    ecs_add_pair(world, ecs_id(LayoutScroll), EcsWith, ecs_id(LayoutContent));

    ECS_COMPONENT_DEFINE(world, LayoutRegistry);
    ecs_set_hooks(world, LayoutRegistry, {
        .ctor = flecs_default_ctor,
        .dtor = box_layout_registry_dtor,
        .move = box_layout_registry_move
    });

    box_layout_state_t *state = ecs_os_calloc_t(box_layout_state_t);
    ecs_vec_init_t(NULL, &state->marks, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->roots, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->nodes, box_layout_node_t, 0);
    ecs_vec_init_t(NULL, &state->untagged, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->floating, box_layout_node_t, 0);
    ecs_vec_init_t(NULL, &state->scrolls, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->shifts, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->floats, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->ordered, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->placing, box_layout_placing_t, 0);
    ecs_map_init(&state->scrolled, NULL);
    ecs_map_init(&state->anchor_sizes, NULL);
    state->child_tag = ecs_entity(world, { .name = "LayoutChild" });
    ecs_map_init(&state->parents, NULL);
    ecs_singleton_ensure(world, LayoutRegistry)->state = state;

    ecs_id_t elements[] = {
        ecs_id(Layout), ecs_id(LayoutItem), ecs_id(Rect), ecs_id(Circle)
    };

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = elements[i], .inout = EcsInOutNone }},
            .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
            .callback = box_layout_on_element,
            .ctx = state
        });
    }

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LayoutOnScroll" }),
        .query.terms = {{ .id = ecs_id(LayoutScroll), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = box_layout_on_scroll,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LayoutOnFloat" }),
        .query.terms = {{ .id = ecs_id(LayoutFloat), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = box_layout_on_float,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LayoutOnLayoutRemove" }),
        .query.terms = {{ .id = ecs_id(Layout), .inout = EcsInOutNone }},
        .events = { EcsOnRemove },
        .callback = box_layout_on_layout_remove,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LayoutOnOrdered" }),
        .query.terms = {{ .id = EcsOrderedChildren }},
        .events = { EcsOnAdd },
        .callback = box_layout_on_ordered,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LayoutOnChildOf" }),
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_layout_on_childof,
        .ctx = state
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LayoutOnParent" }),
        .query.terms = {{ .id = ecs_id(EcsParent) }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = box_layout_on_parent,
        .ctx = state
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LayoutFillUpdate" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(LayoutFill), .inout = EcsIn },
            { .id = ecs_id(LayoutItem), .inout = EcsInOutNone }
        },
        .callback = LayoutFillUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LayoutUpdate" }),
        .phase = EcsPostUpdate,
        .run = LayoutUpdate,
        .ctx = state,
        .immediate = true
    });
}
