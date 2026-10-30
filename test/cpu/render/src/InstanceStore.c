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

/** Bytes of one scatter entry of the transform stream: the slot and the transform */
#define ENTRY ((int64_t)sizeof(uint32_t) + (int64_t)sizeof(box_render_transform_t))

/** Slots of the smallest run, BOX_DRAWS_MIN_CAPACITY of the extract */
#define MIN_RUN (64u)

/** Bytes of one instance in the transform stream */
#define STRIDE ((uint32_t)sizeof(box_render_transform_t))

/** Bytes of one instance in every stream */
#define INSTANCE ((uint32_t)(sizeof(box_render_transform_t) + sizeof(box_render_mesh_shading_t) + \
    sizeof(box_render_surface_t)))

/** Handle of the transform stream of a page */
#define PAGE(index) (BOX_RENDER_BUFFER_FIRST + (uint32_t)(index) * BOX_RENDER_INSTANCE_STREAMS)

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

/** Count the copies of the last packet from a run of a buffer into another of the same
 * buffer.
 *
 * @param null The backend
 * @param buffer The buffer
 * @param src_offset Byte offset of the source
 * @param dst_offset Byte offset of the destination
 * @param size Bytes copied
 * @return The copies
 */
static int32_t copy_of(
    const box_render_null_t *null,
    uint32_t buffer,
    uint32_t src_offset,
    uint32_t dst_offset,
    uint32_t size)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.copies); i ++) {
        const box_render_copy_t *c = ecs_vec_get_t(&null->last.copies, box_render_copy_t, i);
        result += c->src == buffer && c->dst == buffer && c->src_offset == src_offset &&
            c->dst_offset == dst_offset && c->size == size;
    }

    return result;
}

/** Find the description of a buffer create of the last packet.
 *
 * @param null The backend
 * @param handle The handle
 * @return The description, NULL when the packet does not create the buffer
 */
static const box_render_buffer_desc_t* created(
    const box_render_null_t *null,
    uint32_t handle)
{
    const box_render_buffer_desc_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_BUFFER && r->op == BOX_RENDER_CREATE &&
            r->handle == handle && r->size >= sizeof(box_render_buffer_desc_t))
        {
            result = box_render_packet_data(&null->last, r->data);
        }
    }

    return result;
}

/** Bytes of one instance of a stream of a mesh page.
 *
 * @param stream The stream
 * @return The bytes
 */
static uint32_t stride_of(
    int32_t stream)
{
    uint32_t strides[BOX_RENDER_INSTANCE_STREAMS] = {
        [BOX_RENDER_STREAM_TRANSFORM] = (uint32_t)sizeof(box_render_transform_t),
        [BOX_RENDER_STREAM_SHADING] = (uint32_t)sizeof(box_render_mesh_shading_t),
        [BOX_RENDER_STREAM_SURFACE] = (uint32_t)sizeof(box_render_surface_t)
    };

    return strides[stream];
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

    /* The two instances it had are copied from the old run, the new ones upload after them */
    test_int(uploads_at(null, BOX_RENDER_BUFFER_FIRST, MIN_RUN * 2 * STRIDE), 0);
    test_int(uploads_at(null, BOX_RENDER_BUFFER_FIRST, (MIN_RUN * 2 + 2) * STRIDE), 1);
    test_int(uploads_at(null, BOX_RENDER_BUFFER_FIRST, 0), 0);
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_FIRST && u->offset == (MIN_RUN * 2 + 2) * STRIDE) {
            test_uint(u->size, MIN_RUN * STRIDE);
        }
    }

    test_int(copy_of(null, BOX_RENDER_BUFFER_FIRST, 0, MIN_RUN * 2 * STRIDE, 2 * STRIDE), 1);

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
        test_uint(b->instances, PAGE(i / 2));
        test_uint(b->first_instance, (uint32_t)(i % 2) * 2);
    }

    for (i = 0; i < 3; i ++) {
        test_int(buffer_ops(null, BOX_RENDER_CREATE, PAGE(i)), 1);
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
        test_uint(sc->buffer, PAGE(i));
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
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, PAGE(1)), 1);
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, PAGE(0)), 0);
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, PAGE(2)), 0);

    for (j = 0; j < 2; j ++) {
        shape_new(world, 30, (float)j, false);
    }

    stats = frame(world);
    test_int(stats.pages, 3);
    test_uint(batch_of(null, 2) != NULL, true);
    test_int(buffer_ops(null, BOX_RENDER_CREATE, PAGE(1)), 1);

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
    test_int(buffer_ops(second, BOX_RENDER_CREATE, PAGE(0)), 1);
    test_int(buffer_ops(second, BOX_RENDER_CREATE, PAGE(1)), 1);
    int64_t bytes;
    test_int(page_uploads(second, &bytes), 4 * BOX_RENDER_INSTANCE_STREAMS);
    test_int(bytes, 4 * 2 * INSTANCE);
    for (i = 0; i < 4; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &second->last.batches, box_render_batch_t, i);
        test_uint(b->instances, PAGE(i / 2));
        test_uint(b->first_instance, (uint32_t)(i % 2) * 2);
    }

    ecs_fini(world);
    box_render_null_free(null);
    box_render_null_free(second);
}

