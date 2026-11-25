/**
 * @file TerrainQuery.c
 * @brief Tests for the CPU queries of a Terrain on the GPU backend: the heights and normals of
 * the query tiles match the known function of the height graph, rays hit the surface where the
 * function says and the hit entity is the terrain, a first person controller walks up a slope
 * and stops at a cliff steeper than it can stand on, and a ChunkGrid cell waits until the
 * terrain under it is resident and then puts a rock at its height.
 *
 * The scene is test/assets/terrain/query.flecs: cones in every 64 m cell of the world, rising
 * half a meter per meter from the center of the cell to 8 m at 16 m, then a cliff of 6 m.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "spatial.h"
#include "terrain.h"

/** Seconds of a frame */
#define TQ_DELTA (1.0f / 60.0f)

/** Frames a test waits at most for the query tiles of a terrain */
#define TQ_FRAMES (240)

/** Largest difference of a queried height from the function in meters */
#define TQ_HEIGHT_EPS (0.05f)

/** Get the height of the cones of the scene at a point.
 *
 * @param x X in meters
 * @param z Z in meters
 * @return The height in meters
 */
static float tq_cone(
    double x,
    double z)
{
    double cx = (floor(x / 64.0) + 0.5) * 64.0, cz = (floor(z / 64.0) + 0.5) * 64.0;
    double d = sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz)) / 64.0;
    double slope = fmin(fmax(d / 0.25, 0.0), 1.0) * 8.0;
    double cliff = fmin(fmax((d - 0.25) / 0.02, 0.0), 1.0) * 6.0;

    return (float)(slope + cliff);
}

/** Create a headless world that renders on the main thread into a capture surface, so the
 * backend evaluates the height graphs, and run the scene of the tests.
 *
 * @param terrain Receives the terrain
 * @return The world
 */
static ecs_world_t* tq_world(
    ecs_entity_t *terrain)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t capture = ecs_entity(world, { .name = "capture" });
    ecs_set(world, capture, Surface, {.width = 64, .height = 48, .resolution_scale = 1});
    char *path = box_app_locate("test/assets/terrain/query.flecs");
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    *terrain = ecs_lookup(world, "query_cones");
    test_assert(*terrain != 0);

    return world;
}

/** Run a frame and wait for the backend.
 *
 * @param world The world
 */
static void tq_frame(
    ecs_world_t *world)
{
    ecs_progress(world, TQ_DELTA);
    box_render_sync(world);
}

/** Run frames until a rectangle of a terrain is resident at its query spacing.
 *
 * @param world The world
 * @param terrain The terrain
 * @param x0 Minimum x
 * @param z0 Minimum z
 * @param x1 Maximum x
 * @param z1 Maximum z
 */
static void tq_wait(
    ecs_world_t *world,
    ecs_entity_t terrain,
    double x0,
    double z0,
    double x1,
    double z1)
{
    int32_t i;
    for (i = 0; i < TQ_FRAMES && !box_terrain_resident(world, terrain, x0, z0, x1, z1); i ++) {
        tq_frame(world);
    }

    test_bool(box_terrain_resident(world, terrain, x0, z0, x1, z1), true);
}

/** Check that the backend logged no error.
 *
 * @param world The world
 */
static void tq_expect_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);
}

