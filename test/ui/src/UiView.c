/**
 * @file UiView.c
 * @brief Tests for the view of a UI tree and how it composites over the scene.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void UiView_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
    box_packet_init(&packet);
    packet.width = 960;
    packet.height = 540;
    packet.clear[3] = 1.0f;
}

void UiView_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void UiView_camera_is_orthographic_in_logical_pixels(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    box_ui_update(w);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    box_view_t *view = box_vec_at(&packet.views, 1);
    test_assert(fabsf(view->proj[5] - 2.0f / 540.0f) < 1e-9f);
    test_assert(fabsf(view->proj[0] - 2.0f / 960.0f) < 1e-9f);
    test_flt(view->eye[2], -BOX_UI_DEPTH);
    test_assert(fabsf(view->view[14] - BOX_UI_DEPTH) < 1e-3f);
    test_flt(ecs_get(w, ecs_get(w, root, UiImpl)->camera, Camera)->ortho_height, 540);
}

void UiView_view_follows_a_new_height(void)
{
    // ui/ui_view
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    box_ui_update(w);

    ecs_get_mut(w, root, Ui)->height = 270;
    ecs_modified(w, root, Ui);
    box_ui_update(w);
    box_render_extract(w, &packet);

    box_view_t *view = box_vec_at(&packet.views, 1);
    test_assert(fabsf(view->proj[5] - 2.0f / 270.0f) < 1e-9f);
    test_flt(ecs_get(w, ecs_get(w, root, UiImpl)->camera, Camera)->ortho_height, 270);
}

void UiView_view_follows_the_points_of_the_surface(void)
{
    // ui/ui_view
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 1920;
    surface->height = 1080;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    box_ui_update(w);

    test_flt(ecs_get(w, ecs_get(w, root, UiImpl)->camera, Camera)->ortho_height, 540);

    surface = ecs_singleton_ensure(w, Surface);
    surface->point_height = 720;
    box_ui_update(w);

    test_flt(ecs_get(w, ecs_get(w, root, UiImpl)->camera, Camera)->ortho_height, 720);
}

void UiView_scale_divides_the_height_of_the_surface(void)
{
    // ui/ui_view
    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 960;
    surface->height = 540;
    surface->point_width = 960;
    surface->point_height = 540;

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->scale = 2;
    ecs_modified(w, root, Ui);
    box_ui_update(w);

    test_flt(ecs_get(w, ecs_get(w, root, UiImpl)->camera, Camera)->ortho_height, 270);
}

void UiView_view_renders_after_the_view_of_the_scene(void)
{
    // ui/ui_view
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -8});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t scene = ecs_new(w);
    ecs_add(w, scene, View);
    ecs_get_mut(w, scene, View)->camera = camera;
    ecs_modified(w, scene, View);

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    box_ui_update(w);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    test_int(((box_view_t*)box_vec_at(&packet.views, 0))->priority, 0);
    test_int(((box_view_t*)box_vec_at(&packet.views, 1))->priority, BOX_UI_PRIORITY);
    test_flt(((box_view_t*)box_vec_at(&packet.views, 0))->eye[2], -8);
}

void UiView_scene_view_is_synthesized_next_to_a_ui_view(void)
{
    // ui/ui_view
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 2, -6});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    box_ui_update(w);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    box_view_t *scene = box_vec_at(&packet.views, 0);
    test_flt(scene->eye[2], -6);
    test_flt(scene->eye[1], 2);
    test_assert(scene->layer_mask != 0);
}

void UiView_scene_view_hides_the_tree(void)
{
    // ui/ui_view
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -8});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t prop = ecs_new(w);
    ecs_set(w, prop, Position, {0, 0, 0});
    ecs_set(w, prop, Box, {1, 1, 1});

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_set(w, root, Rect, {400, 300});
    box_ui_update(w);
    box_render_extract(w, &packet);

    box_view_t *scene = box_vec_at(&packet.views, 0);
    box_view_t *ui = box_vec_at(&packet.views, 1);
    uint32_t node_mask = ecs_get(w, root, RenderInstance)->layer_mask;
    uint32_t prop_mask = ecs_get(w, prop, RenderInstance)->layer_mask;
    test_assert(node_mask != 0);
    test_int(prop_mask, 0);
    test_bool(box_view_shows(scene, node_mask), false);
    test_bool(box_view_shows(ui, node_mask), true);
    test_bool(box_view_shows(scene, prop_mask), true);
    test_bool(box_view_shows(ui, prop_mask), false);
}

void UiView_ui_view_draws_the_tree_only(void)
{
    // ui/ui_view
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -8});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t prop = ecs_new(w);
    ecs_set(w, prop, Position, {3, 0, 0});
    ecs_set(w, prop, Box, {1, 1, 1});
    ecs_add(w, prop, DynamicTransform);

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_entity_t node = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_set(w, node, Position, {40, 0, 0});
    ecs_set(w, node, Rect, {200, 100});
    ecs_add(w, node, DynamicTransform);
    box_ui_update(w);
    box_render_extract(w, &packet);

    box_view_t *scene = box_vec_at(&packet.views, 0);
    box_view_t *ui = box_vec_at(&packet.views, 1);
    test_int(scene->dynamic_count, 1);
    test_int(ui->dynamic_count, 1);

    box_dynamic_batch_t *scene_batch = box_vec_at(&packet.dynamic_batches, scene->dynamic_first);
    box_dynamic_batch_t *ui_batch = box_vec_at(&packet.dynamic_batches, ui->dynamic_first);
    box_instance_t *scene_inst = box_vec_at(&packet.dynamic_instances, scene_batch->first);
    box_instance_t *ui_inst = box_vec_at(&packet.dynamic_instances, ui_batch->first);
    test_int(scene_batch->count, 1);
    test_int(ui_batch->count, 1);
    test_flt(scene_inst->transform[3], 3);
    test_flt(ui_inst->transform[3], 40);
    test_flt(ui_inst->transform[11], -BOX_UI_STEP);
}

void UiView_ui_view_has_no_sun_no_lights_and_a_flat_environment(void)
{
    // ui/ui_view
    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t lamp = ecs_new(w);
    ecs_set(w, lamp, Position, {0, 0, 0});
    ecs_set(w, lamp, PointLight, {.intensity = 5, .range = 20});

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    box_ui_update(w);
    box_render_extract(w, &packet);

    box_view_t *scene = box_vec_at(&packet.views, 0);
    box_view_t *ui = box_vec_at(&packet.views, 1);
    test_flt(scene->sun_intensity, 3);
    test_flt(ui->sun_intensity, 0);
    test_int(ui->light_count, 0);
    test_int(ui->sky, 0);
    test_int(ui->shadow_cascades, 0);
    test_flt(ui->env_sky[0], 1.0f);
    test_flt(ui->env_ground[0], 1.0f);
    test_flt(ui->env_intensity, 1.0f);
    test_assert(ui->env_generation != scene->env_generation);
}

void UiView_two_trees_reach_their_own_view(void)
{
    // ui/ui_view
    ecs_entity_t first = ecs_new(w);
    ecs_add(w, first, Ui);
    ecs_get_mut(w, first, Ui)->height = 540;
    ecs_modified(w, first, Ui);
    ecs_set(w, first, Rect, {100, 100});

    ecs_entity_t second = ecs_new(w);
    ecs_add(w, second, Ui);
    ecs_get_mut(w, second, Ui)->height = 270;
    ecs_modified(w, second, Ui);
    ecs_set(w, second, Rect, {100, 100});
    box_ui_update(w);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 3);
    uint32_t first_mask = ecs_get(w, first, RenderInstance)->layer_mask;
    uint32_t second_mask = ecs_get(w, second, RenderInstance)->layer_mask;
    test_assert(first_mask != second_mask);

    int32_t shown = 0;
    uint32_t i;
    for (i = 1; i < packet.views.count; i ++) {
        box_view_t *view = box_vec_at(&packet.views, i);
        test_bool(box_view_shows(view, first_mask) && box_view_shows(view, second_mask),
            false);
        shown += box_view_shows(view, first_mask) ? 1 : 0;
    }

    test_int(shown, 1);
}

void UiView_existing_scene_view_is_not_synthesized_twice(void)
{
    // ui/ui_view
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, -8});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t scene = ecs_new(w);
    ecs_add(w, scene, View);
    View *view = ecs_get_mut(w, scene, View);
    view->camera = camera;
    view->priority = 5;
    ecs_modified(w, scene, View);

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    box_ui_update(w);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    test_int(((box_view_t*)box_vec_at(&packet.views, 0))->priority, 5);
}
