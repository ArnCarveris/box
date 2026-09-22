/**
 * @file View.c
 * @brief Tests for extracting View entities into the view array of a frame packet.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void View_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    packet.clear[3] = 1.0f;
}

void View_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void View_defaults_of_a_new_view(void)
{
    // views/view
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    const View *v = ecs_get(w, e, View);
    test_bool(v->enabled, true);
    test_bool(v->shadows.enabled, true);
    test_int(v->shadows.cascades, BOX_SHADOW_CASCADES);
    test_bool(v->culling.enabled, true);
    test_int(v->culling.gpu_threshold, BOX_CULL_GPU_THRESHOLD);
    test_bool(v->culling.occlusion, true);
    test_int(ecs_vec_count(&v->effects), 0);
    test_int(ecs_vec_count(&v->material_layers), 0);
    test_uint(v->camera, 0);
    test_uint(v->target, 0);
    test_int(v->priority, 0);
    test_flt(v->clear_alpha, 1.0f);
    test_int(v->clear.r, BOX_VIEW_CLEAR_R);
    test_assert(ecs_has(w, e, ViewImpl));
}

void View_script_sets_culling(void)
{
    test_int(ecs_script_run(w, "culling",
        "using box.*\n"
        "camera { Camera: {fov: 60} }\n"
        "view { View: {camera: camera, culling: {enabled: true, gpu_threshold: 16, occlusion: true, hiz_levels: 3, min_dimension_px: 2}} }\n"
        "disabled_view { View: {camera: camera, culling: {enabled: false}} }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "view");
    test_assert(e != 0);
    const View *settings = ecs_get(w, e, View);
    test_assert(settings != NULL);
    test_bool(settings->culling.enabled, true);
    test_int(settings->culling.gpu_threshold, 16);
    test_bool(settings->culling.occlusion, true);
    test_int(settings->culling.hiz_levels, 3);
    test_flt(settings->culling.min_dimension_px, 2);
    ecs_entity_t disabled = ecs_lookup(w, "disabled_view");
    test_assert(disabled != 0);
    const View *disabled_settings = ecs_get(w, disabled, View);
    test_assert(disabled_settings != NULL);
    test_bool(disabled_settings->culling.enabled, false);
    test_int(ecs_script_run(w, "overlay",
        "using box.view\n"
        "view { View: {camera: camera, culling: {enabled: true, gpu_threshold: 16, occlusion: false, hiz_levels: 3, min_dimension_px: 2}} }\n", NULL), 0);
    settings = ecs_get(w, e, View);
    test_bool(settings->culling.occlusion, false);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 2);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->cull_threshold, 16);
    test_uint(v->occlusion, 0);
    test_uint(v->hiz_levels, 3);
    test_flt(v->min_dimension_px, 2);
    v = box_vec_at(&packet.views, 1);
    test_uint(v->cull_enabled, 0);
}

void View_camera_fills_the_view(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 90});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_flt(v->eye[2], -5);
    test_assert(fabsf(v->view[14] - 5) < 1e-4f);
    test_assert(fabsf(v->proj[5] - 1.0f) < 1e-4f);
    test_assert(fabsf(v->proj[0] - 480.0f / 640.0f) < 1e-4f);
    test_flt(v->proj[11], 1);
    test_int(v->width, 640);
    test_int(v->height, 480);
}

void View_views_render_in_priority_order(void)
{
    // views/view
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, -5});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {0, 0, -9});
    ecs_set(w, b, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(w);
    ecs_add(w, view_a, View);
    View *view_a_v = ecs_get_mut(w, view_a, View);
    view_a_v->camera = a;
    view_a_v->priority = 10;
    ecs_modified(w, view_a, View);
    ecs_entity_t view_b = ecs_new(w);
    ecs_add(w, view_b, View);
    View *view_b_v = ecs_get_mut(w, view_b, View);
    view_b_v->camera = b;
    view_b_v->priority = -3;
    ecs_modified(w, view_b, View);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 2);
    test_flt(((box_view_t*)box_vec_at(&packet.views, 0))->eye[2], -9);
    test_flt(((box_view_t*)box_vec_at(&packet.views, 1))->eye[2], -5);
    test_int(((box_view_t*)box_vec_at(&packet.views, 0))->priority, -3);
}

void View_world_without_a_view_does_not_render(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {1, 2, -6});
    ecs_set(w, cam, Camera, {.fov = 60});
    box_render_extract(w, &packet);
    test_int(packet.views.count, 0);
    test_bool(packet.has_view, false);
}

void View_disabled_view_is_skipped(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(w);
    ecs_add(w, a, View);
    View *a_v = ecs_get_mut(w, a, View);
    a_v->camera = cam;
    a_v->priority = 0;
    ecs_modified(w, a, View);
    ecs_entity_t cam_b = ecs_new(w);
    ecs_set(w, cam_b, Position, {0, 0, -8});
    ecs_set(w, cam_b, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_add(w, b, View);
    View *b_v = ecs_get_mut(w, b, View);
    b_v->camera = cam_b;
    b_v->priority = 1;
    ecs_modified(w, b, View);
    a_v = ecs_get_mut(w, a, View);
    a_v->enabled = false;
    ecs_modified(w, a, View);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 1);
    test_flt(((box_view_t*)box_vec_at(&packet.views, 0))->eye[2], -8);
    test_uint(ecs_get(w, b, ViewImpl)->index, 0);
}

void View_view_without_camera_is_skipped(void)
{
    // views/view
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_log_set_level(-4);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 0);
    test_bool(ecs_get(w, e, ViewImpl)->warned, true);
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(w, &packet);
    ecs_log_set_level(0);
    test_bool(ecs_get(w, e, ViewImpl)->warned, true);
    test_bool(ecs_get(w, e, ViewImpl)->extracted, false);
}

void View_view_with_a_dead_camera_is_skipped(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    View *e_v = ecs_get_mut(w, e, View);
    e_v->camera = cam;
    e_v->priority = 0;
    ecs_modified(w, e, View);
    ecs_delete(w, cam);
    ecs_log_set_level(-4);
    box_render_extract(w, &packet);
    ecs_log_set_level(0);
    test_int(packet.views.count, 0);
    test_bool(packet.has_view, false);
    test_bool(ecs_get(w, e, ViewImpl)->extracted, false);
}

void View_impl_holds_the_view_index(void)
{
    // views/view
    ecs_entity_t cam_a = ecs_new(w);
    ecs_set(w, cam_a, Position, {0, 0, -5});
    ecs_set(w, cam_a, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(w);
    ecs_add(w, a, View);
    View *a_v = ecs_get_mut(w, a, View);
    a_v->camera = cam_a;
    a_v->priority = 5;
    ecs_modified(w, a, View);
    ecs_entity_t cam_b = ecs_new(w);
    ecs_set(w, cam_b, Position, {0, 0, -8});
    ecs_set(w, cam_b, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_add(w, b, View);
    View *b_v = ecs_get_mut(w, b, View);
    b_v->camera = cam_b;
    b_v->priority = 1;
    ecs_modified(w, b, View);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, a, ViewImpl)->index, 1);
    test_uint(ecs_get(w, b, ViewImpl)->index, 0);
    test_bool(ecs_get(w, a, ViewImpl)->extracted, true);
}

void View_environment_of_a_view_overrides_the_singleton(void)
{
    // views/view
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {255, 255, 255},
        .ground_color = {255, 255, 255},
        .intensity = 1
    });
    ecs_entity_t cam_a = ecs_new(w);
    ecs_set(w, cam_a, Position, {0, 0, -5});
    ecs_set(w, cam_a, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(w);
    ecs_add(w, a, View);
    View *a_v = ecs_get_mut(w, a, View);
    a_v->camera = cam_a;
    a_v->priority = 0;
    ecs_modified(w, a, View);
    ecs_entity_t cam_b = ecs_new(w);
    ecs_set(w, cam_b, Position, {0, 0, -8});
    ecs_set(w, cam_b, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_add(w, b, View);
    View *b_v = ecs_get_mut(w, b, View);
    b_v->camera = cam_b;
    b_v->priority = 1;
    ecs_modified(w, b, View);
    ecs_set(w, a, EnvironmentLight, {
        .sky_color = {0, 0, 0},
        .ground_color = {0, 0, 0},
        .intensity = 4
    });
    box_render_extract(w, &packet);
    box_view_t *first = box_vec_at(&packet.views, 0);
    box_view_t *second = box_vec_at(&packet.views, 1);
    test_flt(first->env_sky[0], 0);
    test_flt(first->env_intensity, 4);
    test_flt(second->env_sky[0], 1);
    test_flt(second->env_intensity, 1);
    test_assert(first->env_generation != second->env_generation);
}

void View_environment_falls_back_to_the_singleton(void)
{
    // views/view
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {255, 128, 0},
        .ground_color = {0, 0, 0},
        .intensity = 2
    });
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    View *e_v = ecs_get_mut(w, e, View);
    e_v->camera = cam;
    e_v->priority = 0;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_flt(v->env_sky[0], 1);
    test_flt(v->env_intensity, 2);
}

void View_equal_environments_share_a_generation(void)
{
    // views/view
    ecs_entity_t cam_a = ecs_new(w);
    ecs_set(w, cam_a, Position, {0, 0, -5});
    ecs_set(w, cam_a, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(w);
    ecs_add(w, a, View);
    View *a_v = ecs_get_mut(w, a, View);
    a_v->camera = cam_a;
    a_v->priority = 0;
    ecs_modified(w, a, View);
    ecs_entity_t cam_b = ecs_new(w);
    ecs_set(w, cam_b, Position, {0, 0, -8});
    ecs_set(w, cam_b, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_add(w, b, View);
    View *b_v = ecs_get_mut(w, b, View);
    b_v->camera = cam_b;
    b_v->priority = 1;
    ecs_modified(w, b, View);
    box_render_extract(w, &packet);
    box_view_t *first = box_vec_at(&packet.views, 0);
    box_view_t *second = box_vec_at(&packet.views, 1);
    test_assert(first->env_generation != 0);
    test_int(first->env_generation, second->env_generation);
}

void View_cluster_settings_of_a_view_override_the_singleton(void)
{
    // views/view
    ecs_singleton_set(w, ClusterSettings, {.x = 4, .y = 3, .z = 2, .max_lights = 8});
    ecs_entity_t cam_a = ecs_new(w);
    ecs_set(w, cam_a, Position, {0, 0, -5});
    ecs_set(w, cam_a, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(w);
    ecs_add(w, a, View);
    View *a_v = ecs_get_mut(w, a, View);
    a_v->camera = cam_a;
    a_v->priority = 0;
    ecs_modified(w, a, View);
    ecs_entity_t cam_b = ecs_new(w);
    ecs_set(w, cam_b, Position, {0, 0, -8});
    ecs_set(w, cam_b, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_add(w, b, View);
    View *b_v = ecs_get_mut(w, b, View);
    b_v->camera = cam_b;
    b_v->priority = 1;
    ecs_modified(w, b, View);
    ecs_set(w, a, ClusterSettings, {.x = 32, .y = 18, .z = 48, .max_lights = 16});
    box_render_extract(w, &packet);
    box_view_t *first = box_vec_at(&packet.views, 0);
    box_view_t *second = box_vec_at(&packet.views, 1);
    test_int(first->cluster.x, 32);
    test_int(first->cluster.max_lights, 16);
    test_int(second->cluster.x, 4);
    test_int(second->cluster.max_lights, 8);
}

void View_cluster_settings_fall_back_to_the_singleton(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    View *e_v = ecs_get_mut(w, e, View);
    e_v->camera = cam;
    e_v->priority = 0;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->cluster.x, BOX_CLUSTER_X);
    test_int(v->cluster.y, BOX_CLUSTER_Y);
    test_int(v->cluster.z, BOX_CLUSTER_Z);
    test_int(v->cluster.max_lights, BOX_CLUSTER_MAX_LIGHTS);
}

void View_material_layer_list_reaches_the_view(void)
{
    // views/view
    ecs_entity_t cam_a = ecs_new(w);
    ecs_set(w, cam_a, Position, {0, 0, -5});
    ecs_set(w, cam_a, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(w);
    ecs_add(w, a, View);
    View *a_v = ecs_get_mut(w, a, View);
    a_v->camera = cam_a;
    a_v->priority = 0;
    ecs_modified(w, a, View);
    ecs_entity_t cam_b = ecs_new(w);
    ecs_set(w, cam_b, Position, {0, 0, -8});
    ecs_set(w, cam_b, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_add(w, b, View);
    View *b_v = ecs_get_mut(w, b, View);
    b_v->camera = cam_b;
    b_v->priority = 1;
    ecs_modified(w, b, View);
    ecs_entity_t layer = ecs_new(w);
    ecs_set(w, layer, MaterialLayer, {0});
    a_v = ecs_get_mut(w, a, View);
    ecs_vec_append_t(NULL, &a_v->material_layers, ViewMaterialLayer)[0] =
        (ViewMaterialLayer){ .layer = layer, .enabled = false };
    ecs_modified(w, a, View);
    b_v = ecs_get_mut(w, b, View);
    ecs_vec_append_t(NULL, &b_v->material_layers, ViewMaterialLayer)[0] =
        (ViewMaterialLayer){ .layer = layer, .enabled = true };
    ecs_modified(w, b, View);
    box_render_extract(w, &packet);
    test_int(((box_view_t*)box_vec_at(&packet.views, 0))->material_layer_count, 0);
    test_int(((box_view_t*)box_vec_at(&packet.views, 1))->material_layer_count, 1);
}

void View_clear_color_of_a_view_is_linear(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    View *e_v = ecs_get_mut(w, e, View);
    e_v->camera = cam;
    e_v->priority = 0;
    ecs_modified(w, e, View);
    e_v = ecs_get_mut(w, e, View);
    e_v->clear.r = 255;
    e_v->clear.g = 0;
    e_v->clear.b = 128;
    e_v->clear_alpha = 0.5f;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *out = box_vec_at(&packet.views, 0);
    test_flt(out->clear[0], 1);
    test_flt(out->clear[1], 0);
    test_assert(fabsf(out->clear[2] - box_srgb_to_linear(128)) < 1e-6f);
    test_flt(out->clear[3], 0.5f);
}

void View_lights_are_culled_per_view(void)
{
    // views/view
    ecs_entity_t right = ecs_new(w);
    ecs_set(w, right, Position, {100, 0, 0});
    ecs_set(w, right, PointLight, {.intensity = 1, .range = 2});
    ecs_entity_t left = ecs_new(w);
    ecs_set(w, left, Position, {-100, 0, 0});
    ecs_set(w, left, PointLight, {.intensity = 1, .range = 2});

    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {100, 0, -5});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {-100, 0, -5});
    ecs_set(w, b, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(w);
    ecs_add(w, view_a, View);
    View *view_a_v = ecs_get_mut(w, view_a, View);
    view_a_v->camera = a;
    view_a_v->priority = 0;
    ecs_modified(w, view_a, View);
    ecs_entity_t view_b = ecs_new(w);
    ecs_add(w, view_b, View);
    View *view_b_v = ecs_get_mut(w, view_b, View);
    view_b_v->camera = b;
    view_b_v->priority = 1;
    ecs_modified(w, view_b, View);
    box_render_extract(w, &packet);
    box_view_t *first = box_vec_at(&packet.views, 0);
    box_view_t *second = box_vec_at(&packet.views, 1);
    test_int(first->light_count, 1);
    test_int(second->light_count, 1);
    test_int(first->light_first, 0);
    test_int(second->light_first, 1);
    test_int(packet.lights.count, 2);
    box_light_t *right_light = box_vec_at(&packet.lights, first->light_first);
    box_light_t *left_light = box_vec_at(&packet.lights, second->light_first);
    test_flt(right_light->position[0], 100);
    test_flt(left_light->position[0], -100);
}

void View_sorted_draws_are_sorted_per_view(void)
{
    // views/view
    ecs_entity_t near_quad = ecs_new(w);
    ecs_set(w, near_quad, Position, {0, 0, 0});
    ecs_set(w, near_quad, Box, {1, 1, 1});
    ecs_set(w, near_quad, Opacity, {0.5f});
    ecs_entity_t far_quad = ecs_new(w);
    ecs_set(w, far_quad, Position, {0, 0, 10});
    ecs_set(w, far_quad, Box, {1, 1, 1});
    ecs_set(w, far_quad, Opacity, {0.5f});

    ecs_entity_t front = ecs_new(w);
    ecs_set(w, front, Position, {0, 0, -20});
    ecs_set(w, front, Camera, {.fov = 60});
    ecs_entity_t behind = ecs_new(w);
    ecs_set(w, behind, Position, {0, 0, 30});
    ecs_set(w, behind, Rotation, {0, 3.14159265f, 0});
    ecs_set(w, behind, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(w);
    ecs_add(w, view_a, View);
    View *view_a_v = ecs_get_mut(w, view_a, View);
    view_a_v->camera = front;
    view_a_v->priority = 0;
    ecs_modified(w, view_a, View);
    ecs_entity_t view_b = ecs_new(w);
    ecs_add(w, view_b, View);
    View *view_b_v = ecs_get_mut(w, view_b, View);
    view_b_v->camera = behind;
    view_b_v->priority = 1;
    ecs_modified(w, view_b, View);
    box_render_extract(w, &packet);
    box_view_t *first = box_vec_at(&packet.views, 0);
    box_view_t *second = box_vec_at(&packet.views, 1);
    test_int(first->transparent_count, 2);
    test_int(second->transparent_count, 2);
    test_int(second->transparent_first, 2);
    box_transparent_t *a = box_vec_at(&packet.transparent, first->transparent_first);
    box_transparent_t *b = box_vec_at(&packet.transparent, second->transparent_first);
    test_flt(a->data.transform[11], 10);
    test_flt(b->data.transform[11], 0);
}

void View_depth_precision_at_100k(void)
{
    // views/view
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    View *e_v = ecs_get_mut(w, e, View);
    e_v->camera = cam;
    e_v->priority = 0;
    ecs_modified(w, e, View);
    box_render_extract(w, &packet);
    box_view_t *v = box_vec_at(&packet.views, 0);

    const float *m = v->proj;
    float near_z = m[10] * 100000.0f + m[14];
    float near_w = m[11] * 100000.0f + m[15];
    float far_z = m[10] * 100001.0f + m[14];
    float far_w = m[11] * 100001.0f + m[15];
    float near_depth = near_z / near_w;
    float far_depth = far_z / far_w;
    test_assert(near_depth > 0.0f);
    test_assert(far_depth > 0.0f);
    test_assert(near_depth > far_depth);
    test_assert(near_depth != far_depth);
}
