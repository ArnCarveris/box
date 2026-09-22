/**
 * @file Group.c
 * @brief Tests for group transparency: the trigger, the target and the composite.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"
#include <math.h>

static ecs_world_t *w;

void Group_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
}

void Group_teardown(void)
{
    ecs_fini(w);
}

void Group_opaque_subtree_takes_no_target(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    test_assert(!ecs_has(w, panel, GroupImpl));
    test_int(box_ui_state(w)->groups, 0);
}

void Group_transparent_leaf_takes_no_target(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    box_ui_update(w);

    test_assert(!ecs_has(w, panel, GroupImpl));
    test_int(box_ui_state(w)->groups, 0);
}

void Group_child_with_its_own_opacity_takes_no_target(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    ecs_set(w, child, Opacity, {0.5f});
    box_ui_update(w);

    test_assert(!ecs_has(w, panel, GroupImpl));
}

void Group_transparent_subtree_gets_a_view_and_a_target(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    test_assert(group != NULL);
    test_assert(ecs_has(w, group->target, RenderTarget));
    test_assert(ecs_has(w, group->view, View));
    test_assert(ecs_has(w, group->camera, Camera));
    test_bool(ecs_get(w, group->camera, Camera)->orthographic, true);

    const View *view = ecs_get(w, group->view, View);
    test_uint(view->camera, group->camera);
    test_uint(view->target, group->target);
    test_uint(view->layers[0], group->layer);
    test_bool(view->strict_layers, true);
    test_flt(view->clear_alpha, 0);
}

void Group_target_is_the_size_of_the_bounds(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    const RenderTarget *target = ecs_get(w, group->target, RenderTarget);
    test_int(target->width, 200);
    test_int(target->height, 100);
    test_flt(ecs_get(w, group->camera, Camera)->ortho_height, 100);
    test_flt(ecs_get(w, group->quad, Rect)->width, 200);
    test_flt(ecs_get(w, group->quad, Rect)->height, 100);
}

void Group_target_follows_the_bounds(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    ecs_set(w, panel, Rect, {320, 180});
    box_ui_update(w);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    const RenderTarget *target = ecs_get(w, group->target, RenderTarget);
    test_int(target->width, 320);
    test_int(target->height, 180);
}

void Group_composite_quad_carries_the_opacity(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.25f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    test_assert(ecs_has_id(w, group->quad, UiComposite));
    test_flt(ecs_get(w, group->quad, Opacity)->value, 0.25f);
    test_uint(ecs_get(w, group->quad, RenderGroup)->target, group->target);
    test_flt(ecs_get(w, group->quad, RenderInstance)->depth,
        ecs_get(w, panel, RenderInstance)->depth);
}

void Group_subtree_joins_the_layer_of_its_group(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    ecs_entity_t tree = ecs_get(w, root, UiImpl)->layer;
    test_assert(ecs_has_pair(w, panel, Layer, group->layer));
    test_assert(ecs_has_pair(w, child, Layer, group->layer));
    test_assert(!ecs_has_pair(w, panel, Layer, tree));
    test_assert(ecs_has_pair(w, group->quad, Layer, tree));
    test_assert(!ecs_has_pair(w, group->quad, Layer, group->layer));
    test_assert(ecs_has_pair(w, root, Layer, tree));
}

void Group_nested_groups_render_inside_out(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t outer = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, outer, Rect, {200, 100});
    ecs_set(w, outer, Opacity, {0.5f});
    ecs_entity_t inner = ecs_new_w_pair(w, EcsChildOf, outer);
    ecs_set(w, inner, Rect, {120, 60});
    ecs_set(w, inner, Opacity, {0.5f});
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, inner);
    ecs_set(w, leaf, Rect, {40, 20});
    box_ui_update(w);
    box_ui_update(w);

    const GroupImpl *a = ecs_get(w, outer, GroupImpl);
    const GroupImpl *b = ecs_get(w, inner, GroupImpl);
    test_assert(a != NULL);
    test_assert(b != NULL);

    int32_t tree = ecs_get(w, root, Ui)->priority;
    int32_t outer_priority = ecs_get(w, a->view, View)->priority;
    int32_t inner_priority = ecs_get(w, b->view, View)->priority;
    test_assert(inner_priority < outer_priority);
    test_assert(outer_priority < tree);
    test_assert(ecs_has_pair(w, leaf, Layer, b->layer));
    test_assert(ecs_has_pair(w, b->quad, Layer, a->layer));
}

void Group_opaque_again_drops_the_group(void)
{
    // ui/ui_visuals
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);
    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    ecs_entity_t view = group->view;
    ecs_entity_t quad = group->quad;

    ecs_set(w, panel, Opacity, {1.0f});
    box_ui_update(w);

    test_assert(!ecs_has(w, panel, GroupImpl));
    test_assert(!ecs_is_alive(w, view));
    test_assert(!ecs_is_alive(w, quad));
    test_int(box_ui_state(w)->groups, 0);
    test_assert(ecs_has_pair(w, child, Layer, ecs_get(w, root, UiImpl)->layer));
}

void Group_composite_reaches_the_packet_with_its_target(void)
{
    // ui/ui_visuals
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;

    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -8});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t scene_view = ecs_new(w);
    ecs_set(w, scene_view, View, {.camera = camera, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);
    box_render_extract(w, &packet);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    uint32_t handle = ecs_get(w, group->target, RenderTargetImpl)->handle;
    test_assert(handle != 0);

    bool seen = false;
    uint32_t i;
    for (i = 0; i < packet.transparent.count; i ++) {
        box_transparent_t *entry = box_vec_at(&packet.transparent, i);
        if (entry->group == handle) {
            test_flt(entry->data.color[3], 0.5f);
            seen = true;
        }
    }

    test_bool(seen, true);
    test_int(packet.views.count, 3);
    box_packet_fini(&packet);
}

void Group_target_follows_the_device_scale(void)
{
    // ore_engine/console_text_blurry
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 960;
    surface->height = 540;
    surface->actual_width = 1920;
    surface->actual_height = 1080;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Rect, {200, 100});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    test_assert(group != NULL);
    test_int(group->width, 400);
    test_int(group->height, 200);
    const RenderTarget *target = ecs_get(w, group->target, RenderTarget);
    test_assert(target != NULL);
    test_int(target->width, 400);
    test_int(target->height, 200);
    test_flt(ecs_get(w, group->camera, Camera)->ortho_height, 100);
}

void Group_target_edges_land_on_device_pixels(void)
{
    // ore_engine/console_text_blurry
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 960;
    surface->height = 540;
    surface->actual_width = 1920;
    surface->actual_height = 1080;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_entity_t panel = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, panel, Position, {10.3f, 20.7f, 0});
    ecs_set(w, panel, Rect, {200.5f, 100.25f});
    ecs_set(w, panel, Opacity, {0.5f});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, panel);
    ecs_set(w, child, Rect, {80, 40});
    box_ui_update(w);

    const GroupImpl *group = ecs_get(w, panel, GroupImpl);
    test_assert(group != NULL);
    test_int(group->width, 401);
    test_int(group->height, 201);
    const Position *cam = ecs_get(w, group->camera, Position);
    const Rect *quad = ecs_get(w, group->quad, Rect);
    test_flt(quad->width, 200.5f);
    test_flt(quad->height, 100.5f);
    float left = (cam->x - quad->width * 0.5f) * 2.0f;
    float top = (cam->y + quad->height * 0.5f) * 2.0f;
    test_flt(left, roundf(left));
    test_flt(top, roundf(top));
    test_flt(ecs_get(w, group->camera, Camera)->ortho_height, 100.5f);
}
