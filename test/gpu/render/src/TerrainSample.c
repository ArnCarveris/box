/**
 * @file TerrainSample.c
 * @brief Tests for the sampling of a Terrain on the GPU (TerrainSampled, terrain_sample.wgsl): a
 * compute shader reads the heights, normals and masks of the tiles that draw by absolute world
 * position, which match the CPU queries where the finest level draws, also after a shift of the
 * WorldOrigin, answers from coarser levels further out and finds nothing far beyond them, and a
 * terrain without the tag has no bindings.
 *
 * The scene is test/assets/terrain/sample.flecs: smooth hills of noise with masks, the focus at
 * (32, 32). The probe is test/assets/terrain/sample_probe.wgsl.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "terrain.h"

/** Seconds of a frame */
#define TS_DELTA (1.0f / 60.0f)

/** Frames a test waits at most for the tiles of the terrain */
#define TS_FRAMES (360)

/** Largest difference of a sampled height from the CPU query where level 0 draws, in meters */
#define TS_HEIGHT_EPS (0.03f)

/** Smallest dot product of a sampled normal with the normal of the CPU query */
#define TS_NORMAL_DOT (0.999f)

/** Largest difference of a sampled mask channel from the CPU query, an RGBA8 atlas */
#define TS_MASK_EPS (0.02f)

/** Most points a test probes */
#define TS_POINTS (64)

/** The probe shader */
#define TS_PROBE "test/assets/terrain/sample_probe.wgsl"

/** A point of the probe (ProbePoint of sample_probe.wgsl) */
typedef struct ts_point_t {
    int32_t whole[2];       /**< Absolute x and z, whole meters */
    float rest[2];          /**< The rest */
} ts_point_t;

/** What the probe found at a point (ProbeResult of sample_probe.wgsl) */
typedef struct ts_result_t {
    float found;            /**< 1 when a tile answered and the height alone agrees */
    float level;            /**< Level of the tile */
    float height;           /**< Height in the frame of the terrain */
    float y;                /**< Local world y of the surface */
    float normal[4];        /**< Unit normal */
    float mask[4];          /**< Masks */
} ts_result_t;

/** Create a headless world that renders on the main thread into a capture surface and run the
 * scene of the tests.
 *
 * @param terrain Receives the terrain
 * @return The world
 */
static ecs_world_t* ts_world(
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
    char *path = box_app_locate("test/assets/terrain/sample.flecs");
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    *terrain = ecs_lookup(world, "sample_hills");
    test_assert(*terrain != 0);
    test_bool(ecs_has_id(world, *terrain, TerrainSampled), true);

    return world;
}

/** Run a frame and wait for the backend.
 *
 * @param world The world
 */
static void ts_frame(
    ecs_world_t *world)
{
    ecs_progress(world, TS_DELTA);
    box_render_sync(world);
}

/** Run frames until the tiles the focus wants draw and a rectangle is resident for the CPU
 * queries, then two more, so the heights of the tiles that draw are in their slots.
 *
 * @param world The world
 * @param terrain The terrain
 * @param x0 Minimum x
 * @param z0 Minimum z
 * @param x1 Maximum x
 * @param z1 Maximum z
 */
static void ts_wait(
    ecs_world_t *world,
    ecs_entity_t terrain,
    double x0,
    double z0,
    double x1,
    double z1)
{
    int32_t i;
    for (i = 0; i < TS_FRAMES; i ++) {
        const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
        if (status && status->drawn && !status->pending &&
            box_terrain_resident(world, terrain, x0, z0, x1, z1))
        {
            break;
        }

        ts_frame(world);
    }

    const TerrainStatus *status = ecs_get(world, terrain, TerrainStatus);
    test_assert(status != NULL);
    test_int(status->pending, 0);
    test_bool(box_terrain_resident(world, terrain, x0, z0, x1, z1), true);
    ts_frame(world);
    ts_frame(world);
}

/** Split an absolute coordinate into whole meters and the rest.
 *
 * @param v The coordinate
 * @param whole Receives the whole meters
 * @param rest Receives the rest
 */
static void ts_split(
    double v,
    int32_t *whole,
    float *rest)
{
    double w = floor(v);
    *whole = (int32_t)w;
    *rest = (float)(v - w);
}

