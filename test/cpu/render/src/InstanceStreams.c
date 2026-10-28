/**
 * @file InstanceStreams.c
 * @brief Tests for the transform and shading streams of the instance pages: which stream a
 * dynamic tag uploads every frame and which stream a static change scatters.
 *
 * Every case renders with the null backend and reads the byte counters of the extract, which
 * count the run uploads and the scatter segments of each stream.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

/** Shapes of the cases with many shapes */
#define COUNT (10)

/** Bytes of one instance in the transform stream */
#define TRANSFORM ((int64_t)sizeof(box_render_transform_t))

/** Bytes of one instance in the shading stream */
#define SHADING ((int64_t)sizeof(box_render_shading_t))

/** Bytes of one instance in the surface stream */
#define SURFACE ((int64_t)sizeof(box_render_surface_t))

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

/** Add a box.
 *
 * @param world The world
 * @param parent Parent of the box, 0 for none
 * @param x Its x position
 * @return The box
 */
static ecs_entity_t box_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {200, 100, 50});

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

/** Get the bytes of a scatter segment as the extract aligns it.
 *
 * @param entries Entries of the segment
 * @param stride Bytes of one instance in the stream of the segment
 * @return The bytes
 */
static int64_t segment_size(
    int64_t entries,
    int64_t stride)
{
    int64_t size = BOX_RENDER_SCATTER_HEADER * (int64_t)sizeof(uint32_t) +
        entries * ((int64_t)sizeof(uint32_t) + stride);

    return (size + BOX_RENDER_SCATTER_ALIGN - 1) / BOX_RENDER_SCATTER_ALIGN *
        BOX_RENDER_SCATTER_ALIGN;
}

/** Check the byte counters of a frame.
 *
 * @param stats The counters
 * @param transform Bytes expected in the transform streams
 * @param shading Bytes expected in the shading streams
 */
static void test_bytes(
    const box_render_draws_stats_t *stats,
    int64_t transform,
    int64_t shading)
{
    test_int(stats->transform_bytes, transform);
    test_int(stats->shading_bytes, shading);
    test_int(stats->surface_bytes, 0);
    test_int(stats->upload_bytes, transform + shading);
}

/** Get the shading of the shape of an entity as the extract keeps it.
 *
 * @param world The world
 * @param e The entity
 * @return The shading of its slot
 */
static const box_render_shading_t* shading_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = ecs_system_get(world, system);
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(s != NULL && s->ctx != NULL);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_slot_shading(box_draws_batch(s->ctx, slot->batch), slot->slot);
}

/** Find the clip index the last packet scattered into a shading stream.
 *
 * @param null The backend
 * @return The ids[0] word of the first entry of the shading scatter, -1 without one
 */
static int64_t scattered_clip(
    const box_render_null_t *null)
{
    const box_render_upload_t *changes = NULL;
    int64_t result = -1;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_CHANGES) {
            changes = u;
        }
    }

    for (i = 0; changes && i < ecs_vec_count(&null->last.scatters); i ++) {
        const box_render_scatter_t *sc = ecs_vec_get_t(
            &null->last.scatters, box_render_scatter_t, i);
        uint32_t stream = (sc->buffer - BOX_RENDER_BUFFER_FIRST) % BOX_RENDER_INSTANCE_STREAMS;
        if (stream != BOX_RENDER_STREAM_SHADING) {
            continue;
        }

        const uint32_t *words = box_render_packet_data(&null->last, changes->data + sc->offset);
        test_uint(words[1], (uint32_t)(SHADING / 4));
        result = words[BOX_RENDER_SCATTER_HEADER + 1 + 16];
    }

    return result;
}

