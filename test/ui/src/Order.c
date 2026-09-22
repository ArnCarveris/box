/**
 * @file Order.c
 * @brief Tests for the depth a node gets from its place in the walk of its tree.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Order_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;
    packet.clear[3] = 1.0f;
}

void Order_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Order_child_is_in_front_of_its_parent(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 300});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, child, Rect, {200, 100});
    box_ui_update(w);

    test_flt(ecs_get(w, root, RenderInstance)->depth, 0);
    test_flt(ecs_get(w, child, RenderInstance)->depth, -BOX_UI_STEP);
    test_assert(ecs_get(w, child, RenderInstance)->depth <
        ecs_get(w, root, RenderInstance)->depth);
}

void Order_nested_children_are_numbered_depth_first(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t first = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t inner = ecs_new_w_pair(w, EcsChildOf, first);
    ecs_entity_t second = ecs_new_w_pair(w, EcsChildOf, root);
    box_ui_update(w);

    test_int(ecs_get(w, root, UiNode)->order, 0);
    test_int(ecs_get(w, first, UiNode)->order, 1);
    test_int(ecs_get(w, inner, UiNode)->order, 2);
    test_int(ecs_get(w, second, UiNode)->order, 3);
}

void Order_later_sibling_is_in_front_of_the_earlier_one(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t back = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, back, Rect, {200, 100});
    ecs_entity_t front = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, front, Rect, {200, 100});
    box_ui_update(w);

    test_assert(ecs_get(w, front, RenderInstance)->depth <
        ecs_get(w, back, RenderInstance)->depth);
}

void Order_reordered_children_swap_their_depth(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t a = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, a, Rect, {200, 100});
    ecs_entity_t b = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, b, Rect, {200, 100});
    box_ui_update(w);
    test_int(ecs_get(w, a, UiNode)->order, 1);
    test_int(ecs_get(w, b, UiNode)->order, 2);

    ecs_entity_t order[2] = { b, a };
    ecs_set_child_order(w, root, order, 2);
    ecs_modified(w, root, Ui);
    box_ui_update(w);

    test_int(ecs_get(w, b, UiNode)->order, 1);
    test_int(ecs_get(w, a, UiNode)->order, 2);
    test_assert(ecs_get(w, a, RenderInstance)->depth <
        ecs_get(w, b, RenderInstance)->depth);
}

void Order_node_added_later_is_in_front_of_its_siblings(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t back = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, back, Rect, {200, 100});
    box_ui_update(w);

    ecs_entity_t front = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, front, Rect, {200, 100});
    box_ui_update(w);

    test_int(ecs_get(w, front, UiNode)->order, 2);
    test_assert(ecs_get(w, front, RenderInstance)->depth <
        ecs_get(w, back, RenderInstance)->depth);
}

void Order_depth_reaches_the_instance_of_the_packet(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Position, {10, 0, 0});
    ecs_set(w, root, Rect, {400, 300});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, child, Position, {5, 0, 0});
    ecs_set(w, child, Rect, {200, 100});
    box_ui_update(w);
    box_render_extract(w, &packet);

    bool seen_root = false;
    bool seen_child = false;
    uint32_t i;
    for (i = 0; i < packet.static_updates.count; i ++) {
        box_static_update_t *update = box_vec_at(&packet.static_updates, i);
        if (fabsf(update->data.transform[3] - 10.0f) < 1e-4f) {
            test_flt(update->data.transform[11], 0);
            seen_root = true;
        }

        if (fabsf(update->data.transform[3] - 15.0f) < 1e-4f) {
            test_flt(update->data.transform[11], -BOX_UI_STEP);
            seen_child = true;
        }
    }

    test_bool(seen_root, true);
    test_bool(seen_child, true);
}

void Order_depth_orders_the_sorted_draws_of_the_view(void)
{
    // ui/ui_view
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -8});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t scene = ecs_new(w);
    ecs_set(w, scene, View, {.camera = camera, .enabled = true});
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_set(w, root, Position, {0, 0, 0});
    ecs_set(w, root, Rect, {400, 300});
    ecs_set(w, root, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, child, Position, {40, 0, 0});
    ecs_set(w, child, Rect, {200, 100});
    ecs_set(w, child, Opacity, {0.5f});
    box_ui_update(w);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    box_view_t *view = box_vec_at(&packet.views, 1);
    test_int(view->transparent_count, 2);

    box_transparent_t *back = box_vec_at(&packet.transparent, view->transparent_first);
    box_transparent_t *front = box_vec_at(&packet.transparent, view->transparent_first + 1);
    test_flt(back->data.transform[3], 0);
    test_flt(front->data.transform[3], 40);
    test_assert(front->depth < back->depth);
}

void Order_tree_of_a_thousand_nodes_stays_in_front_of_the_near_plane(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 300});
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, 1000);
    tree[0] = root;
    int32_t i;
    for (i = 1; i < 1000; i ++) {
        tree[i] = ecs_new_w_pair(w, EcsChildOf, tree[(i - 1) / 8]);
        ecs_set(w, tree[i], Rect, {10, 10});
    }

    box_ui_update(w);
    test_int(ecs_get(w, root, UiImpl)->count, 1000);

    int32_t deepest = 0;
    for (i = 0; i < 1000; i ++) {
        const UiNode *node = ecs_get(w, tree[i], UiNode);
        test_assert(node != NULL);
        test_uint(node->root, root);
        deepest = node->order > deepest ? node->order : deepest;
        test_flt(ecs_get(w, tree[i], RenderInstance)->depth,
            -(float)node->order * BOX_UI_STEP);
    }

    test_int(deepest, 999);
    test_assert(-(float)deepest * BOX_UI_STEP > -(BOX_UI_DEPTH - BOX_UI_NEAR));
    ecs_os_free(tree);
}

void Order_dropped_node_loses_its_depth(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, child, Rect, {200, 100});
    box_ui_update(w);
    test_flt(ecs_get(w, child, RenderInstance)->depth, -BOX_UI_STEP);

    ecs_remove_pair(w, child, EcsChildOf, root);
    box_ui_update(w);

    test_flt(ecs_get(w, child, RenderInstance)->depth, 0);
}
