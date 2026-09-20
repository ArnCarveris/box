/**
 * @file ui/tree.c
 * @brief Detects the nodes of a UI tree and gives them their draw order.
 *
 * The walk of a tree is a pre-order walk, so a node is numbered after its
 * parent and after the subtrees of its earlier siblings. The number becomes the
 * depth bias of its instance, which is what draws a child over its parent. The
 * walk leaves the nodes it found in the places of the registry, which the
 * layout pass reads instead of walking the tree a second time.
 *
 * A tree that was walked drops the cached hit of the picking module, because an
 * element that appeared, moved or grew changes what a still cursor points at
 * and a pick only runs again on its own while the cursor moves.
 *
 * A change that only asks for a new layout is marked on the node it was made
 * on. The walk resolves such a mark to the nearest ancestor whose box the
 * change cannot resize and lays out that subtree alone, so one moved node of a
 * large tree costs its own branch instead of everything the root holds. A
 * change of the structure of a tree still walks and numbers all of it, because
 * the place of every node after the new one moves with it.
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
        impl->relayout = true;
    }

done:
    return;
}

void box_ui_mark_layout(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const UiNode *node = ecs_get(world, e, UiNode);
    ecs_entity_t root = node ? node->root : box_ui_root(world, e);
    if (!root) {
        goto done;
    }

    UiImpl *impl = ecs_get_mut(world, root, UiImpl);
    if (!impl) {
        goto done;
    }

    impl->relayout = true;
    UiMark *mark = ecs_vec_append_t(NULL, &box_ui_state(world)->marks, UiMark);
    mark->root = root;
    mark->node = node ? e : root;
    mark->order = 0;
done:
    return;
}

/** True when the box of a node is worked out from the boxes of its children.
 *
 * Such a node changes size when a node below it does, which moves it and its
 * siblings in the layout of its parent, so a change below it is laid out from
 * that parent on. A Rect the layout wrote itself is the content size of the
 * last pass, not a box of its own, so it does not end the walk.
 *
 * @param world The world
 * @param e The node
 * @return True when a side of the box of the node comes from its content
 */
static bool box_ui_content_sized(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const Layout *l = ecs_get(world, e, Layout);
    bool result = false;
    if (l) {
        const Rect *r = ecs_has_id(world, e, UiContentRect) ?
            NULL : ecs_get(world, e, Rect);
        float width = l->width != 0 ? l->width : (r ? r->width : 0);
        float height = l->height != 0 ? l->height : (r ? r->height : 0);

        result = width == 0 || height == 0;
    }

    return result;
}

/** Node the layout of a change has to start at.
 *
 * The change can resize the node it was made on, which moves the children of
 * the node that places them, so the pass starts at the parent of the changed
 * node. It walks further up while that parent is sized by its content, because
 * its own box changes with it, and stops at the first ancestor with a box of
 * its own. The root of the tree ends the walk and is laid out as a whole.
 *
 * @param world The world
 * @param e The changed node
 * @param root The root of the tree that holds it
 * @return The node the layout starts at, the root when the walk reached it
 */
static ecs_entity_t box_ui_subtree(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t root)
{
    ecs_entity_t cur = e != root ? box_hierarchy_parent(world, e) : root;
    while (cur && cur != root && box_ui_content_sized(world, cur)) {
        cur = box_hierarchy_parent(world, cur);
    }

    return cur ? cur : root;
}

/** True when a subtree is already in the list the layout of a tree visits.
 *
 * @param state The registry
 * @param node The root of the subtree
 * @return True when the list holds the node
 */
static bool box_ui_selected(
    const UiState *state,
    ecs_entity_t node)
{
    bool result = false;
    int32_t i, count = ecs_vec_count(&state->subtrees);
    for (i = 0; i < count && !result; i ++) {
        result = ecs_vec_get_t(&state->subtrees, UiMark, i)->node == node;
    }

    return result;
}

/** Put the subtrees the layout visits in tree order, which is place order.
 *
 * @param state The registry
 */
