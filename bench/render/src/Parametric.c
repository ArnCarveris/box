/**
 * @file Parametric.c
 * @brief Benchmarks for a scene with parametric surfaces next to plain meshes.
 *
 * The scene is about the size of the spaceship scene: about 8,800 instances, a few hundred with
 * a parametric surface of each of the seven kinds, the rest plain boxes, spheres and polygons.
 * Its profiles and paths are made in code, so the bench needs no asset files.
 *
 * The extract case runs frames against a backend that drops the packets, so it measures the
 * main thread and reports the instance memory counters; the GPU case renders headless frames
 * with shadows on the main thread and waits for the GPU after each, so it measures whole frames.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#include "mesh_cache.h"
#include "bench_memory.h"

/** Plain shapes of the scene */
#define BENCH_PARAMETRIC_PLAIN (8400)

/** Shapes of each kind of parametric surface */
#define BENCH_PARAMETRIC_PER_KIND (56)

/** Kinds of parametric surfaces */
#define BENCH_PARAMETRIC_KINDS (7)

/** Shapes of the scene */
#define BENCH_PARAMETRIC_SHAPES \
    (BENCH_PARAMETRIC_PLAIN + BENCH_PARAMETRIC_PER_KIND * BENCH_PARAMETRIC_KINDS)

/** Plain shapes per row of the hull grid */
#define BENCH_PARAMETRIC_ROW (120)

/** Variants of the profiles and paths, so a kind has a few meshes */
#define BENCH_PARAMETRIC_VARIANTS (4)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_parametric_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Create a Path2D of line segments.
 *
 * @param world The world
 * @param points The points, the first the start
 * @param count Points
 * @param closed Whether the path loops back to its start
 * @return The entity with the path
 */
static ecs_entity_t bench_parametric_path2(
    ecs_world_t *world,
    const vec2 *points,
    int32_t count,
    bool closed)
{
    Path2D path = {.closed = closed};
    int32_t i;
    glm_vec2_copy(ECS_CONST_CAST(float*, points[0]), path.start);
    for (i = 1; i < count; i ++) {
        PathSegment2D *segment = ecs_vec_append_t(NULL, &path.segments, PathSegment2D);
        *segment = (PathSegment2D){.kind = PathLine};
        glm_vec2_copy(ECS_CONST_CAST(float*, points[i]), segment->end);
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path2D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment2D);

    return e;
}

/** Create a Profile2D of a regular polygon without holes.
 *
 * @param world The world
 * @param sides Corners of the polygon
 * @param radius Distance of the corners from the center
 * @return The entity with the profile
 */
static ecs_entity_t bench_parametric_profile(
    ecs_world_t *world,
    int32_t sides,
    float radius)
{
    vec2 points[16];
    int32_t i;
    for (i = 0; i < sides; i ++) {
        float t = (float)i * 2 * GLM_PIf / (float)sides;
        points[i][0] = cosf(t) * radius;
        points[i][1] = sinf(t) * radius;
    }

    Profile2D profile = {.outer = bench_parametric_path2(world, points, sides, true)};
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Profile2D, &profile);

    return e;
}

/** Create a Path3D of one cubic segment that bends along x.
 *
 * @param world The world
 * @param bend Offset of the control points along x
 * @return The entity with the path
 */
static ecs_entity_t bench_parametric_route(
    ecs_world_t *world,
    float bend)
{
    Path3D path = {0};
    PathSegment3D segment = {.kind = PathCubicBezier,
        .control1 = {bend, 0.6f, 0}, .control2 = {-bend, 1.2f, 0}, .end = {0, 1.8f, 0}};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = segment;
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);

    return e;
}

/** Profiles and paths the parametric shapes of the scene share */
typedef struct bench_parametric_assets_t {
    ecs_entity_t profiles[BENCH_PARAMETRIC_VARIANTS]; /**< Polygon profiles */
    ecs_entity_t routes[BENCH_PARAMETRIC_VARIANTS];   /**< Paths of the sweeps */
    ecs_entity_t walls[BENCH_PARAMETRIC_VARIANTS];    /**< Outer walls of the lathes */
} bench_parametric_assets_t;

/** Give a shape the parametric surface of a kind.
 *
 * @param world The world
 * @param e The shape
 * @param assets The shared profiles and paths
 * @param kind Kind of the surface, 1 to 7
 * @param i Index of the shape in its kind, which selects its variant and its sizes
 */