/** Sample the terrain at absolute positions with the probe shader.
 *
 * @param world The world
 * @param terrain The terrain
 * @param points The points
 * @param count Number of points, at most TS_POINTS
 * @param out Receives a result per point
 */
static void ts_probe(
    ecs_world_t *world,
    ecs_entity_t terrain,
    const ts_point_t *points,
    int32_t count,
    ts_result_t *out)
{
    test_assert(count > 0 && count <= TS_POINTS);
    test_bool(box_render_gpu_terrain_compute(world, terrain, TS_PROBE, points,
        (uint32_t)(count * ECS_SIZEOF(ts_point_t)), out,
        (uint32_t)(count * ECS_SIZEOF(ts_result_t)), 1), true);
}

/** Sample the terrain at absolute x and z, split in double precision.
 *
 * @param world The world
 * @param terrain The terrain
 * @param xz The points
 * @param count Number of points, at most TS_POINTS
 * @param out Receives a result per point
 */
static void ts_probe_absolute(
    ecs_world_t *world,
    ecs_entity_t terrain,
    const double (*xz)[2],
    int32_t count,
    ts_result_t *out)
{
    ts_point_t points[TS_POINTS];
    int32_t i;
    for (i = 0; i < count; i ++) {
        ts_split(xz[i][0], &points[i].whole[0], &points[i].rest[0]);
        ts_split(xz[i][1], &points[i].whole[1], &points[i].rest[1]);
    }

    ts_probe(world, terrain, points, count, out);
}

/** Fill points on a ring of rings around a center, all within a radius.
 *
 * @param cx X of the center
 * @param cz Z of the center
 * @param radius Largest distance from the center
 * @param out Receives TS_POINTS points
 */
static void ts_points(
    double cx,
    double cz,
    double radius,
    double (*out)[2])
{
    int32_t i;
    for (i = 0; i < TS_POINTS; i ++) {
        double a = (double)i * 2.399963;
        double r = radius * sqrt(((double)i + 0.5) / (double)TS_POINTS);
        out[i][0] = cx + r * cos(a);
        out[i][1] = cz + r * sin(a);
    }
}

/** Check the samples of points against the CPU queries of the terrain where level 0 draws.
 *
 * @param world The world
 * @param terrain The terrain
 * @param xz The points
 * @param results The samples
 * @param count Number of points
 * @param y_offset Local y of the frame of the terrain, which the local y of a sample adds
 */
static void ts_expect_queries(
    ecs_world_t *world,
    ecs_entity_t terrain,
    const double (*xz)[2],
    const ts_result_t *results,
    int32_t count,
    float y_offset)
{
    int32_t i, c;
    for (i = 0; i < count; i ++) {
        const ts_result_t *r = &results[i];
        float h = 0;
        vec3 n;
        vec4 m;
        test_bool(box_terrain_height(world, terrain, xz[i][0], xz[i][1], &h), true);
        test_bool(box_terrain_normal(world, terrain, xz[i][0], xz[i][1], n), true);
        test_bool(box_terrain_mask(world, terrain, xz[i][0], xz[i][1], m), true);
        if (r->found != 1.0f || r->level != 0.0f || fabsf(r->height - h) >= TS_HEIGHT_EPS) {
            printf("sample at %g %g: found %g level %g height %g, query %g\n",
                xz[i][0], xz[i][1], r->found, r->level, r->height, h);
        }

        test_flt(r->found, 1.0f);
        test_flt(r->level, 0.0f);
        test_assert(fabsf(r->height - h) < TS_HEIGHT_EPS);
        test_assert(fabsf(r->y - (r->height + y_offset)) < 1e-3f);
        float dot = r->normal[0] * n[0] + r->normal[1] * n[1] + r->normal[2] * n[2];
        if (dot < TS_NORMAL_DOT) {
            printf("normal at %g %g: %g %g %g, query %g %g %g\n", xz[i][0], xz[i][1],
                r->normal[0], r->normal[1], r->normal[2], n[0], n[1], n[2]);
        }

        test_assert(dot >= TS_NORMAL_DOT);
        for (c = 0; c < 4; c ++) {
            if (fabsf(r->mask[c] - m[c]) >= TS_MASK_EPS) {
                printf("mask %d at %g %g: %g, query %g\n", c, xz[i][0], xz[i][1], r->mask[c],
                    m[c]);
            }

            test_assert(fabsf(r->mask[c] - m[c]) < TS_MASK_EPS);
        }
    }
}