void TerrainQuery_heights_match_graph(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain);
    tq_wait(world, terrain, 0, 0, 64, 64);
    static const double points[][2] = {
        {32, 32}, {36, 32}, {32, 22.5}, {40.25, 39.75}, {21.3, 41.7}, {3, 3}, {61, 5},
        {47.5, 32}, {15.36, 32}, {50, 58}
    };
    int32_t i;
    for (i = 0; i < 10; i ++) {
        float h = 0, e = tq_cone(points[i][0], points[i][1]);
        test_bool(box_terrain_height(world, terrain, points[i][0], points[i][1], &h), true);
        if (fabsf(h - e) >= TQ_HEIGHT_EPS) {
            printf("height at %g %g: %g, expected %g\n", points[i][0], points[i][1], h, e);
        }

        test_assert(fabsf(h - e) < TQ_HEIGHT_EPS);
    }

    /* On the slope the normal leans away from the center by half a meter per meter */
    vec3 n;
    float slope = 0;
    test_bool(box_terrain_normal(world, terrain, 40, 32, n), true);
    test_assert(fabsf(n[1] - 1.0f / sqrtf(1.25f)) < 0.01f);
    test_assert(n[0] < -0.4f);
    test_assert(fabsf(n[2]) < 0.01f);
    test_bool(box_terrain_slope(world, terrain, 32, 40, &slope), true);
    test_assert(fabsf(slope - atanf(0.5f)) < 0.01f);

    /* The plateau is flat, far away is not known at query spacing */
    test_bool(box_terrain_normal(world, terrain, 3, 3, n), true);
    test_assert(n[1] > 0.999f);
    test_bool(box_terrain_resident(world, terrain, 1000, 1000, 1001, 1001), false);
    tq_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainQuery_ray_hits(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain);
    tq_wait(world, terrain, 0, 0, 64, 64);
    ecs_entity_t hit = 0;
    float t = 0;
    test_bool(box_terrain_ray(world, 0, (vec3){40, 50, 32}, (vec3){0, -1, 0}, 0, &hit, &t),
        true);
    test_uint(hit, terrain);
    test_assert(fabsf(t - (50 - tq_cone(40, 32))) < TQ_HEIGHT_EPS);

    /* Through the spatial index, a slanted ray from above the plateau over the cone, which it
     * meets just past its center: 34 - x = (x - 32) / 2 at x = 33.3 */
    vec3 origin = {4, 30, 32}, dir = {1, -1, 0};
    box_spatial_hit_t sh;
    box_spatial_ray_t ray = { .origin = {origin[0], origin[1], origin[2]},
        .dir = {dir[0], dir[1], dir[2]}, .shapes = true };
    test_bool(box_spatial_ray(world, &ray, &sh), true);
    test_uint(sh.entity, terrain);
    float x = origin[0] + sh.distance / sqrtf(2.0f);
    float y = origin[1] - sh.distance / sqrtf(2.0f);
    test_assert(fabsf(y - tq_cone(x, 32)) < 0.1f);
    test_assert(x > 32 && x < 35);
    tq_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainQuery_walk_slope_stop_at_cliff(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain);
    ecs_entity_t walker = ecs_lookup(world, "query_walker");
    test_assert(walker != 0);
    tq_wait(world, terrain, 0, 0, 64, 64);
    ecs_add(world, walker, FirstPersonController);
    tq_frame(world);

    /* Walk towards -z: up the cone from its center */
    box_input_key(world, KeyW, true);
    int32_t i;
    for (i = 0; i < 120; i ++) {
        tq_frame(world);
    }

    const Position *p = ecs_get(world, walker, Position);
    float d = 32 - p->z;
    test_assert(d > 6.0f && d < 10.0f);
    test_assert(fabs(p->y - 1.7f - tq_cone(p->x, p->z)) < 0.15f);
    test_bool(ecs_get(world, walker, FirstPersonController)->grounded, true);

    /* The cliff at 16 m from the center is steeper than the walker stands on */
    for (i = 0; i < 360; i ++) {
        tq_frame(world);
    }

    p = ecs_get(world, walker, Position);
    d = 32 - p->z;
    test_assert(d > 14.5f && d < 16.3f);
    test_assert(p->y < 1.7f + 8.5f);
    test_assert(p->y > 1.7f + 7.0f);
    tq_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainQuery_chunk_waits_for_terrain(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain);
    ecs_entity_t grid = ecs_new(world);
    ecs_set(world, grid, ChunkGrid, {
        .template = ecs_lookup(world, "QueryRock"),
        .cell_size = {16, 0, 16},
        .levels = {20},
        .hysteresis = 2,
        .ground = terrain
    });
    test_assert(ecs_get(world, grid, ChunkGrid)->template != 0);
    tq_frame(world);
    const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
    test_assert(s != NULL);
    test_assert(s->held > 0);
    test_assert(box_chunk_grid_cell(world, grid, (int32_t[3]){2, 0, 2}, NULL) == 0);

    int32_t i;
    for (i = 0; i < TQ_FRAMES; i ++) {
        tq_frame(world);
        s = ecs_get(world, grid, ChunkGridStatus);
        if (!s->held && !s->pending && s->cells) {
            break;
        }
    }

    test_int(s->held, 0);
    test_int(s->pending, 0);
    ecs_entity_t cell = box_chunk_grid_cell(world, grid, (int32_t[3]){2, 0, 2}, NULL);
    test_assert(cell != 0);
    ecs_entity_t rock = ecs_lookup_child(world, cell, "rock");
    test_assert(rock != 0);
    float h = 0;
    test_bool(box_terrain_height(world, terrain, 40, 40, &h), true);
    test_flt(ecs_get(world, rock, Position)->y, h);
    test_assert(fabsf(h - tq_cone(40, 40)) < TQ_HEIGHT_EPS);
    tq_expect_no_errors(world);
    ecs_fini(world);
}
