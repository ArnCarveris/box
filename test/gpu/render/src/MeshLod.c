/**
 * @file MeshLod.c
 * @brief Tests for the levels of detail the cull pass of each view picks per instance
 * (LodSettings): what the draws of a view and of its cascades drew, read back from the indirect
 * draw arguments.
 *
 * The camera looks down -z from the origin into a 64x64 image. The views with levels have three:
 * level 1 from 10 units and level 2 from 30 units, measured from the camera to the nearest point
 * of the bounding sphere of an instance.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "mesh_cache.h"

/** Levels of detail of the views with levels */
static const LodSettings lod_three = {.levels = 3, .distances = {0, 10, 30}};

/** Create a headless world that renders with the GPU on the main thread, with a surface.
 *
 * @return The world
 */
static ecs_world_t* world_new(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    return world;
}

/** Create the camera at the origin looking down -z.
 *
 * @param world The world
 * @return The camera
 */
static ecs_entity_t camera_new(
    ecs_world_t *world)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {0, 0, 0});
    ecs_set(world, result, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});

    return result;
}

/** Create a view of a camera.
 *
 * @param world The world
 * @param camera The camera
 * @param lod Levels of detail of the view
 * @param culling Whether the view culls
 * @param occlusion Whether it culls by occlusion too
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t camera,
    LodSettings lod,
    bool culling,
    bool occlusion)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, View, {.camera = camera,
        .culling = {.enabled = culling, .occlusion = occlusion},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lod = lod});

    return result;
}

/** Create a smooth sphere.
 *
 * @param world The world
 * @param z Its z position, negative in front of the camera
 * @param radius Its radius
 * @return The sphere
 */
static ecs_entity_t sphere_new(
    ecs_world_t *world,
    float z,
    float radius)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {0, 0, z});
    ecs_set(world, result, Sphere, {.segments = 32, .smooth = true, .radius = radius});
    ecs_set(world, result, Rgb, {255, 255, 255});

    return result;
}

/** Get the index count of a level of the mesh of a shape.
 *
 * @param world The world
 * @param e The shape
 * @param level The level
 * @return The indices of the mesh of the level
 */
static int64_t level_indices(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t level)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL && sm->mesh != 0);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, box_mesh_cache_lod(cache, sm->mesh, level));
    test_assert(mesh != NULL);

    return (int64_t)mesh->index_count;
}

/** Run frames and wait until the backend rendered them.
 *
 * @param world The world
 * @param count Frames
 */
static void frames(
    ecs_world_t *world,
    int32_t count)
{
    for (int32_t i = 0; i < count; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }
}

void MeshLod_far_sphere_level_2(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, lod_three, true, false);
    ecs_entity_t sphere = sphere_new(world, -60, 1);
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 1);
    test_int(stats.levels[0], 0);
    test_int(stats.levels[1], 0);
    test_int(stats.levels[2], 1);
    test_assert(level_indices(world, sphere, 2) < level_indices(world, sphere, 0));
    test_int(stats.indices, level_indices(world, sphere, 2));

    ecs_fini(world);
}

void MeshLod_levels_by_distance(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, lod_three, true, false);
    ecs_entity_t near = sphere_new(world, -5, 1);
    sphere_new(world, -20, 1);
    sphere_new(world, -60, 1);
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 3);
    test_int(stats.levels[0], 1);
    test_int(stats.levels[1], 1);
    test_int(stats.levels[2], 1);
    test_int(stats.indices, level_indices(world, near, 0) + level_indices(world, near, 1) +
        level_indices(world, near, 2));

    ecs_fini(world);
}

void MeshLod_large_near_sphere_level_0(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, lod_three, true, false);
    ecs_entity_t sphere = sphere_new(world, -40, 30);
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 1);
    test_int(stats.levels[0], 1);
    test_int(stats.indices, level_indices(world, sphere, 0));

    ecs_fini(world);
}

void MeshLod_second_view_without_lod(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t reduced = view_new(world, camera, lod_three, true, false);
    ecs_entity_t full = view_new(world, camera, (LodSettings){0}, true, false);
    ecs_entity_t sphere = sphere_new(world, -60, 1);
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, reduced, &stats), true);
    test_int(stats.visible, 1);
    test_int(stats.levels[2], 1);
    test_int(stats.indices, level_indices(world, sphere, 2));

    test_bool(box_render_gpu_cull_stats(world, full, &stats), true);
    test_int(stats.visible, 1);
    test_int(stats.levels[0], 1);
    test_int(stats.levels[2], 0);
    test_int(stats.indices, level_indices(world, sphere, 0));

    ecs_fini(world);
}

void MeshLod_flat_cylinder_and_box_not_reduced(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, lod_three, true, false);
    ecs_entity_t cylinder = ecs_new(world);
    ecs_set(world, cylinder, Position, {-2, 0, -60});
    ecs_set(world, cylinder, Cylinder, {.segments = 32, .smooth = false, .length = 2,
        .radius = 1});
    ecs_set(world, cylinder, Rgb, {255, 255, 255});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {2, 0, -60});
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {255, 255, 255});
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 2);
    test_int(stats.levels[0], 2);
    test_int(stats.levels[1], 0);
    test_int(stats.levels[2], 0);
    test_int(stats.indices, level_indices(world, cylinder, 0) + level_indices(world, box, 0));

    ecs_fini(world);
}

