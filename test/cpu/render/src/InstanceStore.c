/**
 * @file InstanceStore.c
 * @brief Tests for the instance pages every batch shares: runs, pages, uploads and scatters.
 *
 * Every case renders with the null backend and reads the packets it received. A batch per
 * mesh is made with regular polygons of distinct side counts, plain meshes without a surface
 * stream; shapes with a parametric surface have pages of their own with the surface stream.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include "../../../geometry_assets.h"

/** Bytes of one scatter entry of the transform stream: the slot and the transform */
#define ENTRY ((int64_t)sizeof(uint32_t) + (int64_t)sizeof(box_render_transform_t))

/** Slots of the smallest run, BOX_DRAWS_MIN_CAPACITY of the extract */
#define MIN_RUN (16u)

/** Bytes of one instance in the transform stream */
#define STRIDE ((uint32_t)sizeof(box_render_transform_t))

/** Bytes of one plain mesh instance in every stream of its page */
#define INSTANCE ((uint32_t)(sizeof(box_render_transform_t) + sizeof(box_render_mesh_shading_t)))

/** Bytes of one instance in the surface stream */
#define SURFACE ((uint32_t)sizeof(box_render_surface_t))

/** Bytes of one instance with a parametric surface in every stream of its page */
#define SURFACE_INSTANCE (INSTANCE + SURFACE)

/** Streams of a plain mesh page */
#define STREAMS (BOX_RENDER_INSTANCE_STREAMS_PLAIN)

/** Handle of the transform stream of a plain mesh page */
#define PAGE(index) (BOX_RENDER_BUFFER_FIRST + (uint32_t)(index) * BOX_RENDER_INSTANCE_STREAMS)

/** Handle of the transform stream of a mesh page with the surface stream */
#define SURFACE_PAGE(index) \
    (BOX_RENDER_BUFFER_FIRST_SURFACE + (uint32_t)(index) * BOX_RENDER_INSTANCE_STREAMS)

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

/** Find the draw of the last packet that draws the instances of a page.
 *
 * @param null The backend
 * @param page Handle of the transform stream of the page
 * @return The draw, NULL when there is none
 */
