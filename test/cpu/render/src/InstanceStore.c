/**
 * @file InstanceStore.c
 * @brief Tests for the instance pages every batch shares: runs, pages, uploads and scatters.
 *
 * Every case renders with the null backend and reads the packets it received. A batch per
 * mesh is made with regular polygons of distinct side counts.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Bytes of one scatter entry: the slot and the instance */
#define ENTRY ((int64_t)sizeof(uint32_t) + (int64_t)sizeof(box_render_instance_t))

/** Slots of the smallest run, BOX_DRAWS_MIN_CAPACITY of the extract */
#define MIN_RUN (64u)

/** Bytes of one instance */
#define STRIDE ((uint32_t)sizeof(box_render_instance_t))

/** Create a world that renders with a null backend and has a view.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Add a shape of the batch of a side count.
 *
 * @param world The world
 * @param sides Sides of its polygon, which select its mesh and so its batch
 * @param x Its x position
 * @param dynamic Whether it has DynamicTransform
 * @return The shape
 */
static ecs_entity_t shape_new(
    ecs_world_t *world,
    int32_t sides,
    float x,
    bool dynamic)
{
    ecs_entity_t e = ecs_new(world);
    if (dynamic) {
        ecs_add(world, e, DynamicTransform);
    }

    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, NGon, {.sides = sides, .radius = 0.5f});

    return e;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 * @return The counters of the batches of the frame
 */
static box_render_draws_stats_t frame(
    ecs_world_t *world)
{
    box_render_draws_stats_t stats;
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);

    return stats;
}

/** Check whether a buffer handle is an instance page.
 *
 * @param handle The handle
 * @return Whether it is at or past the first page and below the light buffers
 */
static bool is_page(
    uint32_t handle)
{
    return handle >= BOX_RENDER_BUFFER_FIRST && handle < BOX_RENDER_BUFFER_LIGHT_FIRST;
}

/** Count the uploads of the last packet into instance pages.
 *
 * @param null The backend
 * @param bytes Receives the bytes they upload, NULL to ignore
 * @return The uploads
 */
static int32_t page_uploads(
    const box_render_null_t *null,
    int64_t *bytes)
{
    int32_t i, result = 0;
    if (bytes) {
        *bytes = 0;
    }

    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (is_page(u->buffer)) {
            result ++;
            if (bytes) {
                *bytes += u->size;
            }
        }
    }

    return result;
}

/** Count the uploads of the last packet into a page at a byte offset.
 *
 * @param null The backend
 * @param buffer The page
 * @param offset The byte offset
 * @return The uploads
 */
static int32_t uploads_at(
    const box_render_null_t *null,
    uint32_t buffer,
    uint32_t offset)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        result += u->buffer == buffer && u->offset == offset;
    }

    return result;
}

/** Count the buffer resources of the last packet with an operation and a handle.
 *
 * @param null The backend
 * @param op The operation
 * @param handle The handle
 * @return The resources
 */
static int32_t buffer_ops(
    const box_render_null_t *null,
    box_render_resource_op_t op,
    uint32_t handle)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        result += r->kind == BOX_RENDER_BUFFER && r->op == op && r->handle == handle;
    }

    return result;
}

/** Find the draw of the last packet that draws a number of instances.
 *
 * @param null The backend
 * @param count Instances it draws, unique among the draws
 * @return The draw, NULL when there is none
 */
static const box_render_batch_t* batch_of(
    const box_render_null_t *null,
    uint32_t count)
{
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (b->instance_count == count) {
            result = b;
        }
    }

    return result;
}

/** Get the bytes of a scatter segment as the extract aligns it.
 *
 * @param entries Entries of the segment
 * @return The bytes
 */
static int64_t segment_size(
    int64_t entries)
{
    int64_t size = BOX_RENDER_SCATTER_HEADER * (int64_t)sizeof(uint32_t) + entries * ENTRY;

    return (size + BOX_RENDER_SCATTER_ALIGN - 1) / BOX_RENDER_SCATTER_ALIGN *
        BOX_RENDER_SCATTER_ALIGN;
}