void InstanceStore_page_memory_mesh_and_2d(void) {
    // gpu_instance_data/instance_memory_counters
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i, j;
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 5; j ++) {
            shape_new(world, 3 + i, (float)j, false);
        }
    }

    for (i = 0; i < 10; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 20, 0, 0});
        ecs_set(world, e, Rect, {10, 10});
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 2);

    /* Three mesh runs of MIN_RUN slots end at 3 * MIN_RUN, so the mesh page buffers double
     * from MIN_RUN to 4 * MIN_RUN slots; the 2D batch fits the smallest buffer */
    int64_t mesh = 4 * MIN_RUN, shape2d = MIN_RUN;
    int64_t transform = (mesh + shape2d) * (int64_t)sizeof(box_render_transform_t);
    int64_t shading = mesh * (int64_t)sizeof(box_render_mesh_shading_t) +
        shape2d * (int64_t)sizeof(box_render_shading_t);
    int64_t surface = (mesh + shape2d) * (int64_t)sizeof(box_render_surface_t);
    test_int(stats.page_slots, mesh + shape2d);
    test_int(stats.page_transform_bytes, transform);
    test_int(stats.page_shading_bytes, shading);
    test_int(stats.page_surface_bytes, surface);
    test_int(stats.page_bytes, transform + shading + surface);

    /* The counters describe the buffers, not the uploads: they hold on a frame without
     * changes */
    stats = frame(world);
    test_int(stats.upload_bytes, 0);
    test_int(stats.page_slots, mesh + shape2d);
    test_int(stats.page_bytes, transform + shading + surface);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_page_memory_drops_with_released_pages(void) {
    // gpu_instance_data/instance_memory_counters
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
    test_int(stats.pages, 3);
    test_int(stats.page_slots, 3 * 4);
    test_int(stats.page_transform_bytes, 3 * 4 * (int64_t)sizeof(box_render_transform_t));
    test_int(stats.page_shading_bytes, 3 * 4 * (int64_t)sizeof(box_render_mesh_shading_t));
    test_int(stats.page_surface_bytes, 3 * 4 * (int64_t)sizeof(box_render_surface_t));
    test_int(stats.page_bytes, 3 * 4 * (int64_t)INSTANCE);

    for (i = 2; i < 4; i ++) {
        for (j = 0; j < 2; j ++) {
            ecs_delete(world, shapes[i][j]);
        }
    }

    stats = frame(world);
    test_int(stats.pages, 2);
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, PAGE(1)), 1);
    test_int(stats.page_slots, 2 * 4);
    test_int(stats.page_transform_bytes, 2 * 4 * (int64_t)sizeof(box_render_transform_t));
    test_int(stats.page_shading_bytes, 2 * 4 * (int64_t)sizeof(box_render_mesh_shading_t));
    test_int(stats.page_surface_bytes, 2 * 4 * (int64_t)sizeof(box_render_surface_t));
    test_int(stats.page_bytes, 2 * 4 * (int64_t)INSTANCE);

    for (j = 0; j < 2; j ++) {
        shape_new(world, 30, (float)j, false);
    }

    stats = frame(world);
    test_int(stats.pages, 3);
    test_int(stats.page_slots, 3 * 4);
    test_int(stats.page_bytes, 3 * 4 * (int64_t)INSTANCE);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_grown_page_keeps_instances(void) {
    // gpu_instance_data/instance_page_copy
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i, s;
    for (i = 0; i < 2; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    frame(world);
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)MIN_RUN * stride_of(s));
        test_assert(desc->usage & BOX_RENDER_BUFFER_COPY);
        test_assert(!(desc->usage & BOX_RENDER_BUFFER_KEEP));
    }

    /* A second batch takes the run after the first, so the page buffers double; they keep the
     * instances of the first batch, which uploads nothing */
    for (i = 0; i < 3; i ++) {
        shape_new(world, 4, (float)i, false);
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 1);
    test_uint(batch_of(null, 2)->first_instance, 0);
    test_uint(batch_of(null, 3)->first_instance, MIN_RUN);
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)MIN_RUN * 2 * stride_of(s));
        test_assert(desc->usage & BOX_RENDER_BUFFER_COPY);
        test_assert(desc->usage & BOX_RENDER_BUFFER_KEEP);
        test_assert(desc->usage & BOX_RENDER_BUFFER_STORAGE);
        test_int(uploads_at(null, PAGE(0) + (uint32_t)s, 0), 0);
        test_int(uploads_at(null, PAGE(0) + (uint32_t)s, MIN_RUN * stride_of(s)), 1);
    }

    int64_t bytes;
    test_int(page_uploads(null, &bytes), BOX_RENDER_INSTANCE_STREAMS);
    test_int(bytes, 3 * INSTANCE);
    test_int(stats.upload_bytes, 3 * INSTANCE);
    test_int(stats.kept_pages, BOX_RENDER_INSTANCE_STREAMS);
    test_int(stats.kept_bytes, MIN_RUN * INSTANCE);
    test_int(stats.copies, 0);
    test_int(ecs_vec_count(&null->last.copies), 0);
    test_int(null->kept, BOX_RENDER_INSTANCE_STREAMS);

    stats = frame(world);
    test_int(stats.upload_bytes, 0);
    test_int(stats.kept_pages, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_moved_run_copies_instances(void) {
    // gpu_instance_data/instance_page_copy
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i, s;
    for (i = 0; i < 2; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    for (i = 0; i < 3; i ++) {
        shape_new(world, 4, (float)i, false);
    }

    frame(world);
    for (i = 0; i < (int32_t)MIN_RUN; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    /* The first batch moves from slot 0 to slot 128, past the second batch, and the page grows
     * from 128 to 256 slots in the same frame */
    box_render_draws_stats_t stats = frame(world);
    test_uint(batch_of(null, MIN_RUN + 2)->first_instance, MIN_RUN * 2);
    test_int(ecs_vec_count(&null->last.copies), BOX_RENDER_INSTANCE_STREAMS);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS);
    test_int(stats.copy_bytes, 2 * INSTANCE);
    test_int(stats.kept_pages, BOX_RENDER_INSTANCE_STREAMS);
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        uint32_t stride = stride_of(s), buffer = PAGE(0) + (uint32_t)s;
        const box_render_copy_t *c = ecs_vec_get_t(&null->last.copies, box_render_copy_t, s);
        test_uint(c->src, buffer);
        test_uint(c->dst, buffer);
        test_uint(c->src_offset, 0);
        test_uint(c->dst_offset, MIN_RUN * 2 * stride);
        test_uint(c->size, 2 * stride);
        test_uint((c->src_offset | c->dst_offset | c->size) % BOX_RENDER_COPY_ALIGN, 0);
        test_assert(created(null, buffer)->usage & BOX_RENDER_BUFFER_KEEP);
        test_int(uploads_at(null, buffer, MIN_RUN * 2 * stride), 0);
        test_int(uploads_at(null, buffer, (MIN_RUN * 2 + 2) * stride), 1);
    }

    /* Only the new instances upload, nothing of the two moved ones or of the second batch */
    int64_t bytes;
    test_int(page_uploads(null, &bytes), BOX_RENDER_INSTANCE_STREAMS);
    test_int(bytes, MIN_RUN * INSTANCE);
    test_int(stats.upload_bytes, MIN_RUN * INSTANCE);
    test_int(stats.scatters, 0);
    test_int(null->copies, BOX_RENDER_INSTANCE_STREAMS);
    test_int(null->copy_bytes, 2 * INSTANCE);

    stats = frame(world);
    test_int(stats.upload_bytes, 0);
    test_int(stats.copies, 0);
    test_int(ecs_vec_count(&null->last.copies), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_moved_run_scatters_copied_changes(void) {
    // gpu_instance_data/instance_page_copy
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t first[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        first[i] = shape_new(world, 3, (float)i, false);
    }

    frame(world);
    ecs_set(world, first[1], Position, {1, 3, 0});
    for (i = 0; i < (int32_t)MIN_RUN; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    /* The changed transform of slot 1 scatters over the copy of the four instances; the other
     * streams only copy them */
    box_render_draws_stats_t stats = frame(world);
    const box_render_batch_t *moved = batch_of(null, MIN_RUN + 4);
    test_assert(moved != NULL);
    uint32_t at = moved->first_instance;
    test_assert(at != 0);
    test_int(stats.copies, BOX_RENDER_INSTANCE_STREAMS);
    test_int(copy_of(null, PAGE(0), 0, at * STRIDE, 4 * STRIDE), 1);
    test_int(stats.scatters, 1);
    test_int(stats.scattered, 1);
    test_int(ecs_vec_count(&null->last.scatters), 1);
    const box_render_scatter_t *sc = ecs_vec_first(&null->last.scatters);
    test_uint(sc->buffer, PAGE(0));
    test_uint(sc->count, 1);
    test_int(uploads_at(null, PAGE(0), (at + 4) * STRIDE), 1);
    int64_t bytes;
    test_int(page_uploads(null, &bytes), BOX_RENDER_INSTANCE_STREAMS);
    test_int(bytes, MIN_RUN * INSTANCE);

    const box_render_upload_t *changes = NULL;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_CHANGES) {
            changes = u;
        }
    }

    test_assert(changes != NULL);
    const uint32_t *words = box_render_packet_data(&null->last, changes->data + sc->offset);
    test_uint(words[0], 1);
    test_uint(words[BOX_RENDER_SCATTER_HEADER], at + 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_moved_out_of_released_page_uploads(void) {
    // gpu_instance_data/instance_page_copy
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    box_render_draws_page(world, (int32_t)MIN_RUN * 2);
    int32_t i, j;
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 2; j ++) {
            shape_new(world, 3 + i, (float)j, false);
        }
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 2);
    test_uint(batch_of(null, 2) != NULL, true);

    /* The third batch, alone in page 1, outgrows its run and fits no page: it moves to page 2
     * and page 1 is released before the copies would run, so it uploads every instance */
    for (i = 0; i < (int32_t)MIN_RUN; i ++) {
        shape_new(world, 5, (float)i, false);
    }

    stats = frame(world);
    test_int(stats.pages, 2);
    test_int(buffer_ops(null, BOX_RENDER_DESTROY, PAGE(1)), 1);
    const box_render_batch_t *moved = batch_of(null, MIN_RUN + 2);
    test_assert(moved != NULL);
    test_uint(moved->instances, PAGE(2));
    test_uint(moved->first_instance, 0);
    test_int(stats.copies, 0);
    test_int(ecs_vec_count(&null->last.copies), 0);
    int64_t bytes;
    test_int(page_uploads(null, &bytes), BOX_RENDER_INSTANCE_STREAMS);
    test_int(bytes, (MIN_RUN + 2) * INSTANCE);
    test_int(uploads_at(null, PAGE(2), 0), 1);

    ecs_fini(world);
    box_render_null_free(null);
}
