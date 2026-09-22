/**
 * @file Frustum.c
 * @brief Tests for the frustum planes, the instance bounds and the culling settings.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Frustum_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    packet.clear[3] = 1.0f;
}

void Frustum_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Frustum_planes_of_a_known_projection(void)
{
    // culling/frustum_culling
    mat4 view = mat4_look_at(vec3_new(0, 0, 0), vec3_new(0, 0, 1), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f);
    box_frustum_t f;
    box_frustum_build(view.v, proj.v, &f);

    float inside[3] = {0, 0, 5};
    test_bool(box_frustum_sphere(&f, inside, 0), true);

    float left[3] = {-6, 0, 5};
    float right[3] = {6, 0, 5};
    float bottom[3] = {0, -6, 5};
    float top[3] = {0, 6, 5};
    float near[3] = {0, 0, 0.05f};
    test_bool(box_frustum_sphere(&f, left, 0), false);
    test_bool(box_frustum_sphere(&f, right, 0), false);
    test_bool(box_frustum_sphere(&f, bottom, 0), false);
    test_bool(box_frustum_sphere(&f, top, 0), false);
    test_bool(box_frustum_sphere(&f, near, 0), false);

    const float *plane = f.planes[BOX_FRUSTUM_LEFT];
    test_assert(plane[0] * left[0] + plane[1] * left[1] + plane[2] * left[2] + plane[3] < 0);
    plane = f.planes[BOX_FRUSTUM_RIGHT];
    test_assert(plane[0] * right[0] + plane[1] * right[1] + plane[2] * right[2] + plane[3] < 0);
    plane = f.planes[BOX_FRUSTUM_BOTTOM];
    test_assert(plane[0] * bottom[0] + plane[1] * bottom[1] + plane[2] * bottom[2] + plane[3] < 0);
    plane = f.planes[BOX_FRUSTUM_TOP];
    test_assert(plane[0] * top[0] + plane[1] * top[1] + plane[2] * top[2] + plane[3] < 0);
    plane = f.planes[BOX_FRUSTUM_NEAR];
    test_assert(plane[0] * near[0] + plane[1] * near[1] + plane[2] * near[2] + plane[3] < 0);
}

void Frustum_infinite_far_keeps_a_distant_point(void)
{
    // culling/frustum_culling
    mat4 view = mat4_look_at(vec3_new(0, 0, 0), vec3_new(0, 0, 1), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f);
    box_frustum_t f;
    box_frustum_build(view.v, proj.v, &f);

    float far_point[3] = {0, 0, 1e6f};
    test_bool(box_frustum_sphere(&f, far_point, 0), true);
    test_flt(f.planes[BOX_FRUSTUM_FAR][0], 0);
    test_flt(f.planes[BOX_FRUSTUM_FAR][1], 0);
    test_flt(f.planes[BOX_FRUSTUM_FAR][2], 0);
    test_assert(f.planes[BOX_FRUSTUM_FAR][3] > 0);

    box_frustum_close(&f, view.v, 100.0f);
    test_bool(box_frustum_sphere(&f, far_point, 0), false);
    float near_far[3] = {0, 0, 99};
    test_bool(box_frustum_sphere(&f, near_far, 0), true);
}

void Frustum_sphere_at_the_boundary(void)
{
    // culling/frustum_culling
    mat4 view = mat4_look_at(vec3_new(0, 0, 0), vec3_new(0, 0, 1), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f);
    box_frustum_t f;
    box_frustum_build(view.v, proj.v, &f);

    float center[3] = {-6, 0, 5};
    float distance = 1.0f / sqrtf(2.0f);
    test_bool(box_frustum_sphere(&f, center, distance * 0.99f), false);
    test_bool(box_frustum_sphere(&f, center, distance * 1.01f), true);

    float just_in[3] = {-4.99f, 0, 5};
    float just_out[3] = {-5.01f, 0, 5};
    test_bool(box_frustum_sphere(&f, just_in, 0), true);
    test_bool(box_frustum_sphere(&f, just_out, 0), false);
}

void Frustum_aabb_at_the_boundary(void)
{
    // culling/frustum_culling
    mat4 view = mat4_look_at(vec3_new(0, 0, 0), vec3_new(0, 0, 1), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f * 0.5f, 1.0f, 0.1f);
    box_frustum_t f;
    box_frustum_build(view.v, proj.v, &f);

    float min_in[3] = {-6.5f, -0.5f, 4.5f};
    float max_in[3] = {-5.5f, 0.5f, 5.5f};
    test_bool(box_frustum_aabb(&f, min_in, max_in), true);

    float min_out[3] = {-6.6f, -0.4f, 4.6f};
    float max_out[3] = {-5.6f, 0.4f, 5.4f};
    test_bool(box_frustum_aabb(&f, min_out, max_out), false);

    float min_near[3] = {-0.5f, -0.5f, -1.0f};
    float max_near[3] = {0.5f, 0.5f, 0.05f};
    test_bool(box_frustum_aabb(&f, min_near, max_near), false);
}

void Frustum_static_instance_carries_its_bounds(void)
{
    // culling/frustum_culling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {2, 2, 2});
    box_render_extract(w, &packet);

    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.bounds[0], 1);
    test_flt(update->data.bounds[1], 2);
    test_flt(update->data.bounds[2], 3);
    test_assert(fabsf(update->data.bounds[3] - sqrtf(3.0f)) < 1e-4f);
}

void Frustum_dynamic_instance_carries_its_bounds(void)
{
    // culling/frustum_culling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 4, 0});
    ecs_set(w, e, Box, {2, 2, 2});
    ecs_add_id(w, e, DynamicTransform);
    box_render_extract(w, &packet);

    test_int(packet.dynamic_instances.count, 1);
    box_instance_t *data = box_vec_at(&packet.dynamic_instances, 0);
    test_flt(data->bounds[0], 0);
    test_flt(data->bounds[1], 4);
    test_flt(data->bounds[2], 0);
    test_assert(fabsf(data->bounds[3] - sqrtf(3.0f)) < 1e-4f);
}

void Frustum_bounds_refresh_when_the_entity_changes(void)
{
    // culling/frustum_culling
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 0, 0});
    ecs_set(w, e, Box, {2, 2, 2});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);

    box_packet_reset(&packet);
    ecs_set(w, e, Position, {7, 0, 0});
    ecs_set(w, e, Scale, {2, 2, 2});
    box_render_extract(w, &packet);

    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_flt(update->data.bounds[0], 7);
    test_assert(fabsf(update->data.bounds[3] - 2.0f * sqrtf(3.0f)) < 1e-4f);
}

void Frustum_settings_fall_back_to_the_singleton(void)
{
    // culling/frustum_culling
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    ecs_singleton_set(w, CullSettings, {.enabled = false, .gpu_threshold = 64});
    box_render_extract(w, &packet);

    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->cull_enabled, 0);
    test_uint(v->cull_threshold, 64);
}

void Frustum_settings_of_a_view_override_the_singleton(void)
{
    // culling/frustum_culling
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t e = ecs_new(w);
    ecs_add(w, e, View);
    ecs_get_mut(w, e, View)->camera = cam;
    ecs_modified(w, e, View);
    ecs_singleton_set(w, CullSettings, {.enabled = false, .gpu_threshold = 64});
    ecs_set(w, e, CullSettings, {.enabled = true, .gpu_threshold = 512});
    box_render_extract(w, &packet);

    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->cull_enabled, 1);
    test_uint(v->cull_threshold, 512);
}

void Frustum_view_without_settings_culls_by_default(void)
{
    // culling/frustum_culling
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    box_render_extract(w, &packet);

    test_int(packet.views.count, 1);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->cull_enabled, 1);
    test_uint(v->cull_threshold, BOX_CULL_GPU_THRESHOLD);
}

void Frustum_sorted_draws_behind_the_camera_are_culled(void)
{
    // culling/frustum_culling
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t front = ecs_new(w);
    ecs_set(w, front, Position, {0, 0, 0});
    ecs_set(w, front, Box, {1, 1, 1});
    ecs_set(w, front, Opacity, {0.5f});

    ecs_entity_t back = ecs_new(w);
    ecs_set(w, back, Position, {0, 0, -40});
    ecs_set(w, back, Box, {1, 1, 1});
    ecs_set(w, back, Opacity, {0.5f});

    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_flt(entry->data.transform[11], 0);
}

void Frustum_disabled_culling_keeps_every_sorted_draw(void)
{
    // culling/frustum_culling
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    ecs_singleton_set(w, CullSettings, {.enabled = false});

    ecs_entity_t front = ecs_new(w);
    ecs_set(w, front, Position, {0, 0, 0});
    ecs_set(w, front, Box, {1, 1, 1});
    ecs_set(w, front, Opacity, {0.5f});

    ecs_entity_t back = ecs_new(w);
    ecs_set(w, back, Position, {0, 0, -40});
    ecs_set(w, back, Box, {1, 1, 1});
    ecs_set(w, back, Opacity, {0.5f});

    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 2);
}
