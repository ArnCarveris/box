/**
 * @file Projection.c
 * @brief Benchmarks for ProjectedAnchor: nameplates that follow moving targets through a view.
 *
 * A grid of boxes is watched by a perspective camera. Every box has a nameplate, a Rect with a
 * child Rect, anchored above it. An iteration moves every box and runs a frame.
 */

#include <box.h>
#include <bake_bench.h>

/** Anchored nameplates, one per target */
#define BENCH_ANCHORS (1000)

/** Targets per row of the grid */
#define BENCH_ANCHOR_ROW (40)

void Projection_anchors_1k_moving(
    bench_t *b)
{
    // core_fixes/projection_position
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {40, 30, 60});
    ecs_set(world, camera, Rotation, {-0.4f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t *targets = ecs_os_malloc_n(ecs_entity_t, BENCH_ANCHORS);
    int32_t i, frame = 0;
    for (i = 0; i < BENCH_ANCHORS; i ++) {
        ecs_entity_t t = targets[i] = ecs_new(world);
        ecs_set(world, t, Position,
            {(float)(i % BENCH_ANCHOR_ROW) * 2, 0, -(float)(i / BENCH_ANCHOR_ROW) * 2});
        ecs_set(world, t, Box, {1, 1, 1});
        ecs_entity_t label = ecs_new(world);
        ecs_set(world, label, ProjectedAnchor, {
            .target = t, .view = view, .offset = {0, 1, 0}});
        ecs_set(world, label, Rect, {40, 12});
        ecs_entity_t icon = ecs_new_w_pair(world, EcsChildOf, label);
        ecs_set(world, icon, Position, {-16, 0, 1});
        ecs_set(world, icon, Rect, {8, 8});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    bench_set_items(b, BENCH_ANCHORS);
    while (bench_iter(b)) {
        frame ++;
        float dx = (frame % 2) ? 0.25f : -0.25f;
        for (i = 0; i < BENCH_ANCHORS; i ++) {
            const Position *p = ecs_get(world, targets[i], Position);
            ecs_set(world, targets[i], Position, {p->x + dx, p->y, p->z});
        }

        ecs_progress(world, 0);
    }

    ecs_os_free(targets);
    ecs_fini(world);
}