static void bench_parametric_surface(
    ecs_world_t *world,
    ecs_entity_t e,
    const bench_parametric_assets_t *assets,
    int32_t kind,
    int32_t i)
{
    int32_t variant = i % BENCH_PARAMETRIC_VARIANTS;
    float size = 0.6f + 0.05f * (float)(i % 8);
    if (kind == 1) {
        ecs_set(world, e, Sweep, {.path = assets->routes[variant],
            .profile = assets->profiles[variant], .scale = {0.3f * size, 0.3f * size},
            .taper = 0.5f + 0.1f * (float)(i % 5), .cap_start = true, .cap_end = true,
            .rings = 12});
    } else if (kind == 2) {
        Loft loft = {.cap_start = true, .cap_end = true, .rings = 8};
        *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = (LoftSection){
            .profile = assets->profiles[variant], .scale = {size, size}};
        *ecs_vec_append_t(NULL, &loft.sections, LoftSection) = (LoftSection){
            .profile = assets->profiles[variant], .offset = {0, 1.5f, 0},
            .rotation = {0, 0.2f * (float)(i % 4), 0}, .scale = {0.4f * size, 0.6f * size}};
        ecs_set_ptr(world, e, Loft, &loft);
        ecs_vec_fini_t(NULL, &loft.sections, LoftSection);
    } else if (kind == 3) {
        ecs_set(world, e, Lathe, {.profile = assets->walls[variant],
            .cap_start = true, .cap_end = true, .segments = 24, .rings = 8});
    } else if (kind == 4) {
        ecs_set(world, e, EllipsoidShell, {.radius = {size, 0.5f * size, 0.8f * size},
            .longitude_end = 2 * GLM_PIf, .latitude_start = -GLM_PI_2f,
            .latitude_end = (0.25f + 0.05f * (float)(i % 5)) * GLM_PIf,
            .thickness = 0.05f, .rims = true, .segments = 24, .rings = 12});
    } else if (kind == 5) {
        ecs_set(world, e, TorusShell, {.radius = size, .tube = 0.2f * size,
            .ring_start = 0, .ring_end = (1.0f + 0.25f * (float)(i % 5)) * GLM_PIf,
            .tube_start = 0, .tube_end = 2 * GLM_PIf, .segments = 24, .rings = 12});
    } else if (kind == 6) {
        BeveledPrism prism = {.depth = size, .inset = 0.1f * size,
            .offset = {0.05f * (float)(i % 3), 0}, .bevel = 0.05f};
        vec2 corners[4] = {{-size, -0.6f}, {size, -0.6f}, {0.8f * size, 0.6f},
            {-0.8f * size, 0.6f}};
        ecs_vec_init_t(NULL, &prism.footprint, vec2, 4);
        ecs_vec_set_count_t(NULL, &prism.footprint, vec2, 4);
        ecs_os_memcpy(ecs_vec_first(&prism.footprint), corners, ECS_SIZEOF(corners));
        ecs_set_ptr(world, e, BeveledPrism, &prism);
        ecs_vec_fini_t(NULL, &prism.footprint, vec2);
    } else {
        ecs_set(world, e, ProfileExtrusion, {.profile = assets->profiles[variant],
            .depth = 0.2f + 0.1f * (float)(i % 6), .cap_start = true, .cap_end = true});
    }
}

/** Populate a world with the plain hull shapes and the parametric details of the scene.
 *
 * @param world The world
 */
