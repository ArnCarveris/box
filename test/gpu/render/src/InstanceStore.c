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

/** Check that the GPU holds every stream of a batch as the extract has it, in a page.
 *
 * @param world The world
 * @param shape A shape of the batch
 * @param page Mesh page the batch is in
 */
static void batch_matches_in(
    ecs_world_t *world,
    ecs_entity_t shape,
    int32_t page)
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
    test_assert(b->page == page);
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        uint32_t size = count * strides[s];
        uint32_t handle = BOX_RENDER_BUFFER_FIRST +
            (uint32_t)page * BOX_RENDER_INSTANCE_STREAMS + (uint32_t)s;
        void *gpu = ecs_os_malloc((ecs_size_t)size);
        test_bool(box_render_gpu_read_buffer(world, handle, b->first * strides[s], size, gpu),
            true);
        test_assert(!ecs_os_memcmp(gpu, ecs_vec_first(streams[s]), (ecs_size_t)size));
        ecs_os_free(gpu);
    }
}

/** Check that the GPU holds every stream of a batch of the first page as the extract has it.
 *
 * @param world The world
 * @param shape A shape of the batch
 */
static void batch_matches(
    ecs_world_t *world,
    ecs_entity_t shape)
{
    batch_matches_in(world, shape, 0);
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

void InstanceStore_run_moved_to_other_page_copies_directly(void) {
    // gpu_instance_data/copy_staging_release
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    box_render_draws_page(world, 128);
    ecs_entity_t a[2], b[3];
    int32_t i;
    for (i = 0; i < 2; i ++) {
        a[i] = ngon_new(world, 3, (float)i - 10, 0);
    }

    for (i = 0; i < 3; i ++) {
        b[i] = ngon_new(world, 4, (float)i, 5);
    }

    box_render_gpu_stats_t before = frames(world, 3);
    box_render_draws_t *d = box_draws_state(world);
    test_int(box_draws_batch(d, ecs_get(world, a[0], DrawSlot)->batch)->page, 0);
    test_int(box_draws_batch(d, ecs_get(world, b[0], DrawSlot)->batch)->page, 0);
    test_int(before.copy_staging, 0);
    batch_matches_in(world, a[0], 0);
    batch_matches_in(world, b[0], 0);

    /* The first batch outgrows its run of 64 slots and the page holds at most 128 slots, so it
     * moves to a new page: the copies go from the buffers of the first page straight into the
     * buffers of the second */
    for (i = 0; i < 64; i ++) {
        ngon_new(world, 3, (float)(i % 16) - 8, (float)(i / 16) - 8);
    }

    box_render_gpu_stats_t after = frames(world, 1);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS);
    test_int(after.errors, 0);
    test_int(after.copies_direct - before.copies_direct, BOX_RENDER_INSTANCE_STREAMS);
    test_int(after.copies_staged - before.copies_staged, 0);
    test_int(after.copy_staging, 0);
    test_int(after.copy_staging_peak, 0);
    batch_matches_in(world, a[0], 1);
    batch_matches_in(world, b[0], 0);

    after = frames(world, 2);
    test_int(after.errors, 0);
    batch_matches_in(world, a[0], 1);
    batch_matches_in(world, b[0], 0);
    ecs_fini(world);
}

/** First buffer handle the copy tests create, below the instance pages */
#define COPY_A (12)

/** Second buffer handle the copy tests create */
#define COPY_B (13)

/** Words of each of the buffers of the copy tests */
#define COPY_WORDS (64)

/** What the extract system of the copy tests appends to the packets */
typedef struct copy_script_t {
    int32_t frame;          /**< Packets extracted so far */
    uint32_t words;         /**< Words of each buffer */
    uint32_t big;           /**< Bytes of the copy within COPY_A of the second packet, 0 for
                             *   the copies of copy_script_overlap */
    bool small;             /**< Every later packet copies 64 bytes within COPY_A */
} copy_script_t;