void InstanceStore_static_changes_one_scatter_per_page(void) {
    // gpu_instance_data/instance_store
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t shapes[50][4];
    int32_t i, j;
    for (i = 0; i < 50; i ++) {
        for (j = 0; j < 4; j ++) {
            shapes[i][j] = shape_new(world, 3 + i, (float)j, false);
        }
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.batches, 50);
    test_int(stats.pages, 1);
    test_int(ecs_vec_count(&null->last.batches), 50);
    for (i = 0; i < 50; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        test_uint(b->instances, BOX_RENDER_BUFFER_FIRST);
        test_uint(b->instance_count, 4);
    }

    stats = frame(world);
    test_int(stats.upload_bytes, 0);
    test_int(stats.scatters, 0);

    for (i = 0; i < 10; i ++) {
        ecs_set(world, shapes[i * 5][i % 4], Position, {(float)i, 1, 0});
    }

    stats = frame(world);
    test_int(stats.scatters, 1);
    test_int(stats.scattered, 10);
    test_int(ecs_vec_count(&null->last.scatters), 1);
    const box_render_scatter_t *sc = ecs_vec_first(&null->last.scatters);
    test_uint(sc->buffer, BOX_RENDER_BUFFER_FIRST);
    test_uint(sc->changes, BOX_RENDER_BUFFER_CHANGES);
    test_uint(sc->count, 10);
    test_int(stats.upload_bytes, segment_size(10));
    test_int(page_uploads(null, NULL), 0);

    const box_render_upload_t *changes = NULL;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_CHANGES) {
            test_assert(changes == NULL);
            changes = u;
        }
    }

    test_assert(changes != NULL);
    test_int(changes->size, segment_size(10));
    const uint32_t *words = box_render_packet_data(&null->last, changes->data + sc->offset);
    test_uint(words[0], 10);
    for (i = 0; i < 10; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i * 5);
        uint32_t slot = words[BOX_RENDER_SCATTER_HEADER + (uint32_t)i * (uint32_t)(ENTRY / 4)];
        test_assert(slot >= b->first_instance && slot < b->first_instance + 4);
    }

    stats = frame(world);
    test_int(stats.upload_bytes, 0);
    test_int(stats.scatters, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Run the operations of the freed run case: four batches, the second emptied, then a batch of
 * a new mesh.
 *
 * @param world The world
 */
static void freed_run_ops(
    ecs_world_t *world)
{
    ecs_entity_t second[2];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t a = shape_new(world, 3 + i, 0, false);
        ecs_entity_t b = shape_new(world, 3 + i, 1, false);
        if (i == 1) {
            second[0] = a;
            second[1] = b;
        }
    }

    frame(world);
    ecs_delete(world, second[0]);
    ecs_delete(world, second[1]);
    frame(world);
    for (i = 0; i < 3; i ++) {
        shape_new(world, 20, (float)i, false);
    }

    frame(world);
}