static void bench_parametric_scene(
    ecs_world_t *world)
{
    bench_parametric_assets_t assets;
    int32_t i, k;
    for (i = 0; i < BENCH_PARAMETRIC_VARIANTS; i ++) {
        vec2 wall[3] = {{0.5f, -0.5f}, {0.7f + 0.1f * (float)i, 0}, {0.4f, 0.5f}};
        assets.profiles[i] = bench_parametric_profile(world, 4 + i * 2, 0.5f);
        assets.routes[i] = bench_parametric_route(world, 0.2f + 0.2f * (float)i);
        assets.walls[i] = bench_parametric_path2(world, wall, 3, false);
    }

    for (i = 0; i < BENCH_PARAMETRIC_PLAIN; i ++) {
        ecs_entity_t e = ecs_new(world);
        float x = (float)(i % BENCH_PARAMETRIC_ROW) - BENCH_PARAMETRIC_ROW / 2;
        float z = (float)(i / BENCH_PARAMETRIC_ROW) - 35;
        ecs_set(world, e, Position, {x, (float)(i % 3) * 0.5f, z});
        if (i % 5 == 0) {
            ecs_set(world, e, Sphere, {.segments = 12, .radius = 0.4f});
        } else if (i % 5 == 1) {
            ecs_set(world, e, NGon, {.sides = 3 + i % 6, .radius = 0.4f});
        } else {
            ecs_set(world, e, Box, {0.8f, 0.4f + 0.1f * (float)(i % 4), 0.8f});
        }

        ecs_set(world, e, Rgb, {(uint8_t)(120 + i % 100), 140, 160});
    }

    for (k = 0; k < BENCH_PARAMETRIC_KINDS; k ++) {
        for (i = 0; i < BENCH_PARAMETRIC_PER_KIND; i ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)(i * 2) - BENCH_PARAMETRIC_PER_KIND,
                2 + (float)(k % 2), (float)(k * 9) - 30});
            bench_parametric_surface(world, e, &assets, k + 1, i);
            ecs_set(world, e, Rgb, {200, (uint8_t)(60 + k * 25), 80});
        }
    }
}

/** Count the shapes of the scene whose ShapeMesh has a parametric surface.
 *
 * @param world The world
 * @return The shapes
 */
static int32_t bench_parametric_count(
    ecs_world_t *world)
{
    int32_t result = 0;
    ecs_iter_t it = ecs_each(world, ShapeMesh);
    while (ecs_each_next(&it)) {
        const ShapeMesh *shapes = ecs_field(&it, ShapeMesh, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            result += shapes[i].mesh && shapes[i].surface.p[0][0] != 0.0f;
        }
    }

    return result;
}

void Parametric_extract_spaceship_size(
    bench_t *b)
{
    // gpu_instance_data/instance_surface_mesh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_parametric_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 40, 70});
    ecs_set(world, camera, Rotation, {-0.5f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    bench_parametric_scene(world);

    box_render_draws_stats_t stats = {0};
    ecs_progress(world, 0);
    box_render_draws_stats(world, &stats);
    int64_t setup_upload_bytes = stats.upload_bytes;
    int64_t setup_surface_bytes = stats.surface_bytes;
    ecs_progress(world, 0);
    double frames = 0;
    bench_set_items(b, BENCH_PARAMETRIC_SHAPES);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_draws_stats(world, &stats);
        bench_keep(stats.upload_bytes);
        frames ++;
    }

    bench_counter(b, "frames", frames);
    bench_counter(b, "instances", (double)(stats.instances + stats.dynamic));
    bench_counter(b, "parametric", (double)bench_parametric_count(world));
    bench_counter(b, "batches", (double)stats.batches);
    bench_counter(b, "pages", (double)stats.pages);
    bench_counter(b, "setup_upload_bytes", (double)setup_upload_bytes);
    bench_counter(b, "setup_surface_bytes", (double)setup_surface_bytes);
    bench_counter(b, "upload_bytes", (double)stats.upload_bytes);
    bench_render_memory(b, &stats);

    box_render_sync(world);
    ecs_fini(world);
}

void Parametric_gpu_spaceship_size(
    bench_t *b)
{
    // gpu_instance_data/instance_surface_mesh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 40, 70});
    ecs_set(world, camera, Rotation, {-0.5f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {24, 28, 36, 255}}, .lighting.enabled = true,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .shadows = {.enabled = true, .cascades = 4, .resolution = 2048,
            .max_distance = 150, .split_lambda = 0.7f, .depth_bias = 2,
            .normal_offset = 2, .filter_size = 3, .fade = 0.2f, .blend = 0.1f}});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    bench_parametric_scene(world);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    box_render_gpu_stats_t before, after;
    uint32_t word = 0;
    double frames = 0;
    box_render_gpu_stats(world, &before);
    bench_set_items(b, BENCH_PARAMETRIC_SHAPES);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    box_render_gpu_stats(world, &after);
    bench_counter(b, "frames", frames);
    bench_counter(b, "cull_gpu_ns", (double)(after.cull_ns - before.cull_ns));
    bench_counter(b, "parametric", (double)bench_parametric_count(world));
    box_render_draws_stats_t stats = {0};
    box_render_draws_stats(world, &stats);
    bench_counter(b, "pages", (double)stats.pages);
    bench_render_memory(b, &stats);
    ecs_fini(world);
}
