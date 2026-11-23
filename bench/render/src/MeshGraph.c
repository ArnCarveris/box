/**
 * @file MeshGraph.c
 * @brief Benchmarks for a tree of 300 shapes as a hierarchy against the same tree as one
 * MeshGraph shape.
 *
 * A tree is a trunk with 299 small spheres along it. The hierarchy makes it of a Cylinder and
 * 299 IcoSphere children of a root; the graph of a tube along a path and the spheres instanced
 * on points of it, one shape per tree. A case moves a tenth of the trees every frame and times
 * the frames against a backend that drops the packets: transforms, change tracking and extract.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#include "mesh_cache.h"

/** Trees of a case */
#define BENCH_MG_TREES (50)

/** Spheres along the trunk of a tree */
#define BENCH_MG_SPHERES (299)

/** Height of a tree */
#define BENCH_MG_HEIGHT (6.0f)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_mg_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Create a headless Box world with a view that drops its packets.
 *
 * @return The world
 */
static ecs_world_t* bench_mg_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    box_render_backend_t backend = { .render = bench_mg_drop };
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {25, 10, -40});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .culling = {.enabled = true, .min_dimension_px = 1}});

    return world;
}

/** Height of a sphere along the trunk, as PointsOnCurves places it.
 *
 * @param i The sphere
 * @return The height
 */
static float bench_mg_height(
    int32_t i)
{
    return BENCH_MG_HEIGHT * (float)i / (float)(BENCH_MG_SPHERES - 1);
}

/** Create a tree as a hierarchy of shapes.
 *
 * @param world The world
 * @param x Position of the tree along x
 * @return The root of the tree
 */
static ecs_entity_t bench_mg_hierarchy(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {x, 0, 0});
    ecs_entity_t trunk = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, trunk, Position, {0, BENCH_MG_HEIGHT * 0.5f, 0});
    ecs_set(world, trunk, Cylinder, {.segments = 8, .smooth = true,
        .length = BENCH_MG_HEIGHT, .radius = 0.1f});
    ecs_set(world, trunk, Rgb, {120, 80, 40});
    int32_t i;
    for (i = 0; i < BENCH_MG_SPHERES; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_set(world, e, Position, {0, bench_mg_height(i), 0});
        ecs_set(world, e, IcoSphere, {.segments = 1, .smooth = true, .radius = 0.1f});
        ecs_set(world, e, Rgb, {120, 80, 40});
    }

    return root;
}

/** Create the graph of a tree, shared by every tree.
 *
 * @param world The world
 * @return The output node of the graph
 */
static ecs_entity_t bench_mg_graph(
    ecs_world_t *world)
{
    ecs_entity_t path = ecs_new(world);
    ecs_set(world, path, Path3D, {.start = {0, 0, 0},
        .segments = BOX_VEC_VIEW(PathSegment3D, {.kind = PathLine,
            .end = {0, BENCH_MG_HEIGHT, 0}})});
    ecs_entity_t curve = ecs_new(world);
    ecs_set(world, curve, MeshPath, {.path = path, .radius = 0.1f});
    ecs_entity_t tube = ecs_new(world);
    ecs_set(world, tube, MeshTube, {.curves = curve, .sides = 8});
    ecs_entity_t points = ecs_new(world);
    ecs_set(world, points, MeshPointsOnCurves, {.curves = curve, .count = BENCH_MG_SPHERES});
    ecs_entity_t sphere = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, sphere, IcoSphere, {.segments = 1, .smooth = true, .radius = 0.1f});
    ecs_entity_t ball = ecs_new(world);
    ecs_set(world, ball, MeshShape, {.entity = sphere});
    ecs_entity_t balls = ecs_new(world);
    ecs_set(world, balls, MeshInstanceOnPoints, {.points = points, .instance = ball,
        .ignore_rotation = true});
    ecs_entity_t all = ecs_new(world);
    ecs_set(world, all, MeshMerge, {.inputs = BOX_VEC_VIEW(ecs_entity_t, tube, balls)});

    return all;
}

/** Run frames that move a tenth of the trees, then report the shapes and the draws.
 *
 * @param b The bench
 * @param world The world
 * @param roots The roots of the trees
 */
static void bench_mg_run(
    bench_t *b,
    ecs_world_t *world,
    const ecs_entity_t *roots)
{
    box_render_draws_stats_t stats = {0};
    int32_t i, next = 0, frames = 0;
    float y = 0;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    bench_set_items(b, BENCH_MG_TREES);
    while (bench_iter(b)) {
        y += 0.01f;
        for (i = 0; i < BENCH_MG_TREES / 10; i ++) {
            ecs_entity_t e = roots[next];
            next = (next + 1) % BENCH_MG_TREES;
            ecs_set(world, e, Position, {(float)next, y, 0});
        }

        ecs_progress(world, 0);
        frames ++;
    }

    box_render_draws_stats(world, &stats);
    bench_counter(b, "frames", (double)frames);
    bench_counter(b, "shapes", (double)ecs_count_id(world, ecs_id(ShapeMesh)));
    bench_counter(b, "positions", (double)ecs_count_id(world, ecs_id(Position)));
    bench_counter(b, "instances", (double)(stats.instances + stats.dynamic));
    bench_counter(b, "batches", (double)stats.batches);
}

void MeshGraph_hierarchy_300(
    bench_t *b)
{
    ecs_world_t *world = bench_mg_world();
    ecs_entity_t roots[BENCH_MG_TREES];
    int32_t i;
    for (i = 0; i < BENCH_MG_TREES; i ++) {
        roots[i] = bench_mg_hierarchy(world, (float)i);
    }

    bench_mg_run(b, world, roots);
    ecs_fini(world);
}

void MeshGraph_graph_300(
    bench_t *b)
{
    ecs_world_t *world = bench_mg_world();
    ecs_entity_t output = bench_mg_graph(world);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Rgb, {120, 80, 40});
    ecs_set(world, prefab, MeshGraph, {.output = output});
    ecs_entity_t roots[BENCH_MG_TREES];
    int32_t i;
    for (i = 0; i < BENCH_MG_TREES; i ++) {
        roots[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, roots[i], Position, {(float)i, 0, 0});
    }

    bench_mg_run(b, world, roots);
    const MeshGraphStatus *status = ecs_get(world, prefab, MeshGraphStatus);
    bench_counter(b, "ready", status && status->state == MeshGraphReady ? 1.0 : 0.0);
    bench_counter(b, "triangles", status ? (double)status->triangles : 0.0);
    ecs_fini(world);
}
