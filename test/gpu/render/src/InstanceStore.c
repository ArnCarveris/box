/**
 * @file InstanceStore.c
 * @brief Tests for the instance pages every batch shares, as the GPU backend binds them.
 *
 * A batch per mesh is made with regular polygons of distinct side counts in front of an
 * orthographic camera, with a sun so the shadow cascades draw them as well.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Create a headless world with a view of the polygons, a sun and a surface.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 40, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    v->culling = (CullSettings){.enabled = true, .occlusion = true};
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 32, .resolution_scale = 1});

    return world;
}

/** Add the shapes of a number of batches, two per batch.
 *
 * @param world The world
 * @param batches Batches, each with its own polygon mesh
 * @param out Receives the first shape of each batch, NULL to ignore
 */
static void batches_new(
    ecs_world_t *world,
    int32_t batches,
    ecs_entity_t *out)
{
    int32_t i, j;
    for (i = 0; i < batches; i ++) {
        for (j = 0; j < 2; j ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)(i % 10) * 3 - 15 + (float)j,
                (float)(i / 10) * 3 - 8, 0});
            ecs_set(world, e, NGon, {.sides = 3 + i, .radius = 0.5f});
            ecs_set(world, e, Rgb, {200, 200, 200});
            if (out && !j) {
                out[i] = e;
            }
        }
    }
}

/** Run frames and read the counters of the GPU backend after them.
 *
 * @param world The world
 * @param count Frames to run
 * @return The counters
 */
static box_render_gpu_stats_t frames(
    ecs_world_t *world,
    int32_t count)
{
    box_render_gpu_stats_t stats;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    test_bool(box_render_gpu_stats(world, &stats), true);

    return stats;
}

void InstanceStore_instance_binds_do_not_grow_with_batches(void) {
    // gpu_instance_data/instance_store
    int32_t counts[2] = {5, 50}, binds[2], buffers[2], draws[2], run;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = world_new();
        batches_new(world, counts[run], NULL);
        box_render_gpu_stats_t before = frames(world, 3);
        box_render_gpu_stats_t after = frames(world, 2);
        box_render_draws_stats_t stats;
        box_render_draws_stats(world, &stats);
        test_int(stats.batches, counts[run]);
        test_int(stats.pages, 1);
        test_int(after.errors, 0);
        binds[run] = after.instance_binds - before.instance_binds;
        draws[run] = after.draws - before.draws;
        buffers[run] = after.buffers;
        test_assert(binds[run] > 0);
        ecs_fini(world);
    }

    test_int(binds[0], binds[1]);
    test_int(buffers[0], buffers[1]);
    test_assert(draws[1] > draws[0]);
}

void InstanceStore_scatters_follow_pages_with_changes(void) {
    // gpu_instance_data/instance_store
    ecs_world_t *world = world_new();
    box_render_draws_chunk(world, 2);
    box_render_draws_page(world, 4);
    ecs_entity_t first[6];
    int32_t i;
    batches_new(world, 6, first);
    box_render_gpu_stats_t before = frames(world, 3);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.pages, 3);

    ecs_set(world, first[0], Position, {0, 5, 0});
    ecs_set(world, first[4], Position, {3, 5, 0});
    box_render_gpu_stats_t after = frames(world, 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.scatters, 2);
    test_int(after.scatters - before.scatters, 2);
    test_int(after.scattered - before.scattered, 2);

    before = after;
    for (i = 0; i < 6; i ++) {
        ecs_set(world, first[i], Position, {(float)i * 3, 8, 0});
    }

    after = frames(world, 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.scatters, 3);
    test_int(after.scatters - before.scatters, 3);
    test_int(after.scattered - before.scattered, 6);

    before = after;
    after = frames(world, 1);
    test_int(after.scatters - before.scatters, 0);
    test_int(after.errors, 0);
    ecs_fini(world);
}