static const box_render_batch_t* batch_page(
    const box_render_null_t *null,
    uint32_t page)
{
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (b->instances == page) {
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
    test_int(page_uploads(second, &bytes), 4 * STREAMS);
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

    /* Three mesh runs of MIN_RUN slots end at 3 * MIN_RUN, which the first mesh page buffers
     * hold exactly; the 2D batch fits the smallest buffer */
    int64_t mesh = 3 * MIN_RUN, shape2d = MIN_RUN;
    int64_t transform = (mesh + shape2d) * (int64_t)sizeof(box_render_transform_t);
    int64_t shading = mesh * (int64_t)sizeof(box_render_mesh_shading_t) +
        shape2d * (int64_t)sizeof(box_render_shading_t);
    int64_t surface = 0;
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
    test_int(stats.page_surface_bytes, 0);
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
    test_int(stats.page_surface_bytes, 0);
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
    for (s = 0; s < STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)MIN_RUN * stride_of(s));
        test_assert(desc->usage & BOX_RENDER_BUFFER_COPY);
        test_assert(!(desc->usage & BOX_RENDER_BUFFER_KEEP));
    }

    /* A second batch takes the run after the first, so the page buffers grow to its end, more
     * than half again; they keep the instances of the first batch, which uploads nothing */
    for (i = 0; i < 3; i ++) {
        shape_new(world, 4, (float)i, false);
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 1);
    test_uint(batch_of(null, 2)->first_instance, 0);
    test_uint(batch_of(null, 3)->first_instance, MIN_RUN);
    for (s = 0; s < STREAMS; s ++) {
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
    test_int(page_uploads(null, &bytes), STREAMS);
    test_int(bytes, 3 * INSTANCE);
    test_int(stats.upload_bytes, 3 * INSTANCE);
    test_int(stats.kept_pages, STREAMS);
    test_int(stats.kept_bytes, MIN_RUN * INSTANCE);
    test_int(stats.copies, 0);
    test_int(ecs_vec_count(&null->last.copies), 0);
    test_int(null->kept, STREAMS);

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

    /* The first batch moves from slot 0 to a run of 24 slots at slot 32, past the second batch,
     * and the page grows from 32 to 56 slots in the same frame */
    box_render_draws_stats_t stats = frame(world);
    test_uint(batch_of(null, MIN_RUN + 2)->first_instance, MIN_RUN * 2);
    for (s = 0; s < STREAMS; s ++) {
        test_uint(created(null, PAGE(0) + (uint32_t)s)->size,
            (uint64_t)(MIN_RUN * 2 + 24) * stride_of(s));
    }

    test_int(ecs_vec_count(&null->last.copies), STREAMS);
    test_int(stats.copies, STREAMS);
    test_int(stats.copy_bytes, 2 * INSTANCE);
    test_int(stats.kept_pages, STREAMS);
    for (s = 0; s < STREAMS; s ++) {
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
    test_int(page_uploads(null, &bytes), STREAMS);
    test_int(bytes, MIN_RUN * INSTANCE);
    test_int(stats.upload_bytes, MIN_RUN * INSTANCE);
    test_int(stats.scatters, 0);
    test_int(null->copies, STREAMS);
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

    /* A second batch above the first, so the first moves instead of growing in place */
    shape_new(world, 4, 0, false);
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
    test_int(stats.copies, STREAMS);
    test_int(copy_of(null, PAGE(0), 0, at * STRIDE, 4 * STRIDE), 1);
    test_int(stats.scatters, 1);
    test_int(stats.scattered, 1);
    test_int(ecs_vec_count(&null->last.scatters), 1);
    const box_render_scatter_t *sc = ecs_vec_first(&null->last.scatters);
    test_uint(sc->buffer, PAGE(0));
    test_uint(sc->count, 1);
    test_int(uploads_at(null, PAGE(0), (at + 4) * STRIDE), 1);
    int64_t bytes;
    test_int(page_uploads(null, &bytes), STREAMS);
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
    ecs_entity_t third[2];
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 2; j ++) {
            ecs_entity_t e = shape_new(world, 3 + i, (float)j, false);
            if (i == 2) {
                third[j] = e;
            }
        }
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 2);
    test_uint(batch_of(null, 2) != NULL, true);

    /* Without the third batch, the fourth is alone in page 1, above a hole */
    for (j = 0; j < 2; j ++) {
        ecs_delete(world, third[j]);
    }

    stats = frame(world);
    test_int(stats.pages, 2);

    /* The fourth batch outgrows its run, which cannot grow in place past the slots of the page
     * and fits no page: it moves to page 2 and page 1 is released before the copies would run,
     * so it uploads every instance */
    for (i = 0; i < (int32_t)MIN_RUN; i ++) {
        shape_new(world, 6, (float)i, false);
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
    test_int(page_uploads(null, &bytes), STREAMS);
    test_int(bytes, (MIN_RUN + 2) * INSTANCE);
    test_int(uploads_at(null, PAGE(2), 0), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Add a box, a plain mesh.
 *
 * @param world The world
 * @param x Its x position
 * @return The box
 */
static ecs_entity_t box_new(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});

    return e;
}

/** Give a shape a torus shell, so its ShapeMesh gets a parametric surface.
 *
 * @param world The world
 * @param e The shape
 */
static void torus_set(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_set(world, e, TorusShell, {
        .radius = 1, .tube = 0.25f,
        .ring_start = 0, .ring_end = 2 * (float)GLM_PI,
        .tube_start = 0, .tube_end = 2 * (float)GLM_PI });
}

/** Get the batch of a shape as the extract keeps it.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* batch_get(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(box_draws_state(world), slot->batch);
}

/** Check that a shape is in a batch of the store of its surface and that the batch keeps
 * surfaces only with the surface stream.
 *
 * @param world The world
 * @param e The shape
 * @param surface Whether the shape has a parametric surface
 */
static void test_store_of(
    ecs_world_t *world,
    ecs_entity_t e,
    bool surface)
{
    box_render_draws_t *d = box_draws_state(world);
    box_draw_batch_t *b = batch_get(world, e);
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;
    test_bool(b->key.surface, surface);
    test_assert(box_draws_batch_store(d, b) ==
        &d->stores[surface ? BOX_DRAWS_STORE_SURFACE : BOX_DRAWS_STORE_MESH]);
    test_uint(box_draws_batch_kinds(b), surface ? BOX_DRAW_ALL : BOX_DRAW_BOTH);
    test_int(ecs_vec_count(&b->surfaces), surface ? ecs_vec_count(&b->entities) : 0);
    if (surface) {
        test_assert(box_draws_slot_surface(b, slot)->p[0][0] != 0.0f);
    } else {
        test_assert(box_draws_slot_surface(b, slot) == NULL);
        test_assert(!b->bounded);
    }
}

/** Plain meshes take pages without the surface stream, shapes with a parametric surface pages
 * of the surface store with all three streams. */