void InstanceStore_freed_run_reused_lowest(void) {
    // gpu_instance_data/instance_store
    box_render_null_t *null, *fresh_null;
    ecs_world_t *world = world_new(&null);
    freed_run_ops(world);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.batches, 4);
    test_int(stats.pages, 1);
    const box_render_batch_t *added = batch_of(null, 3);
    test_assert(added != NULL);
    test_uint(added->instances, BOX_RENDER_BUFFER_FIRST);
    test_uint(added->first_instance, MIN_RUN);
    test_int(uploads_at(null, BOX_RENDER_BUFFER_FIRST, MIN_RUN * STRIDE), 1);

    ecs_world_t *fresh = world_new(&fresh_null);
    freed_run_ops(fresh);
    test_int(ecs_vec_count(&fresh_null->last.batches), ecs_vec_count(&null->last.batches));
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *a = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        const box_render_batch_t *b = ecs_vec_get_t(
            &fresh_null->last.batches, box_render_batch_t, i);
        test_uint(a->instances, b->instances);
        test_uint(a->first_instance, b->first_instance);
        test_uint(a->instance_count, b->instance_count);
    }

    ecs_fini(fresh);
    box_render_null_free(fresh_null);
    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_outgrown_run_moves(void) {
    // gpu_instance_data/instance_store
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i;
    for (i = 0; i < 2; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    for (i = 0; i < 3; i ++) {
        shape_new(world, 4, (float)i, false);
    }

    frame(world);
    test_uint(batch_of(null, 2)->first_instance, 0);
    test_uint(batch_of(null, 3)->first_instance, MIN_RUN);

    for (i = 0; i < MIN_RUN; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 1);
    const box_render_batch_t *moved = batch_of(null, MIN_RUN + 2);
    test_assert(moved != NULL);
    test_uint(moved->first_instance, MIN_RUN * 2);
    test_uint(batch_of(null, 3)->first_instance, MIN_RUN);
    test_int(uploads_at(null, BOX_RENDER_BUFFER_FIRST, MIN_RUN * 2 * STRIDE), 1);
    test_int(uploads_at(null, BOX_RENDER_BUFFER_FIRST, 0), 0);
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_FIRST && u->offset == MIN_RUN * 2 *
            STRIDE)
        {
            test_uint(u->size, (MIN_RUN + 2) * STRIDE);
        }
    }

    stats = frame(world);
    test_int(stats.upload_bytes, 0);

    for (i = 0; i < 5; i ++) {
        shape_new(world, 5, (float)i, false);
    }

    frame(world);
    test_uint(batch_of(null, 5)->first_instance, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_lowered_pages_spread(void) {
    // gpu_instance_data/instance_store
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    box_render_draws_chunk(world, 2);
    box_render_draws_page(world, 4);
    ecs_entity_t shapes[6][2];
    int32_t i, j;
    for (i = 0; i < 6; i ++) {
        for (j = 0; j < 2; j ++) {
            shapes[i][j] = shape_new(world, 3 + i, (float)j, false);
        }
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.batches, 6);
    test_int(stats.pages, 3);
    for (i = 0; i < 6; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        test_uint(b->instances, BOX_RENDER_BUFFER_FIRST + (uint32_t)(i / 2));
        test_uint(b->first_instance, (uint32_t)(i % 2) * 2);
    }

    for (i = 0; i < 3; i ++) {
        test_int(buffer_ops(null, BOX_RENDER_CREATE, BOX_RENDER_BUFFER_FIRST + (uint32_t)i), 1);
    }

    for (i = 0; i < 3; i ++) {
        ecs_set(world, shapes[i * 2][0], Position, {(float)i, 2, 0});
    }

    stats = frame(world);
    test_int(stats.scatters, 3);
    test_int(stats.scattered, 3);
    test_int(ecs_vec_count(&null->last.scatters), 3);
    for (i = 0; i < 3; i ++) {
        const box_render_scatter_t *sc = ecs_vec_get_t(
            &null->last.scatters, box_render_scatter_t, i);
        test_uint(sc->buffer, BOX_RENDER_BUFFER_FIRST + (uint32_t)i);
        test_uint(sc->count, 1);
    }

    test_int(stats.upload_bytes, 3 * segment_size(1));

    for (i = 2; i < 4; i ++) {
        for (j = 0; j < 2; j ++) {
            ecs_delete(world, shapes[i][j]);
        }
    }

    stats = frame(world);
    test_int(stats.batches, 4);
    test_int(stats.pages, 2);
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, BOX_RENDER_BUFFER_FIRST + 1), 1);
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, BOX_RENDER_BUFFER_FIRST), 0);
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, BOX_RENDER_BUFFER_FIRST + 2), 0);

    for (j = 0; j < 2; j ++) {
        shape_new(world, 30, (float)j, false);
    }

    stats = frame(world);
    test_int(stats.pages, 3);
    test_uint(batch_of(null, 2) != NULL, true);
    test_int(buffer_ops(null, BOX_RENDER_CREATE, BOX_RENDER_BUFFER_FIRST + 1), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_dynamic_uploads_one_range_per_batch(void) {
    // gpu_instance_data/instance_store
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i, j;
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 5; j ++) {
            shape_new(world, 3 + i, (float)j, true);
        }
    }

    frame(world);
    int32_t f;
    for (f = 0; f < 3; f ++) {
        int64_t bytes;
        box_render_draws_stats_t stats = frame(world);
        test_int(stats.batches, 3);
        test_int(stats.pages, 1);
        test_int(page_uploads(null, &bytes), 3);
        test_int(bytes, 3 * 5 * STRIDE);
        test_int(stats.upload_bytes, 3 * 5 * STRIDE);
        test_int(stats.scatters, 0);
        test_int(ecs_vec_count(&null->last.scatters), 0);
        for (i = 0; i < 3; i ++) {
            test_int(uploads_at(null, BOX_RENDER_BUFFER_FIRST,
                (uint32_t)i * MIN_RUN * STRIDE), 1);
            test_int(buffer_ops(null, BOX_RENDER_CREATE, BOX_RENDER_BUFFER_FIRST), 0);
        }
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_new_backend_sends_pages_again(void) {
    // gpu_instance_data/instance_store
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    box_render_draws_chunk(world, 2);
    box_render_draws_page(world, 4);
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 2; j ++) {
            shape_new(world, 3 + i, (float)j, false);
        }
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 2);
    stats = frame(world);
    test_int(stats.upload_bytes, 0);

    box_render_null_t *second = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(second);
    box_render_set_backend(world, &backend);
    stats = frame(world);
    test_int(second->packets, 1);
    test_int(stats.pages, 2);
    test_int(buffer_ops(second, BOX_RENDER_CREATE, BOX_RENDER_BUFFER_FIRST), 1);
    test_int(buffer_ops(second, BOX_RENDER_CREATE, BOX_RENDER_BUFFER_FIRST + 1), 1);
    int64_t bytes;
    test_int(page_uploads(second, &bytes), 4);
    test_int(bytes, 4 * 2 * STRIDE);
    for (i = 0; i < 4; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &second->last.batches, box_render_batch_t, i);
        test_uint(b->instances, BOX_RENDER_BUFFER_FIRST + (uint32_t)(i / 2));
        test_uint(b->first_instance, (uint32_t)(i % 2) * 2);
    }

    ecs_fini(world);
    box_render_null_free(null);
    box_render_null_free(second);
}