static void box_ui_sort(
    UiState *state)
{
    UiMark *marks = ecs_vec_first_t(&state->subtrees, UiMark);
    int32_t i, count = ecs_vec_count(&state->subtrees);
    for (i = 1; i < count; i ++) {
        UiMark mark = marks[i];
        int32_t j = i;
        while (j > 0 && marks[j - 1].order > mark.order) {
            marks[j] = marks[j - 1];
            j --;
        }

        marks[j] = mark;
    }
}

/** Resolve the marks of a tree into the subtrees its layout has to visit.
 *
 * The tree is laid out as a whole when a change reached its root, when a mark
 * names a node the last walk did not place, and when the changes are so many
 * that resolving them costs more than the pass they save.
 *
 * @param world The world
 * @param root The root of the tree
 * @return True when every change of the tree resolved to a subtree below it
 */
static bool box_ui_select(
    ecs_world_t *world,
    ecs_entity_t root)
{
    UiState *state = box_ui_state(world);
    bool result = false, whole = false;
    int32_t i, count = ecs_vec_count(&state->marks);

    ecs_vec_clear(&state->subtrees);
    if (count > BOX_UI_MARK_MAX) {
        goto done;
    }

    for (i = 0; i < count && !whole; i ++) {
        UiMark mark = ecs_vec_get_t(&state->marks, UiMark, i)[0];
        if (mark.root != root) {
            continue;
        }

        ecs_entity_t node = ecs_is_alive(world, mark.node) ?
            box_ui_subtree(world, mark.node, root) : root;
        const UiNode *place = node != root ? ecs_get(world, node, UiNode) : NULL;
        result = true;
        if (!place || place->root != root || !ecs_has(world, node, Bounds)) {
            whole = true;
            continue;
        }

        if (!box_ui_selected(state, node)) {
            UiMark *sub = ecs_vec_append_t(NULL, &state->subtrees, UiMark);

            sub->root = root;
            sub->node = node;
            sub->order = place->order;
        }
    }

    if (whole || ecs_vec_count(&state->subtrees) > BOX_UI_SUBTREE_MAX) {
        result = false;
    }

    if (result) {
        box_ui_sort(state);
    }

done:
    return result;
}

/** Give one node its place in the tree, its layer and its depth bias.
 *
 * @param world The world
 * @param e The node
 * @param root The root of the tree
 * @param layer Layer the node joins, of its tree or of a group above it
 * @param order Index of the node in the walk
 * @param depth Offset along the world z axis the node is drawn at
 */
static void box_ui_number(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t root,
    ecs_entity_t layer,
    int32_t order,
    float depth)
{
    UiNode *node = ecs_get_mut(world, e, UiNode);
    ecs_entity_t was = node ? node->layer : 0;
    if (node && was == layer) {
        node->root = root;
        node->order = order;
        goto done;
    }

    if (!node) {
        ecs_set(world, e, UiNode, { .root = root, .layer = 0, .order = order });
    }

    if (was) {
        ecs_remove_pair(world, e, Layer, was);
    }

    if (layer && !ecs_has_pair(world, e, Layer, layer)) {
        ecs_add_pair(world, e, Layer, layer);
    }

    node = ecs_get_mut(world, e, UiNode);
    if (node) {
        node->root = root;
        node->layer = layer;
        node->order = order;
    }

done:
    box_render_set_depth(world, e, depth);
}

/** Collect the nodes below a node into the places of the registry, in pre-order.
 *
 * @param world The world
 * @param e The root of the tree, or the root of one subtree of it
 * @param tree The private state of the root of the tree
 * @return The nodes the walk found, the node it started at included
 */
