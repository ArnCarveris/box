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
#include "draw.h"

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

/** Check that the GPU holds every stream of a batch as the extract has it.
 *
 * @param world The world
 * @param shape A shape of the batch
 */
static void batch_matches(
    ecs_world_t *world,
    ecs_entity_t shape)
{
    box_render_draws_t *d = box_draws_state(world);
    box_draw_batch_t *b = box_draws_batch(d, ecs_get(world, shape, DrawSlot)->batch);
    const ecs_vec_t *streams[BOX_RENDER_INSTANCE_STREAMS] = {
        &b->transforms, &b->mesh_shadings, &b->surfaces
    };
    uint32_t strides[BOX_RENDER_INSTANCE_STREAMS] = {
        (uint32_t)sizeof(box_render_transform_t),
        (uint32_t)sizeof(box_render_mesh_shading_t),
        (uint32_t)sizeof(box_render_surface_t)
    };
    uint32_t count = (uint32_t)ecs_vec_count(&b->entities);
    int32_t s;
    test_assert(b->page == 0);
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        uint32_t size = count * strides[s];
        void *gpu = ecs_os_malloc((ecs_size_t)size);
        test_bool(box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST + (uint32_t)s,
            b->first * strides[s], size, gpu), true);
        test_assert(!ecs_os_memcmp(gpu, ecs_vec_first(streams[s]), (ecs_size_t)size));
        ecs_os_free(gpu);
    }
}

void InstanceStore_moved_run_and_grown_page_keep_records(void) {
    // gpu_instance_data/instance_page_copy
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t a[2], b[3], added[128];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        a[i] = ecs_new(world);
        ecs_set(world, a[i], Position, {(float)i - 10, 0, 0});
        ecs_set(world, a[i], NGon, {.sides = 3, .radius = 0.5f});
        ecs_set(world, a[i], Rgb, {200, 50, 50});
    }

    for (i = 0; i < 3; i ++) {
        b[i] = ecs_new(world);
        ecs_set(world, b[i], Position, {(float)i, 5, 0});
        ecs_set(world, b[i], NGon, {.sides = 4, .radius = 0.5f});
        ecs_set(world, b[i], Rgb, {50, 200, 50});
    }

    frames(world, 3);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);

    /* The first batch outgrows its run of 64 slots and moves past the second, so the page grows
     * from 128 to 256 slots in the same frame: the old buffers are copied into the new ones and
     * the two old instances from the old run into the new run */
    for (i = 0; i < 64; i ++) {
        added[i] = ecs_new(world);
        ecs_set(world, added[i], Position, {(float)(i % 16) - 8, (float)(i / 16) - 8, 0});
        ecs_set(world, added[i], NGon, {.sides = 3, .radius = 0.5f});
        ecs_set(world, added[i], Rgb, {200, 50, 50});
    }

    box_render_gpu_stats_t after = frames(world, 1);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS);
    test_int(stats.kept_pages, BOX_RENDER_INSTANCE_STREAMS);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);

    /* Moved again, past 128 slots, with a changed instance among the copied ones, which
     * scatters over the copy, and a change of the second batch in the same frame */
    ecs_set(world, a[1], Position, {-12, 1, 0});
    ecs_set(world, b[1], Position, {1, 6, 0});
    for (i = 64; i < 128; i ++) {
        added[i] = ecs_new(world);
        ecs_set(world, added[i], Position, {(float)(i % 16) - 8, (float)(i / 16) - 8, 0});
        ecs_set(world, added[i], NGon, {.sides = 3, .radius = 0.5f});
        ecs_set(world, added[i], Rgb, {200, 50, 50});
    }

    after = frames(world, 1);
    box_render_draws_stats(world, &stats);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS);
    test_int(stats.scattered, 2);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);

    after = frames(world, 2);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);
    ecs_fini(world);
}

/** Add a polygon.
 *
 * @param world The world
 * @param sides Sides of the polygon, which select its mesh
 * @param x Its x position
 * @param y Its y position
 * @return The shape
 */
static ecs_entity_t ngon_new(
    ecs_world_t *world,
    int32_t sides,
    float x,
    float y)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, 0});
    ecs_set(world, e, NGon, {.sides = sides, .radius = 0.5f});
    ecs_set(world, e, Rgb, {(uint8_t)(40 * sides), 50, 50});

    return e;
}

void InstanceStore_moved_run_reused_in_same_frame(void) {
    // gpu_instance_data/instance_page_copy
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t a[4], b[4], c = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        a[i] = ngon_new(world, 3, (float)i - 10, 0);
        b[i] = ngon_new(world, 4, (float)i - 10, 4);
    }

    frames(world, 3);
    box_render_draws_t *d = box_draws_state(world);
    test_uint(box_draws_batch(d, ecs_get(world, a[0], DrawSlot)->batch)->first, 0);
    test_uint(box_draws_batch(d, ecs_get(world, b[0], DrawSlot)->batch)->first, 64);

    /* The first batch moves past the second, and a new batch with the mesh of the second takes
     * the run it left in the same frame: its upload writes the old run after the copy read it.
     * The second batch scatters a change in the same frame */
    for (i = 0; i < 64; i ++) {
        ngon_new(world, 3, (float)(i % 16) - 8, (float)(i / 16) - 8);
    }

    for (i = 0; i < 3; i ++) {
        c = ngon_new(world, 4, (float)i, 8);
        ecs_add(world, c, DynamicTransform);
    }

    ecs_set(world, b[1], Position, {-9, 5, 0});
    box_render_gpu_stats_t after = frames(world, 1);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_uint(box_draws_batch(d, ecs_get(world, a[0], DrawSlot)->batch)->first, 128);
    test_uint(box_draws_batch(d, ecs_get(world, c, DrawSlot)->batch)->first, 0);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS);
    test_int(stats.scattered, 1);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);
    batch_matches(world, c);

    after = frames(world, 2);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    batch_matches(world, b[0]);
    batch_matches(world, c);
    ecs_fini(world);
}