/** Check that the backend logged no error.
 *
 * @param world The world
 */
static void ts_expect_no_errors(
    ecs_world_t *world)
{
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);
}

void TerrainSample_matches_queries(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = ts_world(&terrain);
    ts_wait(world, terrain, -8, -8, 72, 72);
    double xz[TS_POINTS][2];
    ts_result_t results[TS_POINTS];
    ts_points(32, 32, 40, xz);
    ts_probe_absolute(world, terrain, (const double (*)[2])xz, TS_POINTS, results);
    ts_expect_queries(world, terrain, (const double (*)[2])xz, results, TS_POINTS, 0.0f);
    ts_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainSample_tile_edges(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = ts_world(&terrain);
    ts_wait(world, terrain, -8, -8, 72, 72);

    // Points on and next to the edges and corners of the tiles of level 0 and between the
    // vertices of their grid
    double xz[TS_POINTS][2];
    ts_result_t results[TS_POINTS];
    int32_t i;
    for (i = 0; i < TS_POINTS; i ++) {
        double edge = (double)((i / 2) % 3) * 32.0;
        double along = 0.25 + (double)(i / 4) * 3.7;
        double nudge = (i % 3) == 0 ? 0.0 : (i % 3) == 1 ? 1e-3 : -1e-3;
        xz[i][0] = (i & 1) ? edge + nudge : along;
        xz[i][1] = (i & 1) ? along : edge + nudge;
    }

    ts_probe_absolute(world, terrain, (const double (*)[2])xz, TS_POINTS, results);
    ts_expect_queries(world, terrain, (const double (*)[2])xz, results, TS_POINTS, 0.0f);
    ts_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainSample_after_origin_shift(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = ts_world(&terrain);
    ecs_singleton_add(world, WorldOrigin);
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    o->enabled = false;
    ecs_singleton_modified(world, WorldOrigin);
    ts_wait(world, terrain, -8, -8, 72, 72);
    double xz[TS_POINTS][2];
    ts_result_t before[TS_POINTS], after[TS_POINTS];
    ts_points(32, 32, 40, xz);
    ts_probe_absolute(world, terrain, (const double (*)[2])xz, TS_POINTS, before);

    // The terrain stays in the world, so its tiles stay and the samples at the same absolute
    // positions do not change; the local y moves down by the shift
    box_world_origin_shift(world, 0, (double[3]){ 1000.25, 8, -3000.5 });
    ts_frame(world);
    ts_frame(world);
    ts_frame(world);
    o = ecs_singleton_get_mut(world, WorldOrigin);
    test_flt(o->x, 1000.25);
    test_flt(o->y, 8);
    test_flt(o->z, -3000.5);
    ts_probe_absolute(world, terrain, (const double (*)[2])xz, TS_POINTS, after);
    ts_expect_queries(world, terrain, (const double (*)[2])xz, after, TS_POINTS, -8.0f);
    int32_t i;
    for (i = 0; i < TS_POINTS; i ++) {
        test_assert(fabsf(after[i].height - before[i].height) < 1e-4f);
        test_assert(fabsf(after[i].y - (before[i].y - 8.0f)) < 1e-3f);
    }

    // The same positions as the local position of the shifted origin plus a local x and z
    // (terrain_sample_local): whole meters of the origin and its rest plus the local offset
    ts_point_t points[TS_POINTS];
    ts_result_t local[TS_POINTS];
    for (i = 0; i < TS_POINTS; i ++) {
        points[i].whole[0] = 1000;
        points[i].whole[1] = -3001;
        points[i].rest[0] = 0.25f + (float)(xz[i][0] - 1000.25);
        points[i].rest[1] = 0.5f + (float)(xz[i][1] + 3000.5);
    }

    ts_probe(world, terrain, points, TS_POINTS, local);
    ts_expect_queries(world, terrain, (const double (*)[2])xz, local, TS_POINTS, -8.0f);
    ts_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainSample_coarse_levels_further_out(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = ts_world(&terrain);
    ts_wait(world, terrain, -8, -8, 72, 72);

    // Well beyond the range of level 0 (128 m) coarser tiles answer, near the heights of the
    // graph
    double xz[TS_POINTS][2];
    ts_result_t results[TS_POINTS];
    int32_t i, coarse = 0;
    for (i = 0; i < TS_POINTS; i ++) {
        double a = (double)i * 2.399963, r = 300.0 + (double)(i % 8) * 40.0;
        xz[i][0] = 32.0 + r * cos(a);
        xz[i][1] = 32.0 + r * sin(a);
    }

    ts_probe_absolute(world, terrain, (const double (*)[2])xz, TS_POINTS, results);
    for (i = 0; i < TS_POINTS; i ++) {
        float h = 0;
        test_flt(results[i].found, 1.0f);
        test_assert(results[i].level >= 1.0f);
        coarse += results[i].level >= 2.0f;
        if (box_terrain_height(world, terrain, xz[i][0], xz[i][1], &h)) {
            test_assert(fabsf(results[i].height - h) < 0.5f);
        }
    }

    test_assert(coarse > 0);
    ts_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainSample_beyond_windows_not_found(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = ts_world(&terrain);
    ts_wait(world, terrain, -8, -8, 72, 72);
    double xz[2][2] = {{100000.5, 32}, {32, -250000.25}};
    ts_result_t results[2];
    ts_probe_absolute(world, terrain, (const double (*)[2])xz, 2, results);
    int32_t i;
    for (i = 0; i < 2; i ++) {
        test_flt(results[i].found, 0.0f);
        test_flt(results[i].height, 0.0f);
        test_flt(results[i].normal[0], 0.0f);
        test_flt(results[i].normal[1], 1.0f);
        test_flt(results[i].normal[2], 0.0f);
        test_flt(results[i].mask[0], 0.0f);
    }

    ts_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainSample_untagged_has_no_bindings(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = ts_world(&terrain);
    ts_wait(world, terrain, -8, -8, 72, 72);
    ecs_remove_id(world, terrain, TerrainSampled);
    ts_frame(world);
    ts_result_t results[1];
    ts_point_t point = {{32, 32}, {0.5f, 0.5f}};
    test_bool(box_render_gpu_terrain_compute(world, terrain, TS_PROBE, &point,
        ECS_SIZEOF(point), results, ECS_SIZEOF(results), 1), false);

    // Tagged again, it is sampled from the next frame
    ecs_add_id(world, terrain, TerrainSampled);
    ts_frame(world);
    ts_probe(world, terrain, &point, 1, results);
    test_flt(results[0].found, 1.0f);
    ts_expect_no_errors(world);
    ecs_fini(world);
}

void TerrainSample_grid_built_on_change(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = ts_world(&terrain);
    ts_wait(world, terrain, -8, -8, 72, 72);
    int32_t i, heights, grids, h, g;
    for (i = 0; i < 8; i ++) {
        ts_frame(world);
    }

    box_render_terrain_counters(world, &heights, &grids);
    test_assert(grids > 0);
    for (i = 0; i < 8; i ++) {
        ts_frame(world);
    }

    box_render_terrain_counters(world, &h, &g);
    test_int(g, grids);
    test_int(h, heights);
    double xz[TS_POINTS][2];
    ts_result_t results[TS_POINTS];
    ts_points(32, 32, 40, xz);
    ts_probe_absolute(world, terrain, (const double (*)[2])xz, TS_POINTS, results);
    ts_expect_queries(world, terrain, (const double (*)[2])xz, results, TS_POINTS, 0.0f);

    ecs_entity_t walker = ecs_lookup(world, "sample_walker");
    test_assert(walker != 0);
    ecs_set(world, walker, Position, {232, 30, 32});
    ts_frame(world);
    ts_wait(world, terrain, 192, -8, 272, 72);
    box_render_terrain_counters(world, &h, &g);
    test_assert(g > grids);
    ts_points(232, 32, 40, xz);
    ts_probe_absolute(world, terrain, (const double (*)[2])xz, TS_POINTS, results);
    ts_expect_queries(world, terrain, (const double (*)[2])xz, results, TS_POINTS, 0.0f);
    ts_expect_no_errors(world);
    ecs_fini(world);
}
