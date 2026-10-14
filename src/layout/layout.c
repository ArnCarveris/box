/**
 * @file layout/layout.c
 * @brief Registers the box.layout module and lays out the trees that changed.
 *
 * Observers mark the elements whose layout input changed; the LayoutUpdate system lays out
 * the tree of every marked element in EcsPostUpdate, before transforms propagate.
 */

#define BOX_LAYOUT_IMPL
#include "layout_internal.h"

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

/** Position observer: a child of a Layout moved, which a DirectionNone parent fits.
 *
 * @param it Iterator over the elements
 */
static void box_layout_on_position(
    ecs_iter_t *it)
{
    box_layout_state_t *state = it->ctx;
    int32_t i;
    if (state->writing) {
        goto done;
    }

    ecs_entity_t prev = 0;
    bool laid_out = false;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t parent = ecs_get_parent(it->world, it->entities[i]);
        if (parent != prev) {
            laid_out = parent && ecs_has(it->world, parent, Layout);
            prev = parent;
        }

        if (laid_out) {
            box_layout_mark(state, it->entities[i]);
        }
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

    if (ecs_table_has_id(it->world, it->table, ecs_id(Layout)) ||
        ecs_table_has_id(it->world, it->table, ecs_id(LayoutItem)))
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
}

/** Parent observer: children were added to or removed from a parent.
 *
 * @param it Iterator over the children
 */
static void box_layout_on_parent(
    ecs_iter_t *it)
{
    const EcsParent *p = ecs_field(it, EcsParent, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_layout_mark_children(it, i, 1, p[i].value);
    }
}

/** Find the root of the layout tree of an element.
 *
 * The root is the highest ancestor reached through parents with Layout. An element outside
 * any tree has no root.
 *
 * @param world The world
 * @param e The element
 * @return The root, 0 when the element is not laid out
 */
static ecs_entity_t box_layout_root(
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

/** Look up the TextBounds component and observe it once it exists.
 *
 * @param world The world
 * @param state The state of the layout module
 */
static void box_layout_find_text(
    ecs_world_t *world,
    box_layout_state_t *state)
{
    ecs_entity_t c = ecs_lookup_symbol(world, "TextBounds", false, false);
    const EcsComponent *type = c ? ecs_get(world, c, EcsComponent) : NULL;
    if (type && type->size == (ecs_size_t)(2 * sizeof(float))) {
        state->text_bounds = c;
        ecs_observer(world, {
            .query.terms = {{ .id = c, .inout = EcsInOutNone }},
            .events = { EcsOnSet, EcsOnRemove },
            .callback = box_layout_on_element,
            .ctx = state
        });
    }
}

/** Lay out the trees of the elements marked since the last frame.
 *
 * @param it The system iterator
 */
static void LayoutUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_layout_state_t *state = it->ctx;
    int32_t i, count = ecs_vec_count(&state->marks);
    if (!state->text_bounds) {
        box_layout_find_text(world, state);
    }

    if (!count) {
        goto done;
    }

    ecs_entity_t *marks = ecs_vec_first_t(&state->marks, ecs_entity_t);
    ecs_vec_clear(&state->roots);
    for (i = 0; i < count; i ++) {
        ecs_entity_t root = ecs_is_alive(world, marks[i]) ?
            box_layout_root(world, marks[i]) : 0;
        if (root) {
            *ecs_vec_append_t(NULL, &state->roots, ecs_entity_t) = root;
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

done:
    return;
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

/** Construct LayoutItem values whose sizes have a value of one, the weight of a growing size.
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
        values[i] = (LayoutItem){ .width = { .value = 1 }, .height = { .value = 1 } };
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
    ecs_set_hooks(world, LayoutItem, { .ctor = box_layout_item_ctor });

    ecs_add_pair(world, ecs_id(Layout), EcsWith, ecs_id(LayoutBounds));
    ecs_add_pair(world, ecs_id(Layout), EcsWith, EcsOrderedChildren);
    ecs_add_pair(world, ecs_id(LayoutItem), EcsWith, ecs_id(LayoutBounds));

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
        .entity = ecs_entity(world, { .name = "LayoutOnPosition" }),
        .query.terms = {{ .id = ecs_id(Position), .inout = EcsInOutNone }},
        .events = { EcsOnSet },
        .callback = box_layout_on_position,
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
        .entity = ecs_entity(world, { .name = "LayoutUpdate" }),
        .phase = EcsPostUpdate,
        .run = LayoutUpdate,
        .ctx = state,
        .immediate = true
    });

    box_layout_find_text(world, state);
}