/** Append the copies of the staged copy test: within one buffer, reading what another copy
 * writes, and one direct copy whose destination another copy reads.
 *
 * @param p The packet
 */
static void copy_script_overlap(
    box_render_packet_t *p)
{
    /* Within COPY_A, reading words 0 to 16, which the next copy writes */
    box_render_copy_t *c = box_render_packet_buffer_copy(p);
    *c = (box_render_copy_t){.src = COPY_A, .src_offset = 0, .dst = COPY_A,
        .dst_offset = 128, .size = 64};

    /* Into the other buffer, reading words 0 to 16 of COPY_B, which the next copy writes */
    c = box_render_packet_buffer_copy(p);
    *c = (box_render_copy_t){.src = COPY_B, .src_offset = 0, .dst = COPY_A,
        .dst_offset = 0, .size = 64};

    /* Reads words 16 to 32 of COPY_A, which no copy writes: direct */
    c = box_render_packet_buffer_copy(p);
    *c = (box_render_copy_t){.src = COPY_A, .src_offset = 64, .dst = COPY_B,
        .dst_offset = 0, .size = 64};
}

/** Create two buffers with known words in the first packet, then append the copies of the
 * script.
 *
 * @param it The iterator of the system
 */
static void CopyScript(ecs_iter_t *it) {
    copy_script_t *script = it->ctx;
    box_render_packet_t *p = box_render_packet(it->world);
    uint32_t h, i;
    if (!script->frame) {
        for (h = COPY_A; h <= COPY_B; h ++) {
            box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
                BOX_RENDER_CREATE, h, sizeof(box_render_buffer_desc_t));
            desc->size = (uint64_t)script->words * sizeof(uint32_t);
            desc->usage = BOX_RENDER_BUFFER_COPY;
            uint32_t *words = box_render_packet_upload(p, h, 0,
                script->words * (uint32_t)sizeof(uint32_t));
            for (i = 0; i < script->words; i ++) {
                words[i] = (h == COPY_A ? 1000 : 2000) + i;
            }
        }
    } else if (script->frame == 1 && !script->big) {
        copy_script_overlap(p);
    } else if (script->frame == 1) {
        box_render_copy_t *c = box_render_packet_buffer_copy(p);
        *c = (box_render_copy_t){.src = COPY_A, .src_offset = 0, .dst = COPY_A,
            .dst_offset = script->big, .size = script->big};
    } else if (script->small) {
        box_render_copy_t *c = box_render_packet_buffer_copy(p);
        *c = (box_render_copy_t){.src = COPY_A, .src_offset = 0, .dst = COPY_A,
            .dst_offset = 64, .size = 64};
    }

    script->frame ++;
}

/** Add the extract system of a copy script to a world.
 *
 * @param world The world
 * @param script The script, alive as long as the world
 */
static void copy_script_new(
    ecs_world_t *world,
    copy_script_t *script)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "CopyScript" }),
        .phase = RenderExtract,
        .callback = CopyScript,
        .ctx = script
    });
}

/** Read the words of a buffer of the copy tests.
 *
 * @param world The world
 * @param handle Handle of the buffer
 * @param out Receives COPY_WORDS words
 */
static void copy_read(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t *out)
{
    test_bool(box_render_gpu_read_buffer(world, handle, 0,
        COPY_WORDS * (uint32_t)sizeof(uint32_t), out), true);
}