void MeshLod_shadow_same_level(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f,
            .cascades = 1, .resolution = 1024, .max_distance = 100, .split_lambda = 0.5f,
            .filter_size = 3},
        .lod = lod_three});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t sphere = sphere_new(world, -60, 1);
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_frustum(world, view, -1, &stats), true);
    test_int(stats.visible, 1);
    test_int(stats.levels[2], 1);
    test_int(stats.indices, level_indices(world, sphere, 2));

    test_bool(box_render_gpu_cull_frustum(world, view, 0, &stats), true);
    test_int(stats.visible, 1);
    test_int(stats.levels[0], 0);
    test_int(stats.levels[2], 1);
    test_int(stats.indices, level_indices(world, sphere, 2));

    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.casters_visible, 1);
    test_int(stats.casters_levels[2], 1);
    test_int(stats.casters_indices, level_indices(world, sphere, 2));

    ecs_fini(world);
}

/** Get the index count of a level of the mesh a shape resolved against its surface.
 *
 * @param world The world
 * @param e The shape
 * @param level The level
 * @return The indices of the resolved mesh of the level
 */
static int64_t resolved_level_indices(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t level)
{
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL && sm->render_mesh != 0);
    const box_mesh_t *mesh = box_mesh_cache_get(cache,
        box_mesh_cache_lod(cache, sm->render_mesh, level));
    test_assert(mesh != NULL);

    return (int64_t)mesh->index_count;
}

void MeshLod_far_lathe_and_sweep_level_2(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, lod_three, true, false);
    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "vase {\n"
        "  Path2D: {start: {0, 0}, segments: [{kind: Line, end: {0.4, 0}},\n"
        "    {kind: CatmullRom, end: {0.75, 0.6}}, {kind: CatmullRom, end: {0.5, 1.8}}]}\n"
        "}\n"
        "round {\n"
        "  Path2D: {start: {0.3, 0}, closed: true, segments: [\n"
        "    {kind: CatmullRom, end: {0, 0.3}}, {kind: CatmullRom, end: {-0.3, 0}},\n"
        "    {kind: CatmullRom, end: {0, -0.3}}, {kind: CatmullRom, end: {0.3, 0}}]}\n"
        "}\n"
        "round_section { Profile2D: {outer: round} }\n"
        "bend {\n"
        "  Path3D: {start: {-1, 0, 0}, segments: [{kind: CatmullRom, end: {0, 0.5, 0}},\n"
        "    {kind: CatmullRom, end: {1, 0, 0}}]}\n"
        "}\n"
        "lathe {\n"
        "  Position: {-2, 0, -60}\n"
        "  Lathe: {profile: vase, cap_start: true, cap_end: true, segments: 32, rings: 24}\n"
        "  Rgb: {255, 255, 255}\n"
        "}\n"
        "sweep {\n"
        "  Position: {2, 0, -60}\n"
        "  Sweep: {path: bend, profile: round_section, segments: 16, rings: 32}\n"
        "  Rgb: {255, 255, 255}\n"
        "}\n" }) != 0);
    ecs_entity_t lathe = ecs_lookup(world, "lathe");
    ecs_entity_t sweep = ecs_lookup(world, "sweep");
    frames(world, 2);

    test_assert(resolved_level_indices(world, lathe, 2) <
        resolved_level_indices(world, lathe, 0));
    test_assert(resolved_level_indices(world, sweep, 2) <
        resolved_level_indices(world, sweep, 0));
    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 2);
    test_int(stats.levels[0], 0);
    test_int(stats.levels[2], 2);
    test_int(stats.indices, resolved_level_indices(world, lathe, 2) +
        resolved_level_indices(world, sweep, 2));

    ecs_fini(world);
}

void MeshLod_culling_off(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, lod_three, false, false);
    ecs_entity_t near = sphere_new(world, -5, 1);
    sphere_new(world, -60, 1);
    sphere_new(world, 60, 1);
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 3);
    test_int(stats.levels[0], 1);
    test_int(stats.levels[1], 0);
    test_int(stats.levels[2], 2);
    test_int(stats.indices, level_indices(world, near, 0) + 2 * level_indices(world, near, 2));

    ecs_fini(world);
}

void MeshLod_occlusion(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, lod_three, true, true);
    ecs_entity_t near = sphere_new(world, -5, 1);
    ecs_set(world, near, Position, {3, 0, -5});
    sphere_new(world, -60, 1);
    frames(world, 3);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 2);
    test_int(stats.levels[0], 1);
    test_int(stats.levels[2], 1);
    test_int(stats.indices, level_indices(world, near, 0) + level_indices(world, near, 2));

    ecs_fini(world);
}

void MeshLod_one_level_unchanged(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t camera = camera_new(world);
    ecs_entity_t view = view_new(world, camera, (LodSettings){.levels = 1,
        .distances = {0, 1, 2, 3}}, true, false);
    ecs_entity_t sphere = sphere_new(world, -60, 1);
    frames(world, 2);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.visible, 1);
    test_int(stats.levels[0], 1);
    test_int(stats.indices, level_indices(world, sphere, 0));

    ecs_fini(world);
}