void InstanceStore_parametric_shapes_use_surface_store(void) {
    // gpu_instance_data/instance_surface_mesh
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t plain[4], parametric[3];
    int32_t i, s;
    for (i = 0; i < 4; i ++) {
        plain[i] = shape_new(world, 3, (float)i, false);
    }

    for (i = 0; i < 3; i ++) {
        parametric[i] = ecs_new(world);
        ecs_set(world, parametric[i], Position, {(float)i, 2, 0});
        torus_set(world, parametric[i]);
    }

    box_render_draws_stats_t stats = frame(world);
    for (i = 0; i < 4; i ++) {
        test_store_of(world, plain[i], false);
    }

    for (i = 0; i < 3; i ++) {
        test_store_of(world, parametric[i], true);
    }

    test_int(stats.pages, 2);
    test_uint(batch_of(null, 4)->instances, PAGE(0));
    test_uint(batch_of(null, 3)->instances, SURFACE_PAGE(0));
    test_int(stats.transform_bytes, 7 * STRIDE);
    test_int(stats.shading_bytes, 7 * (int64_t)sizeof(box_render_mesh_shading_t));
    test_int(stats.surface_bytes, 3 * SURFACE);
    test_int(stats.page_slots, 2 * MIN_RUN);
    test_int(stats.page_surface_bytes, MIN_RUN * SURFACE);
    test_int(stats.page_bytes, MIN_RUN * (INSTANCE + SURFACE_INSTANCE));

    /* The plain page has no surface buffer, the surface page one of MIN_RUN records */
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        bool plain_stream = s != BOX_RENDER_STREAM_SURFACE;
        test_int(buffer_ops(null, BOX_RENDER_CREATE, PAGE(0) + (uint32_t)s), plain_stream);
        const box_render_buffer_desc_t *desc = created(null, SURFACE_PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)MIN_RUN * stride_of(s));
        test_int(uploads_at(null, PAGE(0) + (uint32_t)s, 0), plain_stream);
        test_int(uploads_at(null, SURFACE_PAGE(0) + (uint32_t)s, 0), 1);
    }

    stats = frame(world);
    test_int(stats.upload_bytes, 0);

    /* A moved plain mesh scatters its transform only; a moved parametric shape its transform
     * into the surface page */
    ecs_set(world, plain[1], Position, {1, 1, 0});
    ecs_set(world, parametric[1], Position, {1, 3, 0});
    stats = frame(world);
    test_int(stats.surface_bytes, 0);
    test_int(stats.shading_bytes, 0);
    test_int(stats.scatters, 2);
    test_int(stats.scattered, 2);
    for (i = 0; i < ecs_vec_count(&null->last.scatters); i ++) {
        const box_render_scatter_t *sc = ecs_vec_get_t(
            &null->last.scatters, box_render_scatter_t, i);
        test_assert(sc->buffer == PAGE(0) || sc->buffer == SURFACE_PAGE(0));
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** Every kind of parametric surface keys its shape into the surface store; the other meshes,
 * whatever their shape, stay plain. */
void InstanceStore_every_surface_kind_uses_surface_store(void) {
    // gpu_instance_data/instance_surface_mesh
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t kinds[7];
    int32_t i;
    for (i = 0; i < 7; i ++) {
        kinds[i] = ecs_new(world);
        ecs_set(world, kinds[i], Position, {(float)i * 3, 0, 0});
    }

    Sweep sweep = test_sweep(world);
    ecs_set_ptr(world, kinds[0], Sweep, &sweep);

    ecs_entity_t square = test_profile(world,
        (vec2[]){{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}, 4, NULL, 0);
    Loft loft = {.cap_start = true, .cap_end = true};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) =
        (LoftSection){.profile = square, .scale = {0.5f, 0.5f}};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) =
        (LoftSection){.profile = square, .offset = {0, 1, 0}, .scale = {1, 1}};
    ecs_set_ptr(world, kinds[1], Loft, &loft);
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);

    Lathe lathe = {.cap_start = true, .cap_end = true};
    vec2 wall[2] = {{1, -1}, {1, 1}};
    lathe.profile = test_path2(world, wall, 2, false);
    ecs_set_ptr(world, kinds[2], Lathe, &lathe);

    ecs_set(world, kinds[3], EllipsoidShell, {
        .radius = {1, 0.5f, 1}, .longitude_end = 2 * (float)GLM_PI,
        .latitude_start = -(float)GLM_PI_2, .latitude_end = (float)GLM_PI_2 });
    torus_set(world, kinds[4]);

    BeveledPrism prism = {.depth = 1, .bevel = 0.1f};
    vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    ecs_vec_init_t(NULL, &prism.footprint, vec2, 4);
    ecs_vec_set_count_t(NULL, &prism.footprint, vec2, 4);
    ecs_os_memcpy(ecs_vec_first(&prism.footprint), corners, ECS_SIZEOF(corners));
    ecs_set_ptr(world, kinds[5], BeveledPrism, &prism);
    ecs_vec_fini_t(NULL, &prism.footprint, vec2);

    ecs_set(world, kinds[6], ProfileExtrusion, {.profile = square, .depth = 1,
        .cap_start = true, .cap_end = true});

    ecs_entity_t box = box_new(world, -3), polygon = shape_new(world, 5, -6, false);
    ecs_entity_t sphere = ecs_new(world);
    ecs_set(world, sphere, Position, {-9, 0, 0});
    ecs_set(world, sphere, Sphere, {.segments = 8, .radius = 0.5f});
    box_render_draws_stats_t stats = frame(world);
    for (i = 0; i < 7; i ++) {
        test_store_of(world, kinds[i], true);
        test_flt(box_draws_slot_surface(batch_get(world, kinds[i]),
            ecs_get(world, kinds[i], DrawSlot)->slot)->p[0][0], (float)(i + 1));
    }

    test_store_of(world, box, false);
    test_store_of(world, polygon, false);
    test_store_of(world, sphere, false);
    /* Seven runs of MIN_RUN slots end at 7 * MIN_RUN, which the first surface page buffers
     * hold exactly */
    test_int(stats.surface_bytes, 7 * SURFACE);
    test_int(stats.page_surface_bytes, 7 * MIN_RUN * SURFACE);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A shape whose ShapeMesh gains a parametric surface moves from the plain store to the surface
 * store, and back when it loses it; its old slot is released each time. */
