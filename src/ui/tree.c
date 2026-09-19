/**
 * @file ui/tree.c
 * @brief Detects the nodes of a UI tree and gives them their draw order.
 *
 * The walk of a tree is a pre-order walk, so a node is numbered after its
 * parent and after the subtrees of its earlier siblings. The number becomes the
 * depth bias of its instance, which is what draws a child over its parent.
 */

#include "ui_internal.h"

ecs_entity_t box_ui_root(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_entity_t cur = e;
    while (cur && !result) {
        if (ecs_has(world, cur, Ui)) {
            result = cur;
        }

        cur = box_hierarchy_parent(world, cur);
    }

    return result;
}

void box_ui_mark(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t root = box_ui_root(world, e);
    if (!root) {
        goto done;
    }

    UiImpl *impl = ecs_get_mut(world, root, UiImpl);
    if (impl) {
        impl->dirty = true;
    }

done:
    return;
}

/** Give one node its place in the tree, its layer and its depth bias.
 *
 * @param world The world
 * @param e The node
 * @param root The root of the tree
 * @param impl The private state of the root
 * @param order Index of the node in the walk
 */
static void box_ui_number(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t root,
    const UiImpl *impl,
    int32_t order)
{
    UiNode *node = ecs_get_mut(world, e, UiNode);
    if (node) {
        node->root = root;
        node->order = order;
    } else {
        ecs_set(world, e, UiNode, { .root = root, .order = order });
    }

    if (impl->layer && !ecs_has_pair(world, e, Layer, impl->layer)) {
        ecs_add_pair(world, e, Layer, impl->layer);
    }

    box_render_set_depth(world, e, -(float)order * BOX_UI_STEP);
}

void box_ui_walk(
    ecs_world_t *world,
    ecs_entity_t e)
{
    UiState *state = box_ui_state(world);
    const UiImpl *stored = ecs_get(world, e, UiImpl);
    if (!stored) {
        goto done;
    }

    UiImpl tree = *stored;
    int32_t order = 0;

    ecs_vec_clear(&state->stack);
    ecs_vec_append_t(NULL, &state->stack, ecs_entity_t)[0] = e;
    while (ecs_vec_count(&state->stack)) {
        int32_t last = ecs_vec_count(&state->stack) - 1;
        ecs_entity_t node = ecs_vec_get_t(&state->stack, ecs_entity_t, last)[0];
        ecs_vec_remove_last(&state->stack);
        box_ui_number(world, node, e, &tree, order ++);
        ecs_vec_clear(&state->nodes);
        box_hierarchy_collect(world, node, &state->nodes);

        int32_t i;
        for (i = ecs_vec_count(&state->nodes) - 1; i >= 0; i --) {
            ecs_entity_t child = ecs_vec_get_t(&state->nodes, ecs_entity_t, i)[0];
            bool skip = child == tree.view || child == tree.camera ||
                ecs_has(world, child, Ui);
            if (!skip) {
                ecs_vec_append_t(NULL, &state->stack, ecs_entity_t)[0] = child;
            }
        }
    }

    UiImpl *impl = ecs_get_mut(world, e, UiImpl);
    if (impl) {
        impl->count = order;
        impl->dirty = false;
    }

done:
    return;
}

void box_ui_drop(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const UiNode *node = ecs_get(world, e, UiNode);
    if (!node) {
        goto done;
    }

    ecs_entity_t root = node->root;
    const UiImpl *impl = ecs_is_alive(world, root) ?
        ecs_get(world, root, UiImpl) : NULL;
    ecs_entity_t layer = impl ? impl->layer : 0;
    ecs_vec_t todo;

    ecs_vec_init_t(NULL, &todo, ecs_entity_t, 0);
    ecs_vec_append_t(NULL, &todo, ecs_entity_t)[0] = e;
    while (ecs_vec_count(&todo)) {
        int32_t last = ecs_vec_count(&todo) - 1;
        ecs_entity_t cur = ecs_vec_get_t(&todo, ecs_entity_t, last)[0];
        ecs_vec_remove_last(&todo);
        box_hierarchy_collect(world, cur, &todo);
        if (layer) {
            ecs_remove_pair(world, cur, Layer, layer);
        }

        box_render_set_depth(world, cur, 0);
        ecs_remove(world, cur, UiNode);
    }

    ecs_vec_fini_t(NULL, &todo, ecs_entity_t);
done:
    return;
}

/** Hierarchy observer: the tree an entity joins or leaves must be walked again.
 *
 * An entity that leaves a tree keeps the node data of that tree, which the walk
 * no longer reaches, so the data is dropped here unless the entity is on its
 * way out of the world.
 *
 * @param it Iterator over the entities that changed parent
 */
static void box_ui_on_parent(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    UiState *state = box_ui_state(world);
    if (!state->trees) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (it->event != EcsOnRemove) {
            box_ui_mark(world, e);
            continue;
        }

        const UiNode *node = ecs_get(world, e, UiNode);
        if (!node) {
            continue;
        }

        const ecs_type_t *type = it->other_table ?
            ecs_table_get_type(it->other_table) : NULL;
        box_ui_mark(world, node->root);
        if (type && type->count) {
            box_ui_drop(it->stage, e);
        }
    }

done:
    return;
}

/** Instance observer: a node that gains a shape needs its depth bias.
 *
 * @param it Iterator over the entities that gained an instance
 */
static void box_ui_on_instance(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);
    if (!state->trees) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_ui_mark(it->world, it->entities[i]);
    }

done:
    return;
}

/** Tree observer: count the trees of the world so a world without one pays none.
 *
 * @param it Iterator over the roots that gained or lost their state
 */
static void box_ui_on_tree(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);

    state->trees += it->event == EcsOnAdd ? it->count : -it->count;
}

void box_ui_tree_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(UiImpl), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_ui_on_tree
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_ui_on_parent
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(EcsParent) }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = box_ui_on_parent
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RenderInstance), .inout = EcsInOutNone }},
        .events = { EcsOnAdd },
        .callback = box_ui_on_instance
    });
}