static int32_t box_ui_collect(
    ecs_world_t *world,
    ecs_entity_t e,
    const UiImpl *tree)
{
    UiState *state = box_ui_state(world);

    ecs_vec_clear(&state->places);
    ecs_vec_clear(&state->stack);
    UiVisit *first = ecs_vec_append_t(NULL, &state->stack, UiVisit);
    first->e = e;
    first->parent = -1;
    first->aux = false;
    first->composite = false;

    bool quads = state->texts || state->shadows || state->groups;
    bool alpha = state->opacities != 0;
    while (ecs_vec_count(&state->stack)) {
        int32_t last = ecs_vec_count(&state->stack) - 1;
        UiVisit visit = ecs_vec_get_t(&state->stack, UiVisit, last)[0];
        ecs_vec_remove_last(&state->stack);

        int32_t index = ecs_vec_count(&state->places);
        UiPlace *place = ecs_vec_append_t(NULL, &state->places, UiPlace);
        memset(place, 0, sizeof(UiPlace));
        place->e = visit.e;
        place->parent = visit.parent;
        place->aux = visit.aux;
        place->composite = visit.composite;
        bool heir = !visit.composite &&
            (!alpha || !ecs_has(world, visit.e, Opacity));
        if (visit.parent >= 0 && heir) {
            ecs_vec_get_t(&state->places, UiPlace, visit.parent)->kids ++;
        }

        ecs_vec_clear(&state->nodes);
        box_hierarchy_collect(world, visit.e, &state->nodes);

        int32_t i;
        for (i = ecs_vec_count(&state->nodes) - 1; i >= 0; i --) {
            ecs_entity_t child = ecs_vec_get_t(&state->nodes, ecs_entity_t, i)[0];
            bool skip = child == tree->view || child == tree->camera ||
                ecs_has(world, child, Ui);
            if (!skip) {
                UiVisit *next = ecs_vec_append_t(NULL, &state->stack, UiVisit);
                next->e = child;
                next->parent = index;
                next->aux = quads && ecs_has_id(world, child, UiQuad);
                next->composite = next->aux && ecs_has_id(world, child, UiComposite);
            }
        }
    }

    return ecs_vec_count(&state->places);
}

/** Number the collected nodes of a tree and join them to its layer.
 *
 * The bias of a node is a step per place of the walk, so a node is drawn over
 * everything before it. A shadow quad is the one node that goes the other way:
 * it belongs to the element that casts it and is drawn part of a step behind
 * it, which leaves it over everything that comes before that element.
 *
 * @param world The world
 * @param e The root of the tree
 * @param tree The private state of the root
 */
static void box_ui_number_places(
    ecs_world_t *world,
    ecs_entity_t e,
    const UiImpl *tree)
{
    UiState *state = box_ui_state(world);
    bool groups = state->groups != 0;
    int32_t i, count = ecs_vec_count(&state->places);
    for (i = 0; i < count; i ++) {
        UiPlace *place = ecs_vec_get_t(&state->places, UiPlace, i);
        UiPlace *parent = place->parent >= 0 ?
            ecs_vec_get_t(&state->places, UiPlace, place->parent) : NULL;
        ecs_entity_t outer = parent ? parent->layer : tree->layer;
        float order = (float)i;
        if (place->composite) {
            outer = parent && parent->parent >= 0 ?
                ecs_vec_get_t(&state->places, UiPlace, parent->parent)->layer :
                tree->layer;
            order = (float)place->parent;
        } else if (place->aux && ecs_has_id(world, place->e, UiShadow)) {
            order = (float)place->parent - BOX_UI_SHADOW_STEP;
        }

        const GroupImpl *group = groups && !place->aux ?
            ecs_get(world, place->e, GroupImpl) : NULL;
        place->layer = group ? group->layer : outer;
        ecs_entity_t layer = place->layer;

        box_ui_number(world, place->e, e, layer, i, -order * BOX_UI_STEP);
    }
}

/** Lay out the subtrees the marks of a tree selected, in tree order.
 *
 * A subtree an earlier pass already covered is skipped: the places of a walk
 * are contiguous, so a node whose place falls in the range the earlier subtree
 * spans was laid out with it.
 *
 * @param world The world
 * @param root The root of the tree
 * @param tree The private state of the root
 * @return True when a pass made or dropped nodes, which asks for a whole walk
 */
static bool box_ui_walk_subtrees(
    ecs_world_t *world,
    ecs_entity_t root,
    const UiImpl *tree)
{
    UiState *state = box_ui_state(world);
    int32_t i, count = ecs_vec_count(&state->subtrees), end = -1;
    for (i = 0; i < count; i ++) {
        UiMark mark = ecs_vec_get_t(
            &box_ui_state(world)->subtrees, UiMark, i)[0];
        if (mark.order < end) {
            continue;
        }

        end = mark.order + box_ui_collect(world, mark.node, tree);
        box_ui_layout(world, root);
    }

    state = box_ui_state(world);

    return state->respawn || state->instanced;
}

