/**
 * @file Tree.c
 * @brief Tests for detecting the nodes of a UI tree and the view that draws it.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

void Tree_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
}

void Tree_teardown(void)
{
    ecs_fini(w);
}

void Tree_root_gets_a_view_a_camera_and_a_layer(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    box_ui_update(w);

    const Ui *ui = ecs_get(w, root, Ui);
    test_assert(ui->view != 0);
    test_assert(ecs_has(w, ui->view, View));

    const View *view = ecs_get(w, ui->view, View);
    test_assert(view->camera != 0);
    test_assert(view->layers[0] != 0);
    test_bool(view->strict_layers, true);
    test_bool(view->shadows, false);
    test_bool(view->effects, false);
    test_bool(view->enabled, true);
    test_int(view->priority, BOX_UI_PRIORITY);
    test_assert(ecs_has(w, view->camera, Camera));
    test_bool(ecs_get(w, view->camera, Camera)->orthographic, true);

    const UiImpl *impl = ecs_get(w, root, UiImpl);
    test_uint(impl->view, ui->view);
    test_uint(impl->camera, view->camera);
    test_uint(impl->layer, view->layers[0]);
    test_bool(impl->owned, true);
}

void Tree_root_without_children_is_one_node(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    box_ui_update(w);

    test_int(ecs_get(w, root, UiImpl)->count, 1);
    test_assert(ecs_has(w, root, UiNode));
    test_int(ecs_get(w, root, UiNode)->order, 0);
    test_uint(ecs_get(w, root, UiNode)->root, root);
}

void Tree_descendants_join_the_tree(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, panel);
    box_ui_update(w);

    const UiImpl *impl = ecs_get(w, root, UiImpl);
    test_int(impl->count, 3);
    test_uint(ecs_get(w, panel, UiNode)->root, root);
    test_uint(ecs_get(w, label, UiNode)->root, root);
    test_assert(ecs_has_pair(w, panel, Layer, impl->layer));
    test_assert(ecs_has_pair(w, label, Layer, impl->layer));
}

void Tree_node_added_later_joins_the_tree(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    box_ui_update(w);
    test_int(ecs_get(w, root, UiImpl)->count, 1);

    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    test_bool(ecs_get(w, root, UiImpl)->dirty, true);
    box_ui_update(w);

    test_int(ecs_get(w, root, UiImpl)->count, 2);
    test_assert(ecs_has(w, panel, UiNode));
    test_assert(ecs_has_pair(w, panel, Layer, ecs_get(w, root, UiImpl)->layer));
}

void Tree_node_that_leaves_the_tree_is_dropped(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t label = ecs_new_w_pair(w, EcsChildOf, panel);
    box_ui_update(w);
    ecs_entity_t layer = ecs_get(w, root, UiImpl)->layer;
    test_assert(ecs_has_pair(w, label, Layer, layer));

    ecs_remove_pair(w, panel, EcsChildOf, root);
    box_ui_update(w);

    test_int(ecs_get(w, root, UiImpl)->count, 1);
    test_assert(!ecs_has(w, panel, UiNode));
    test_assert(!ecs_has(w, label, UiNode));
    test_assert(!ecs_has_pair(w, panel, Layer, layer));
    test_assert(!ecs_has_pair(w, label, Layer, layer));
}

void Tree_nested_root_starts_its_own_tree(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t inner = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_add(w, inner, Ui);
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, inner);
    box_ui_update(w);

    test_int(ecs_get(w, root, UiImpl)->count, 2);
    test_int(ecs_get(w, inner, UiImpl)->count, 2);
    test_uint(ecs_get(w, leaf, UiNode)->root, inner);
    test_uint(ecs_get(w, inner, UiNode)->root, inner);
    test_int(ecs_get(w, inner, UiNode)->order, 0);
    test_uint(ecs_get(w, panel, UiNode)->root, root);
}

void Tree_entity_outside_a_tree_has_no_node(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t outside = ecs_new(w);
    ecs_set(w, outside, Position, {0, 0, 0});
    box_ui_update(w);

    test_assert(!ecs_has(w, outside, UiNode));
    test_uint(box_ui_root(w, outside), 0);
}

void Tree_two_trees_get_their_own_view_and_layer(void)
{
    // ui/ui_view
    ecs_entity_t first = ecs_new(w);
    ecs_add(w, first, Ui);
    ecs_entity_t second = ecs_new(w);
    ecs_add(w, second, Ui);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, second);
    box_ui_update(w);

    const UiImpl *a = ecs_get(w, first, UiImpl);
    const UiImpl *b = ecs_get(w, second, UiImpl);
    test_assert(a->view != b->view);
    test_assert(a->layer != b->layer);
    test_assert(a->camera != b->camera);
    test_uint(ecs_get(w, child, UiNode)->root, second);
    test_assert(ecs_has_pair(w, child, Layer, b->layer));
    test_assert(!ecs_has_pair(w, child, Layer, a->layer));
}

void Tree_authored_view_is_bound_instead_of_a_new_one(void)
{
    // ui/ui_view
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -10});
    ecs_set(w, camera, Camera, {.orthographic = true, .ortho_height = 200});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = camera;
    ecs_modified(w, view, View);

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->view = view;
    ecs_modified(w, root, Ui);
    box_ui_update(w);

    const UiImpl *impl = ecs_get(w, root, UiImpl);
    test_uint(impl->view, view);
    test_uint(impl->camera, camera);
    test_bool(impl->owned, false);
    test_uint(ecs_get(w, view, View)->layers[0], impl->layer);
    test_bool(ecs_get(w, view, View)->strict_layers, true);
    test_flt(ecs_get(w, camera, Camera)->ortho_height, 200);
}

void Tree_removing_ui_deletes_the_view_it_created(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    box_ui_update(w);
    ecs_entity_t view = ecs_get(w, root, Ui)->view;
    ecs_entity_t camera = ecs_get(w, root, UiImpl)->camera;
    test_assert(ecs_is_alive(w, view));

    ecs_remove(w, root, Ui);

    test_assert(!ecs_is_alive(w, view));
    test_assert(!ecs_is_alive(w, camera));
    test_assert(!ecs_has(w, root, UiImpl));
}

void Tree_view_and_camera_are_not_nodes_of_the_tree(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    box_ui_update(w);

    const UiImpl *impl = ecs_get(w, root, UiImpl);
    test_assert(!ecs_has(w, impl->view, UiNode));
    test_assert(!ecs_has(w, impl->camera, UiNode));
    test_int(impl->count, 1);
}

void Tree_root_without_a_position_draws_its_children(void)
{
    // ui/root_position
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Rgb, {200, 40, 40});
    box_ui_update(w);
    box_render_extract(w, &packet);

    const RenderInstance *inst = ecs_get(w, panel, RenderInstance);
    test_assert(inst != NULL);

    bool seen = false;
    uint32_t i;
    for (i = 0; i < packet.static_updates.count; i ++) {
        box_static_update_t *update = box_vec_at(&packet.static_updates, i);
        if (update->slot == inst->slot && update->mesh == inst->mesh) {
            test_flt(update->data.transform[0], 200.0f);
            test_flt(update->data.transform[5], 100.0f);
            test_flt(update->data.transform[3], 0);
            test_flt(update->data.transform[7], 0);
            seen = true;
        }
    }

    test_bool(seen, true);
    box_packet_fini(&packet);
}

void Tree_root_without_a_position_sits_at_the_origin(void)
{
    // ui/root_position
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    box_ui_update(w);

    const WorldTransform *wt = ecs_get(w, root, WorldTransform);
    test_assert(wt != NULL);
    test_flt(wt->m.v[12], 0);
    test_flt(wt->m.v[13], 0);
    test_flt(wt->m.v[14], 0);
    test_flt(wt->m.v[0], 1.0f);

    const Bounds *b = ecs_get(w, panel, Bounds);
    test_assert(b != NULL);
    test_flt(b->x, 0);
    test_flt(b->y, 0);
}