void InstanceStore_staged_copies_read_old_bytes(void) {
    // gpu_instance_data/copy_staging_release
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    copy_script_t script = {.words = COPY_WORDS};
    uint32_t a[COPY_WORDS], b[COPY_WORDS];
    uint32_t i;
    copy_script_new(world, &script);
    box_render_gpu_stats_t before = frames(world, 1);
    copy_read(world, COPY_A, a);
    copy_read(world, COPY_B, b);
    for (i = 0; i < COPY_WORDS; i ++) {
        test_uint(a[i], 1000 + i);
        test_uint(b[i], 2000 + i);
    }

    /* Every copy reads its source as it was before the copies of the packet: the copy within
     * COPY_A reads the words the copy from COPY_B writes, which reads the words the direct copy
     * writes */
    box_render_gpu_stats_t after = frames(world, 1);
    test_int(script.frame, 2);
    test_int(after.errors, 0);
    test_int(after.copies_staged - before.copies_staged, 2);
    test_int(after.copies_direct - before.copies_direct, 1);
    test_int(after.copy_staging, 64 * 1024);
    copy_read(world, COPY_A, a);
    copy_read(world, COPY_B, b);
    for (i = 0; i < 16; i ++) {
        test_uint(a[i], 2000 + i);
        test_uint(a[16 + i], 1000 + 16 + i);
        test_uint(a[32 + i], 1000 + i);
        test_uint(a[48 + i], 1000 + 48 + i);
        test_uint(b[i], 1000 + 16 + i);
    }

    for (i = 16; i < COPY_WORDS; i ++) {
        test_uint(b[i], 2000 + i);
    }

    ecs_fini(world);
}

void InstanceStore_staging_released_after_frames_without_copies(void) {
    // gpu_instance_data/copy_staging_release
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_entity_t a[4];
    int32_t i, burst;
    for (i = 0; i < 4; i ++) {
        a[i] = ngon_new(world, 3, (float)i - 10, 0);
        ngon_new(world, 4, (float)i - 10, 4);
    }

    box_render_gpu_stats_t before = frames(world, 3);
    test_int(before.copy_staging, 0);

    /* A burst of moves within the first page, which grows: each frame the first batch outgrows
     * its run and moves past the second one in the same buffer, through the staging buffer */
    for (burst = 0; burst < 3; burst ++) {
        for (i = 0; i < 64 << burst; i ++) {
            ngon_new(world, 3, (float)(i % 16) - 8, (float)((i / 16) % 16) - 8);
        }

        frames(world, 1);
    }

    box_render_gpu_stats_t after = frames(world, 0);
    test_int(after.errors, 0);
    test_assert(after.copies_staged - before.copies_staged >= BOX_RENDER_INSTANCE_STREAMS);
    test_assert(after.copy_staging > 0);
    test_assert(after.copy_staging_peak >= after.copy_staging);
    batch_matches(world, a[0]);

    /* Kept while fewer packets than the limit had no staged copies */
    before = after;
    after = frames(world, BOX_RENDER_COPY_STAGING_FRAMES - 1);
    test_int(after.copies_staged, before.copies_staged);
    test_int(after.copy_staging, before.copy_staging);

    /* Released by the packet that reaches it */
    after = frames(world, 1);
    test_int(after.copy_staging, 0);
    test_int(after.copy_staging_peak, before.copy_staging_peak);

    after = frames(world, 3);
    test_int(after.copy_staging, 0);
    test_int(after.errors, 0);
    batch_matches(world, a[0]);
    ecs_fini(world);
}

void InstanceStore_staging_shrinks_while_copies_continue(void) {
    // gpu_instance_data/copy_staging_release
    ecs_world_t *world = world_new();
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    copy_script_t script = {.words = (1 << 20) / 4, .big = 512 * 1024, .small = true};
    int32_t i;
    copy_script_new(world, &script);
    frames(world, 1);

    /* One large staged copy, then a small one in every packet: the staging buffer is never
     * released, and shrinks once a whole period of packets staged only the small ones */
    box_render_gpu_stats_t after = frames(world, 1);
    test_int(after.errors, 0);
    test_int(after.copy_staging, 512 * 1024);
    for (i = 0; i < 2 * BOX_RENDER_COPY_STAGING_FRAMES; i ++) {
        after = frames(world, 1);
        test_assert(after.copy_staging > 0);
    }

    test_int(after.copy_staging, 64 * 1024);
    test_int(after.copy_staging_peak, 512 * 1024);
    test_int(after.errors, 0);
    ecs_fini(world);
}