void box_ui_walk(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const UiImpl *stored = ecs_get(world, e, UiImpl);
    if (!stored) {
        goto done;
    }

    UiImpl tree = *stored;
    UiState *state = box_ui_state(world);
    state->instanced = false;
    state->respawn = false;
    int32_t count = tree.count;
    bool number = tree.dirty;
    bool whole = tree.dirty || !tree.relayout || !box_ui_select(world, e);
    if (!whole && box_ui_walk_subtrees(world, e, &tree)) {
        tree.relayout = false;
        number = true;
        whole = true;
    }

    if (whole) {
        count = box_ui_collect(world, e, &tree);
        if (tree.relayout) {
            box_ui_layout(world, e);
            state = box_ui_state(world);
            number = number || state->instanced || state->respawn;
            if (state->respawn) {
                count = box_ui_collect(world, e, &tree);
                if (box_ui_state(world)->scrolls) {
                    box_ui_scroll_clip(world);
                }
            }
        }

        if (number) {
            box_ui_number_places(world, e, &tree);
        }
    }

    UiImpl *impl = ecs_get_mut(world, e, UiImpl);
    if (impl) {
        impl->count = count;
        impl->dirty = false;
        impl->relayout = false;
    }

    box_picking_invalidate(world);
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
    ecs_vec_t todo;

    ecs_vec_init_t(NULL, &todo, ecs_entity_t, 0);
    ecs_vec_append_t(NULL, &todo, ecs_entity_t)[0] = e;
    while (ecs_vec_count(&todo)) {
        int32_t last = ecs_vec_count(&todo) - 1;
        ecs_entity_t cur = ecs_vec_get_t(&todo, ecs_entity_t, last)[0];
        ecs_vec_remove_last(&todo);
        box_hierarchy_collect(world, cur, &todo);
        const UiNode *left = ecs_get(world, cur, UiNode);
        ecs_entity_t layer = left ? left->layer : 0;
        if (layer) {
            ecs_remove_pair(world, cur, Layer, layer);
        }

        box_render_set_depth(world, cur, 0);
        ecs_remove(world, cur, UiNode);
        ecs_remove(world, cur, Bounds);
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
    if (!state->trees || state->busy) {
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

    state->instanced = true;
    if (state->busy) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_ui_mark(it->world, it->entities[i]);
    }

done:
    return;
}

/** Layout observer: a node that was sized, moved or pinned needs a new layout.
 *
 * The observer is matched with UiNode, so it hears the nodes of a tree only and
 * the entities of the scene around them cost nothing. A component set on an
 * entity before it joins a tree is picked up by the walk that adds it. A Rect
 * an author sets is a box of its own again, whatever the layout wrote before.
 *
 * @param it Iterator over the nodes that changed
 */
static void box_ui_on_layout(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);
    if (!state->trees || state->busy) {
        goto done;
    }

    bool rect = it->event == EcsOnSet && ecs_field_id(it, 0) == ecs_id(Rect);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (rect && ecs_has_id(it->world, it->entities[i], UiContentRect)) {
            ecs_remove_id(it->world, it->entities[i], UiContentRect);
        }

        box_ui_mark_layout(it->world, it->entities[i]);
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

    ecs_id_t placed[] = {
        ecs_id(Position), ecs_id(Rect), ecs_id(Layout), ecs_id(Anchor),
        ecs_id(Text), ecs_id(Font), ecs_id(Rgb), ecs_id(BoxShadow),
        ecs_id(Opacity), ecs_id(Fill), ecs_id(Scroll), ecs_id(WorldAnchor)
    };

    int32_t i;
    for (i = 0; i < 12; i ++) {
        ecs_observer(world, {
            .query.terms = {
                { .id = placed[i], .inout = EcsInOutNone },
                { .id = ecs_id(UiNode), .inout = EcsInOutFilter }
            },
            .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
            .callback = box_ui_on_layout
        });
    }
}