void InstanceStore_shape_moves_to_surface_store_and_back(void) {
    // gpu_instance_data/instance_surface_mesh
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t moving = box_new(world, 0), stay = box_new(world, 2);
    int32_t s;
    frame(world);
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.upload_bytes, 0);
    box_draw_batch_t *plain = batch_get(world, stay);
    int32_t plain_index = ecs_get(world, stay, DrawSlot)->batch;
    test_store_of(world, moving, false);
    test_int(ecs_get(world, moving, DrawSlot)->batch, plain_index);
    test_int(ecs_get(world, moving, DrawSlot)->slot, 0);
    test_int(ecs_vec_count(&plain->entities), 2);

    /* Gains a surface: a batch of the surface store, which uploads all three streams of its
     * run; the plain batch releases the slot, and its last instance moves into it */
    torus_set(world, moving);
    stats = frame(world);
    test_int(stats.pages, 2);
    test_store_of(world, moving, true);
    test_store_of(world, stay, false);
    test_assert(ecs_get(world, moving, DrawSlot)->batch != plain_index);
    test_int(ecs_vec_count(&plain->entities), 1);
    test_uint(*ecs_vec_get_t(&plain->entities, ecs_entity_t, 0), stay);
    test_int(ecs_get(world, stay, DrawSlot)->slot, 0);
    test_int(stats.surface_bytes, SURFACE);
    test_int(stats.page_surface_bytes, MIN_RUN * SURFACE);
    test_assert(batch_page(null, SURFACE_PAGE(0)) != NULL);
    test_uint(batch_page(null, SURFACE_PAGE(0))->instance_count, 1);
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        test_int(buffer_ops(null, BOX_RENDER_CREATE, SURFACE_PAGE(0) + (uint32_t)s), 1);
        test_int(uploads_at(null, SURFACE_PAGE(0) + (uint32_t)s, 0), 1);
    }

    test_int(buffer_ops(null, BOX_RENDER_CREATE, PAGE(0) + BOX_RENDER_STREAM_SURFACE), 0);
    test_int(uploads_at(null, PAGE(0) + BOX_RENDER_STREAM_SURFACE, 0), 0);
    for (s = 0; s < ecs_vec_count(&null->last.scatters); s ++) {
        const box_render_scatter_t *sc = ecs_vec_get_t(
            &null->last.scatters, box_render_scatter_t, s);
        test_assert(sc->buffer == PAGE(0) + BOX_RENDER_STREAM_TRANSFORM ||
            sc->buffer == PAGE(0) + BOX_RENDER_STREAM_SHADING);
    }

    stats = frame(world);
    test_int(stats.upload_bytes, 0);

    /* Loses the surface: back into the plain batch, after the instance that stayed. The batch
     * of the surface store is empty, so its run is released; page 0 of a store keeps its
     * buffers, as it does for every store */
    ecs_set(world, moving, Box, {1, 1, 1});
    stats = frame(world);
    test_store_of(world, moving, false);
    test_int(ecs_get(world, moving, DrawSlot)->batch, plain_index);
    test_int(ecs_get(world, moving, DrawSlot)->slot, 1);
    test_int(ecs_vec_count(&plain->entities), 2);
    test_int(stats.batches, 1);
    test_int(stats.surface_bytes, 0);
    test_int(stats.upload_bytes, stats.transform_bytes + stats.shading_bytes);
    for (s = 0; s < BOX_RENDER_INSTANCE_STREAMS; s ++) {
        test_int(uploads_at(null, SURFACE_PAGE(0) + (uint32_t)s, 0), 0);
        test_int(buffer_ops(null, BOX_RENDER_DESTROY, PAGE(0) + (uint32_t)s), 0);
    }

    test_assert(batch_page(null, SURFACE_PAGE(0)) == NULL);
    test_assert(batch_page(null, PAGE(0)) != NULL);
    test_uint(batch_page(null, PAGE(0))->instance_count, 2);

    stats = frame(world);
    test_int(stats.upload_bytes, 0);

    /* And gains it again: the surface store hands out the lowest run of its page 0 again,
     * which keeps its buffers */
    torus_set(world, moving);
    stats = frame(world);
    test_store_of(world, moving, true);
    test_int(ecs_vec_count(&plain->entities), 1);
    test_int(stats.surface_bytes, SURFACE);
    test_int(stats.page_surface_bytes, MIN_RUN * SURFACE);
    test_assert(batch_page(null, SURFACE_PAGE(0)) != NULL);
    test_uint(batch_page(null, SURFACE_PAGE(0))->first_instance, 0);
    test_int(buffer_ops(null, BOX_RENDER_CREATE, SURFACE_PAGE(0) + BOX_RENDER_STREAM_SURFACE), 0);
    test_int(uploads_at(null, SURFACE_PAGE(0) + BOX_RENDER_STREAM_SURFACE, 0), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Each store derives its chunk from the largest stride of its layout, so that no page buffer
 * exceeds 128 MiB: 2D pages from the 2D shading, plain mesh pages from the transform and
 * surface pages from the surface record. */
void InstanceStore_store_chunk_from_largest_stride(void) {
    // gpu_instance_data/store_chunk_size
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    box_render_draws_t *d = box_draws_state(world);
    uint32_t page = 128u << 20, i;
    uint32_t mesh = (uint32_t)(sizeof(box_render_transform_t) >
        sizeof(box_render_mesh_shading_t) ? sizeof(box_render_transform_t) :
        sizeof(box_render_mesh_shading_t));
    uint32_t shape2d = (uint32_t)(sizeof(box_render_transform_t) >
        sizeof(box_render_shading_t) ? sizeof(box_render_transform_t) :
        sizeof(box_render_shading_t));
    test_uint(mesh, 48);
    test_uint(shape2d, 64);
    test_uint(d->stores[BOX_DRAWS_STORE_MESH].chunk, page / mesh);
    test_uint(d->stores[BOX_DRAWS_STORE_MESH].chunk, 2796202);
    test_uint(d->stores[BOX_DRAWS_STORE_2D].chunk, page / shape2d);
    test_uint(d->stores[BOX_DRAWS_STORE_2D].chunk, 2097152);
    test_uint(d->stores[BOX_DRAWS_STORE_SURFACE].chunk, page / SURFACE);
    test_uint(d->stores[BOX_DRAWS_STORE_SURFACE].chunk, 349525);
    test_uint(BOX_DRAWS_CHUNK, 349525);
    for (i = 0; i < BOX_DRAWS_STORES; i ++) {
        const box_draw_store_t *s = &d->stores[i];
        test_uint(s->slots, s->chunk);
        test_uint(s->max, s->chunk);
        for (int32_t j = 0; j < s->layout.streams; j ++) {
            test_assert((uint64_t)s->max * s->layout.stride[j] <= page);
        }
    }

    /* The test sizes lower every store, capped by its own chunk */
    box_render_draws_chunk(world, 3000000);
    box_render_draws_page(world, 3000000);
    test_uint(d->stores[BOX_DRAWS_STORE_MESH].chunk, 2796202);
    test_uint(d->stores[BOX_DRAWS_STORE_MESH].slots, 2796202);
    test_uint(d->stores[BOX_DRAWS_STORE_SURFACE].chunk, 349525);
    test_uint(d->stores[BOX_DRAWS_STORE_SURFACE].slots, 349525);
    box_render_draws_chunk(world, 2);
    box_render_draws_page(world, 4);
    for (i = 0; i < BOX_DRAWS_STORES; i ++) {
        test_uint(d->stores[i].chunk, 2);
        test_uint(d->stores[i].slots, 4);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_grow_steps(void) {
    // gpu_instance_data/instance_growth
    static const uint32_t steps[] = {16, 24, 36, 56, 84, 128, 192, 288, 432, 648};
    int32_t i;
    test_uint(BOX_DRAWS_MIN_CAPACITY, MIN_RUN);
    test_uint(box_draws_grow(0, 0), MIN_RUN);
    test_uint(box_draws_grow(0, 1), MIN_RUN);
    for (i = 0; i < (int32_t)(sizeof(steps) / sizeof(steps[0])); i ++) {
        test_uint(box_draws_grow(0, steps[i]), steps[i]);
        if (i) {
            test_uint(box_draws_grow(0, steps[i - 1] + 1), steps[i]);
            test_uint(box_draws_grow(steps[i - 1], steps[i - 1] + 1), steps[i]);
        }

        test_uint(steps[i] % BOX_DRAWS_GROW_ALIGN, 0);
    }

    /* A capacity below the minimum grows from the minimum, one off the alignment from its
     * rounded value */
    test_uint(box_draws_grow(5, 6), MIN_RUN);
    test_uint(box_draws_grow(17, 20), 20);
    test_uint(box_draws_grow(17, 21), 32);

    /* From its own count a run holds the count rounded up to the alignment, at least the
     * minimum: the run of a new batch */
    test_uint(box_draws_grow(0, 0), MIN_RUN);
    test_uint(box_draws_grow(5, 5), MIN_RUN);
    test_uint(box_draws_grow(16, 16), MIN_RUN);
    test_uint(box_draws_grow(17, 17), 20);
    test_uint(box_draws_grow(1000, 1000), 1000);
    test_uint(box_draws_grow(1001, 1001), 1004);
    test_uint(box_draws_grow(110000, 110000), 110000);

    /* A run that outgrows its slots takes the step at or above its count */
    test_uint(box_draws_grow(0, 1001), 1460);
    test_uint(box_draws_grow(0, 21), 24);

    /* The steps stay within a factor of 1.5 of the count, past the minimum */
    for (i = (int32_t)MIN_RUN + 1; i < 100000; i += 7) {
        uint32_t step = box_draws_grow(0, (uint32_t)i);
        test_assert(step >= (uint32_t)i);
        test_assert((uint64_t)step * 2 <= (uint64_t)i * 3 + 2 * BOX_DRAWS_GROW_ALIGN);
    }
}

void InstanceStore_runs_grow_by_half(void) {
    // gpu_instance_data/instance_growth
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i, s;
    for (i = 0; i < 17; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    shape_new(world, 4, 0, false);

    /* A new batch of 17 instances takes a run of 17 rounded up to 20 slots, so the second batch
     * starts at slot 20 and the first page buffers hold the 36 slots of both runs */
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 1);
    test_uint(batch_of(null, 17)->first_instance, 0);
    test_uint(batch_of(null, 1)->first_instance, 20);
    test_int(stats.page_slots, 36);
    for (s = 0; s < STREAMS; s ++) {
        test_uint(created(null, PAGE(0) + (uint32_t)s)->size, (uint64_t)36 * stride_of(s));
    }

    /* Up to 20 instances the run holds them: nothing moves and no buffer grows */
    for (i = 17; i < 20; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    stats = frame(world);
    test_uint(batch_of(null, 20)->first_instance, 0);
    test_int(stats.copies, 0);
    test_int(stats.kept_pages, 0);
    test_int(buffer_ops(null, BOX_RENDER_CREATE, PAGE(0)), 0);

    /* The 21st moves the batch to the growth step of 24 slots at slot 36; the page grows from
     * 36 slots by half to 56, then to the 60 slots its top reaches */
    shape_new(world, 3, 20, false);
    stats = frame(world);
    test_uint(batch_of(null, 21)->first_instance, 36);
    test_int(stats.copies, STREAMS);
    test_int(stats.kept_pages, STREAMS);
    test_int(stats.page_slots, 60);
    for (s = 0; s < STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)60 * stride_of(s));
        test_uint(desc->size % BOX_RENDER_COPY_ALIGN, 0);
        test_assert(desc->usage & BOX_RENDER_BUFFER_KEEP);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** A plain mesh batch is not split at the chunk of the surface store, 349,525 instances, while
 * a batch with a parametric surface is: plain boxes fill one batch of one page past it. */
void InstanceStore_plain_batch_not_split_at_surface_chunk(void) {
    // gpu_instance_data/store_chunk_size
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    uint32_t count = BOX_DRAWS_CHUNK + 1, i;
    for (i = 0; i < count; i ++) {
        box_new(world, (float)(i % 1000));
    }

    ecs_entity_t parametric = ecs_new(world);
    ecs_set(world, parametric, Position, {0, 2, 0});
    torus_set(world, parametric);

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.batches, 2);
    test_int(stats.pages, 2);
    /* The surface store keys a batch only up to its chunk: its batch continues in another
     * once it holds 349,525 instances */
    const box_render_batch_t *plain = batch_of(null, count);
    test_assert(plain != NULL);
    test_uint(plain->instances, PAGE(0));
    test_uint(plain->first_instance, 0);
    test_assert(batch_page(null, PAGE(1)) == NULL);

    box_render_draws_t *d = box_draws_state(world);
    box_draw_batch_t *b = batch_get(world, parametric);
    test_bool(b->key.surface, true);
    test_uint(box_draws_key_limit(d, &b->key), BOX_DRAWS_CHUNK);
    box_draw_key_t key = b->key;
    key.surface = false;
    test_uint(box_draws_key_limit(d, &key), 2796202);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_page_grows_by_half(void) {
    // gpu_instance_data/instance_growth
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i, s;
    for (i = 0; i < 4; i ++) {
        shape_new(world, 3 + i, 0, false);
    }

    /* Four runs of MIN_RUN slots: the first buffers hold their 64 slots */
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.page_slots, 4 * MIN_RUN);

    /* A fifth run ends at 80 slots, less than half again: the buffers grow by half to 96 */
    shape_new(world, 7, 0, false);
    stats = frame(world);
    test_int(stats.kept_pages, STREAMS);
    test_int(stats.page_slots, 96);
    for (s = 0; s < STREAMS; s ++) {
        test_uint(created(null, PAGE(0) + (uint32_t)s)->size, (uint64_t)96 * stride_of(s));
    }

    /* A sixth fits the 96 slots, so the buffers stay */
    shape_new(world, 8, 0, false);
    stats = frame(world);
    test_int(stats.kept_pages, 0);
    test_int(stats.page_slots, 96);
    test_int(buffer_ops(null, BOX_RENDER_CREATE, PAGE(0)), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_page_growth_stops_at_page_slots(void) {
    // gpu_instance_data/instance_growth
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    box_render_draws_page(world, 70);
    int32_t i;
    for (i = 0; i < 2; i ++) {
        shape_new(world, 3 + i, 0, false);
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.page_slots, 2 * MIN_RUN);

    /* A third run ends at 48 slots, which half again holds */
    shape_new(world, 5, 0, false);
    stats = frame(world);
    test_int(stats.pages, 1);
    test_int(stats.page_slots, 48);

    /* A fourth ends at 64; half again would be 72, past the 70 slots of the page */
    shape_new(world, 6, 0, false);
    stats = frame(world);
    test_int(stats.pages, 1);
    test_int(stats.page_slots, 70);
    test_uint(created(null, PAGE(0))->size, (uint64_t)70 * STRIDE);

    /* A fifth fits no more: it opens a second page, the first keeps its 70 slots */
    shape_new(world, 7, 0, false);
    stats = frame(world);
    test_int(stats.pages, 2);
    test_int(stats.page_slots, 70 + MIN_RUN);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_top_run_grows_in_place(void) {
    // gpu_instance_data/instance_growth
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i, s;
    for (i = 0; i < 4; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.page_slots, MIN_RUN);

    /* The batch outgrows its run, which ends at the top of the page: the run grows in place to
     * 24 slots, nothing is copied and only the page buffers grow, keeping the four instances */
    for (i = 4; i < 20; i ++) {
        shape_new(world, 3, (float)i, false);
    }

    stats = frame(world);
    test_uint(batch_of(null, 20)->first_instance, 0);
    test_int(stats.copies, 0);
    test_int(ecs_vec_count(&null->last.copies), 0);
    test_int(stats.kept_pages, STREAMS);
    test_int(stats.page_slots, 24);
    test_int(uploads_at(null, PAGE(0), 0), 0);
    for (s = 0; s < STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)24 * stride_of(s));
        test_assert(desc->usage & BOX_RENDER_BUFFER_KEEP);
    }

    /* No hole is left below it: the next batch starts at the end of the grown run */
    shape_new(world, 4, 0, false);
    stats = frame(world);
    test_uint(batch_of(null, 1)->first_instance, 24);
    test_uint(batch_of(null, 20)->first_instance, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_new_batch_gets_exact_run(void) {
    // gpu_instance_data/instance_run_exact
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t first = 0;
    int32_t i, s;
    for (i = 0; i < 1000; i ++) {
        ecs_entity_t e = shape_new(world, 3, (float)(i % 100), false);
        if (!i) {
            first = e;
        }
    }

    /* 1,000 instances created in one frame take a run of 1,000 slots, not the growth step of
     * 1,460, and the first page buffers hold just that run */
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.pages, 1);
    test_uint(batch_of(null, 1000)->first_instance, 0);
    test_uint(batch_get(world, first)->capacity, 1000);
    test_int(stats.page_slots, 1000);
    for (s = 0; s < STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)1000 * stride_of(s));
        test_uint(desc->size % BOX_RENDER_COPY_ALIGN, 0);
    }

    /* One more instance grows the run to the next growth step, 1,460 slots; the run ends at the
     * top of the page, so it extends in place without a copy. The page buffers grow by half */
    shape_new(world, 3, 0, false);
    stats = frame(world);
    test_uint(batch_of(null, 1001)->first_instance, 0);
    test_uint(batch_get(world, first)->capacity, 1460);
    test_int(stats.copies, 0);
    test_int(ecs_vec_count(&null->last.copies), 0);
    test_int(stats.kept_pages, STREAMS);
    test_int(stats.page_slots, 1500);
    for (s = 0; s < STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)1500 * stride_of(s));
        test_assert(desc->usage & BOX_RENDER_BUFFER_KEEP);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_new_small_batch_gets_min_run(void) {
    // gpu_instance_data/instance_run_exact
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t first = 0;
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_entity_t e = shape_new(world, 3, (float)i, false);
        if (!i) {
            first = e;
        }
    }

    shape_new(world, 4, 0, false);

    /* Five instances take the smallest run, so the second batch starts at MIN_RUN */
    box_render_draws_stats_t stats = frame(world);
    test_uint(batch_get(world, first)->capacity, MIN_RUN);
    test_uint(batch_of(null, 5)->first_instance, 0);
    test_uint(batch_of(null, 1)->first_instance, MIN_RUN);
    test_int(stats.page_slots, 2 * MIN_RUN);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStore_exact_run_grows_moved(void) {
    // gpu_instance_data/instance_run_exact
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t first = 0;
    int32_t i, s;
    for (i = 0; i < 1000; i ++) {
        ecs_entity_t e = shape_new(world, 3, (float)(i % 100), false);
        if (!i) {
            first = e;
        }
    }

    shape_new(world, 4, 0, false);

    /* The second batch takes the run above the 1,000 slots of the first */
    box_render_draws_stats_t stats = frame(world);
    test_uint(batch_of(null, 1000)->first_instance, 0);
    test_uint(batch_of(null, 1)->first_instance, 1000);
    test_int(stats.page_slots, 1000 + MIN_RUN);

    /* One more instance moves the first batch to a run of the next growth step, 1,460 slots,
     * above the second; the GPU copies its 1,000 instances into the new run */
    shape_new(world, 3, 0, false);
    stats = frame(world);
    test_uint(batch_get(world, first)->capacity, 1460);
    test_uint(batch_of(null, 1001)->first_instance, 1000 + MIN_RUN);
    test_uint(batch_of(null, 1)->first_instance, 1000);
    test_int(stats.copies, STREAMS);
    test_int(stats.kept_pages, STREAMS);
    test_int(stats.copy_bytes, 1000 * (int64_t)INSTANCE);
    test_int(stats.page_slots, 1000 + MIN_RUN + 1460);
    for (s = 0; s < STREAMS; s ++) {
        const box_render_buffer_desc_t *desc = created(null, PAGE(0) + (uint32_t)s);
        test_assert(desc != NULL);
        test_uint(desc->size, (uint64_t)(1000 + MIN_RUN + 1460) * stride_of(s));
        test_uint(desc->size % BOX_RENDER_COPY_ALIGN, 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** The spawn scene of docs/gpu-instance-memory.md: 1,000 boxes added in each of 64 frames. */
void InstanceStore_spawn_grows_pages_without_moves(void) {
    // gpu_instance_data/instance_run_exact
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t first = 0;
    int32_t f, i, moves = 0, growths = 0;
    box_render_draws_stats_t stats = {0};
    for (f = 0; f < 64; f ++) {
        for (i = 0; i < 1000; i ++) {
            ecs_entity_t e = box_new(world, (float)i);
            if (!first) {
                first = e;
            }
        }

        stats = frame(world);
        moves += stats.copies / STREAMS;
        growths += stats.kept_pages / STREAMS;

        /* The first frame takes a run of its 1,000 instances; every later one grows the run in
         * place to the growth step at or above its count */
        if (!f) {
            test_uint(batch_get(world, first)->capacity, 1000);
        } else {
            test_uint(batch_get(world, first)->capacity,
                box_draws_grow(0, (uint32_t)(f + 1) * 1000));
        }
    }

    /* The run never moves and the page buffers grow 10 times, as with a first run of the growth
     * step, up to the 84,312 slots of the last step */
    test_int(moves, 0);
    test_int(growths, 10);
    test_int(stats.page_slots, 84312);

    ecs_fini(world);
    box_render_null_free(null);
}