void InstanceStreams_dynamic_transform_uploads_transforms(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        ecs_add(world, box_new(world, 0, (float)i), DynamicTransform);
    }

    box_render_draws_stats_t stats = frame(world);
    test_int(stats.dynamic, COUNT);
    test_int(stats.transform_bytes, COUNT * TRANSFORM);
    test_int(stats.shading_bytes, COUNT * SHADING);
    test_int(stats.surface_bytes, COUNT * SURFACE);
    for (i = 0; i < 3; i ++) {
        stats = frame(world);
        test_bytes(&stats, COUNT * TRANSFORM, 0);
        test_int(stats.scatters, 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_dynamic_material_uploads_shading(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        ecs_add_id(world, box_new(world, 0, (float)i), DynamicMaterial);
    }

    frame(world);
    for (i = 0; i < 3; i ++) {
        box_render_draws_stats_t stats = frame(world);
        test_int(stats.dynamic, COUNT);
        test_bytes(&stats, 0, COUNT * SHADING);
        test_int(stats.scatters, 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_both_tags_upload_both(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        ecs_entity_t e = box_new(world, 0, (float)i);
        ecs_add(world, e, DynamicTransform);
        ecs_add_id(world, e, DynamicMaterial);
    }

    frame(world);
    for (i = 0; i < 3; i ++) {
        box_render_draws_stats_t stats = frame(world);
        test_int(stats.dynamic, COUNT);
        test_bytes(&stats, COUNT * TRANSFORM, COUNT * SHADING);
        test_int(stats.scatters, 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_dynamic_material_parent_uploads_shading(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 1, 0});
    ecs_add_id(world, parent, DynamicMaterial);
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        box_new(world, parent, (float)i);
    }

    frame(world);
    for (i = 0; i < 3; i ++) {
        box_render_draws_stats_t stats = frame(world);
        test_int(stats.dynamic, COUNT);
        test_bytes(&stats, 0, COUNT * SHADING);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_static_change_scatters_its_stream(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t boxes[COUNT];
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        boxes[i] = box_new(world, 0, (float)i);
    }

    frame(world);
    box_render_draws_stats_t stats = frame(world);
    test_bytes(&stats, 0, 0);

    ecs_set(world, boxes[3], Position, {3, 2, 0});
    stats = frame(world);
    test_int(stats.scatters, 1);
    test_int(stats.scattered, 1);
    test_bytes(&stats, segment_size(1, TRANSFORM), 0);

    stats = frame(world);
    test_bytes(&stats, 0, 0);

    ecs_set(world, boxes[5], Rgb, {0, 255, 0});
    stats = frame(world);
    test_int(stats.scatters, 1);
    test_int(stats.scattered, 1);
    test_bytes(&stats, 0, segment_size(1, SHADING));

    stats = frame(world);
    test_bytes(&stats, 0, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_reparent_writes_both(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = ecs_new(world), b = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, b, Position, {0, 3, 0});
    ecs_entity_t boxes[COUNT];
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        boxes[i] = box_new(world, a, (float)i);
    }

    frame(world);
    box_render_draws_stats_t stats = frame(world);
    test_bytes(&stats, 0, 0);

    ecs_add_pair(world, boxes[4], EcsChildOf, b);
    stats = frame(world);
    test_int(stats.scatters, 2);
    test_int(stats.scattered, 2);
    test_bytes(&stats, segment_size(1, TRANSFORM), segment_size(1, SHADING));

    stats = frame(world);
    test_bytes(&stats, 0, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_scene_change_writes_both(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t boxes[COUNT];
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        boxes[i] = box_new(world, 0, (float)i);
    }

    frame(world);
    box_render_draws_stats_t stats = frame(world);
    test_bytes(&stats, 0, 0);
    test_int(stats.batches, 1);

    ecs_add_pair(world, boxes[2], EcsChildOf, scene);
    stats = frame(world);
    test_int(stats.batches, 2);
    test_assert(stats.transform_bytes >= TRANSFORM);
    test_assert(stats.shading_bytes >= SHADING);

    stats = frame(world);
    test_bytes(&stats, 0, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_toggle_dynamic_transform(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = box_new(world, 0, 0);
    int32_t i;
    frame(world);
    box_render_draws_stats_t stats = frame(world);
    test_bytes(&stats, 0, 0);

    ecs_add(world, e, DynamicTransform);
    stats = frame(world);
    test_int(stats.dynamic, 1);
    test_int(stats.transform_bytes, TRANSFORM);
    test_int(stats.shading_bytes, SHADING);
    test_int(stats.surface_bytes, SURFACE);
    for (i = 0; i < 3; i ++) {
        stats = frame(world);
        test_bytes(&stats, TRANSFORM, 0);
    }

    ecs_remove(world, e, DynamicTransform);
    stats = frame(world);
    test_int(stats.dynamic, 0);
    test_int(stats.transform_bytes, TRANSFORM);
    test_int(stats.shading_bytes, SHADING);
    for (i = 0; i < 3; i ++) {
        stats = frame(world);
        test_bytes(&stats, 0, 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_dynamic_shape2d_keeps_clip(void) {
    // gpu_instance_data/instance_hot_cold
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t first = ecs_new(world), second = ecs_new(world);
    ecs_set(world, first, Position, {0, 0, 0});
    ecs_set(world, first, Rect, {20, 20});
    ecs_add_id(world, first, ClipChildren);
    ecs_set(world, second, Position, {30, 0, 0});
    ecs_set(world, second, Rect, {20, 20});
    ecs_add_id(world, second, ClipChildren);
    ecs_entity_t children[COUNT];
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        children[i] = ecs_new_w_pair(world, EcsChildOf, first);
        ecs_add(world, children[i], DynamicTransform);
        ecs_set(world, children[i], Position, {0, (float)i, 0});
        ecs_set(world, children[i], Rect, {10, 10});
    }

    ecs_entity_t child = children[0];

    frame(world);
    frame(world);
    uint32_t clip = shading_of(world, child)->ids[0];
    test_assert(clip != 0);
    test_int(ecs_get(world, child, DrawSlot)->clip, (int32_t)clip);

    for (i = 0; i < 3; i ++) {
        ecs_set(world, child, Position, {(float)i, 1, 0});
        box_render_draws_stats_t stats = frame(world);
        test_int(stats.shading_bytes, 0);
        test_assert(stats.transform_bytes >= TRANSFORM);
        test_uint(shading_of(world, child)->ids[0], clip);
    }

    ecs_add_pair(world, child, EcsChildOf, second);
    box_render_draws_stats_t stats = frame(world);
    uint32_t moved = shading_of(world, child)->ids[0];
    test_assert(moved != 0);
    test_assert(moved != clip);
    test_int(ecs_get(world, child, DrawSlot)->clip, (int32_t)moved);
    test_int(stats.shading_bytes, segment_size(1, SHADING));
    test_int(scattered_clip(null), moved);

    stats = frame(world);
    test_int(stats.shading_bytes, 0);
    test_uint(shading_of(world, child)->ids[0], moved);

    ecs_fini(world);
    box_render_null_free(null);
}
