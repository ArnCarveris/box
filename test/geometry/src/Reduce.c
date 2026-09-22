/**
 * @file Reduce.c
 * @brief Tests for the tessellation a parametric shape is drawn with per frame.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/geometry/geometry_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t camera;
static ecs_entity_t view;

void Reduce_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, 0});
    ecs_set(w, camera, Camera, {.fov = 60});
    view = ecs_new(w);
    ecs_set(w, view, View, {.camera = camera, .enabled = true});
    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
}

void Reduce_teardown(void)
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

void Reduce_a_distant_sphere_drops_to_eight_segments(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 8, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 60});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    test_assert(mesh != NULL);
    test_uint(mesh->handle, expected);
}

void Reduce_a_near_sphere_keeps_the_authored_mesh(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 6});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_instances_at_one_level_share_a_mesh(void)
{
    // lod/primitive_reduction
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    vec3 min, max;
    uint32_t authored;

    ecs_set(w, a, Position, {0, 0, 60});
    ecs_set(w, a, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    ecs_set(w, b, Position, {3, 0, 60});
    ecs_set(w, b, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    authored = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);

    uint32_t handle_a = ecs_get(w, a, RenderMesh)->handle;
    uint32_t handle_b = ecs_get(w, b, RenderMesh)->handle;
    test_uint(handle_a, handle_b);
    test_assert(handle_a != authored);
}

void Reduce_the_authored_count_caps_the_level(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 6, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 6});
    ecs_set(w, e, Sphere, {.segments = 6, .smooth = true, .radius = 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_shape_at_its_floor_never_reduces(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 4, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 400});
    ecs_set(w, e, Sphere, {.segments = 4, .smooth = true, .radius = 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_settings_that_are_off_keep_the_authored_mesh(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_singleton_set(w, LodSettings, {.enabled = false});
    ecs_set(w, e, Position, {0, 0, 60});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_world_without_settings_keeps_the_authored_mesh(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_singleton_remove(w, LodSettings);
    ecs_set(w, e, Position, {0, 0, 60});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_world_without_a_view_keeps_the_authored_mesh(void)
{
    ecs_delete(w, view);
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    vec3 min, max;
    ecs_set(w, e, Position, {0, 0, 60});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    uint32_t expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_view_without_settings_holds_the_finest_level(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    ecs_entity_t reducing = ecs_new(w);
    ecs_entity_t plain = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_singleton_remove(w, LodSettings);
    ecs_set(w, reducing, View, {.camera = camera, .enabled = true});
    ecs_set(w, reducing, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
    ecs_set(w, plain, View, {.camera = camera, .enabled = true, .priority = 1});
    ecs_set(w, e, Position, {0, 0, 60});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_receding_camera_coarsens_the_level(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t authored_key = { .kind = BoxShapeSphere, .i0 = 32, .smooth = true };
    box_mesh_key_t reduced_key = { .kind = BoxShapeSphere, .i0 = 8, .smooth = true };
    vec3 min, max;
    uint32_t authored;
    uint32_t reduced;

    ecs_set(w, e, Position, {0, 0, 6});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    authored = box_mesh_cache_get(w, &authored_key, &min, &max);
    reduced = box_mesh_cache_get(w, &reduced_key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, authored);

    ecs_set(w, camera, Position, {0, 0, -54});
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, reduced);
}

void Reduce_the_band_holds_a_level_over_a_frame(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    uint32_t settled;

    ecs_set(w, e, Position, {0, 0, 22});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    box_render_extract(w, &packet);
    settled = ecs_get(w, e, RenderMesh)->handle;

    ecs_set(w, camera, Position, {0, 0, 0.4f});
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, settled);

    ecs_set(w, camera, Position, {0, 0, -0.4f});
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, settled);
}

void Reduce_a_level_change_moves_the_instance_to_its_batch(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);

    ecs_set(w, e, Position, {0, 0, 60});
    ecs_set(w, e, Sphere, {.segments = 32, .smooth = true, .radius = 1});
    box_render_extract(w, &packet);

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    test_assert(inst != NULL);
    test_uint(inst->mesh, mesh->handle);
    test_assert(!inst->dirty);

    uint32_t i;
    bool updated = false;
    for (i = 0; i < packet.static_updates.count; i ++) {
        box_static_update_t *update = box_vec_at(&packet.static_updates, i);

        updated = updated || update->mesh == mesh->handle;
    }

    test_assert(updated);
}

void Reduce_a_cylinder_follows_the_metric(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 480,
        .radius = 1, .threshold = 2, .hysteresis = 0.25f
    };
    box_mesh_key_t key = { .kind = BoxShapeCylinder, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 40});
    ecs_set(w, e, Cylinder, {.segments = 32, .smooth = true, .length = 2, .radius = 1});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    vec3 size = vec3_mul(vec3_sub(mesh->bounds_max, mesh->bounds_min), mesh->scale);
    float bound = vec3_length(vec3_scale(size, 0.5f));

    desc.bounds.min = vec3_new(-bound, -bound, 40.0f - bound);
    desc.bounds.max = vec3_new(bound, bound, 40.0f + bound);
    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    key.i0 = ladder.segments[box_lod_reduce(&desc)];
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_assert(key.i0 < 32);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_cone_follows_the_metric(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 480,
        .radius = 1, .threshold = 2, .hysteresis = 0.25f
    };
    box_mesh_key_t key = { .kind = BoxShapeCone, .smooth = true };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 40});
    ecs_set(w, e, Cone, {.segments = 32, .smooth = true, .length = 2, .radius = 1});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    vec3 size = vec3_mul(vec3_sub(mesh->bounds_max, mesh->bounds_min), mesh->scale);
    float bound = vec3_length(vec3_scale(size, 0.5f));

    desc.bounds.min = vec3_new(-bound, -bound, 40.0f - bound);
    desc.bounds.max = vec3_new(bound, bound, 40.0f + bound);
    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    key.i0 = ladder.segments[box_lod_reduce(&desc)];
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_assert(key.i0 < 32);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_torus_reduces_its_rings_with_its_segments(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 480,
        .radius = 1, .threshold = 2, .hysteresis = 0.25f
    };
    box_mesh_key_t key = { .kind = BoxShapeTorus, .smooth = true, .f0 = 0.25f };
    vec3 min, max;
    uint32_t expected;
    int32_t level;

    ecs_set(w, e, Position, {0, 0, 40});
    ecs_set(w, e, Torus, {.segments = 32, .rings = 16, .smooth = true,
        .radius = 1, .tube = 0.25f});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    vec3 size = vec3_mul(vec3_sub(mesh->bounds_max, mesh->bounds_min), mesh->scale);
    float bound = vec3_length(vec3_scale(size, 0.5f));

    desc.bounds.min = vec3_new(-bound, -bound, 40.0f - bound);
    desc.bounds.max = vec3_new(bound, bound, 40.0f + bound);
    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    level = box_lod_reduce(&desc);
    key.i0 = ladder.segments[level];
    key.i1 = (16 >> level) > BOX_LOD_MIN_SEGMENTS ? (16 >> level) : BOX_LOD_MIN_SEGMENTS;
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_assert(level > 0);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_capsule_follows_the_metric(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 480,
        .radius = 1, .threshold = 2, .hysteresis = 0.25f
    };
    box_mesh_key_t key = { .kind = BoxShapeCapsule, .smooth = true, .f0 = 2 };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 40});
    ecs_set(w, e, Capsule, {.segments = 32, .smooth = true, .length = 2, .radius = 1});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    vec3 size = vec3_mul(vec3_sub(mesh->bounds_max, mesh->bounds_min), mesh->scale);
    float bound = vec3_length(vec3_scale(size, 0.5f));

    desc.bounds.min = vec3_new(-bound, -bound, 40.0f - bound);
    desc.bounds.max = vec3_new(bound, bound, 40.0f + bound);
    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    key.i0 = ladder.segments[box_lod_reduce(&desc)];
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_assert(key.i0 < 32);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_an_open_tube_is_measured_as_the_circle_it_sweeps(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 480,
        .radius = 1, .span = 0.5f, .threshold = 2, .hysteresis = 0.25f
    };
    box_mesh_key_t key = {
        .kind = BoxShapeTube, .smooth = true, .f0 = 0.2f, .f1 = 3.14159265f
    };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 40});
    ecs_set(w, e, Tube, {.segments = 32, .smooth = true, .length = 2, .radius = 1,
        .thickness = 0.2f, .arc = 3.14159265f});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    vec3 size = vec3_mul(vec3_sub(mesh->bounds_max, mesh->bounds_min), mesh->scale);
    float bound = vec3_length(vec3_scale(size, 0.5f));

    desc.bounds.min = vec3_new(-bound, -bound, 40.0f - bound);
    desc.bounds.max = vec3_new(bound, bound, 40.0f + bound);
    box_lod_ladder(32, BOX_LOD_MIN_SEGMENTS, &ladder);
    key.i0 = ladder.segments[box_lod_reduce(&desc)];
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_rounded_box_reduces_its_corners(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_lod_ladder_t ladder;
    box_lod_reduce_desc_t desc = {
        .ladder = &ladder, .camera = { .fov = 60 }, .viewport_px = 480,
        .radius = 0.5f, .span = 0.25f, .threshold = 2, .hysteresis = 0.25f
    };
    box_mesh_key_t key = {
        .kind = BoxShapeRoundedBox, .f0 = 2, .f1 = 2, .f2 = 2, .f3 = 0.5f
    };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 40});
    ecs_set(w, e, RoundedBox, {.x = 2, .y = 2, .z = 2, .radius = 0.5f, .segments = 8});

    const RenderMesh *mesh = ecs_get(w, e, RenderMesh);
    vec3 size = vec3_mul(vec3_sub(mesh->bounds_max, mesh->bounds_min), mesh->scale);
    float bound = vec3_length(vec3_scale(size, 0.5f));

    desc.bounds.min = vec3_new(-bound, -bound, 40.0f - bound);
    desc.bounds.max = vec3_new(bound, bound, 40.0f + bound);
    box_lod_ladder(8, 1, &ladder);
    key.i0 = ladder.segments[box_lod_reduce(&desc)];
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_assert(key.i0 < 8);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}

void Reduce_a_hemisphere_and_a_frustum_reduce_too(void)
{
    // lod/primitive_reduction
    ecs_entity_t half = ecs_new(w);
    ecs_entity_t cut = ecs_new(w);
    box_mesh_key_t half_key = { .kind = BoxShapeHemiSphere, .i0 = 32, .smooth = true };
    box_mesh_key_t cut_key = { .kind = BoxShapeFrustum, .i0 = 32, .smooth = true, .f0 = 0.5f };
    vec3 min, max;
    uint32_t half_authored;
    uint32_t cut_authored;

    ecs_set(w, half, Position, {0, 0, 60});
    ecs_set(w, half, HemiSphere, {.segments = 32, .smooth = true, .radius = 1});
    ecs_set(w, cut, Position, {4, 0, 60});
    ecs_set(w, cut, Frustum, {.segments = 32, .smooth = true, .length = 2,
        .radius_bottom = 1, .radius_top = 0.5f});
    half_authored = box_mesh_cache_get(w, &half_key, &min, &max);
    cut_authored = box_mesh_cache_get(w, &cut_key, &min, &max);
    box_render_extract(w, &packet);
    test_assert(ecs_get(w, half, RenderMesh)->handle != half_authored);
    test_assert(ecs_get(w, cut, RenderMesh)->handle != cut_authored);
}

void Reduce_a_box_never_carries_a_ladder(void)
{
    // lod/primitive_reduction
    ecs_entity_t e = ecs_new(w);
    box_mesh_key_t key = { .kind = BoxShapeBox };
    vec3 min, max;
    uint32_t expected;

    ecs_set(w, e, Position, {0, 0, 400});
    ecs_set(w, e, Box, {1, 1, 1});
    expected = box_mesh_cache_get(w, &key, &min, &max);
    box_render_extract(w, &packet);
    test_uint(ecs_get(w, e, RenderMesh)->handle, expected);
}
