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

/** Bytes of one mesh instance in the shading stream */
#define SHADING ((int64_t)sizeof(box_render_mesh_shading_t))

/** Bytes of one 2D instance in the shading stream */
#define SHADING_2D ((int64_t)sizeof(box_render_shading_t))

/** Bytes of one record of the material table */
#define MATERIAL ((int64_t)sizeof(box_render_material_t))

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
        test_uint(words[1], (uint32_t)(SHADING_2D / 4));
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
    test_int(stats.scatters, 0);
    test_bytes(&stats, 0, 0);
    test_int(stats.material_bytes, MATERIAL);

    ecs_set(world, boxes[5], Tint, {0, 255, 0, 128});
    stats = frame(world);
    test_int(stats.scatters, 1);
    test_int(stats.scattered, 1);
    test_bytes(&stats, 0, segment_size(1, SHADING));
    test_int(stats.material_bytes, 0);

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
    test_int(stats.shading_bytes, segment_size(1, SHADING_2D));
    test_int(scattered_clip(null), moved);

    stats = frame(world);
    test_int(stats.shading_bytes, 0);
    test_uint(shading_of(world, child)->ids[0], moved);

    ecs_fini(world);
    box_render_null_free(null);
}

void InstanceStreams_shape2d_ranks_follow_order(void) {
    // ui/shapes_2d
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Rect, {100, 100});
    ecs_entity_t children[COUNT];
    uint32_t ranks[COUNT];
    int32_t i;
    for (i = 0; i < COUNT; i ++) {
        children[i] = ecs_new_w_pair(world, EcsChildOf, panel);
        ecs_set(world, children[i], Position, {(float)i, 0, 0});
        ecs_set(world, children[i], Rect, {10, 10});
    }

    frame(world);
    frame(world);
    test_uint(shading_of(world, panel)->ids[1], 0);
    for (i = 0; i < COUNT; i ++) {
        ranks[i] = shading_of(world, children[i])->ids[1];
        test_uint(ecs_get(world, children[i], DrawSlot)->rank, ranks[i]);
        test_assert(ranks[i] > (i ? ranks[i - 1] : 0));
    }

    ecs_delete(world, children[2]);
    box_render_draws_stats_t stats = frame(world);
    test_int(stats.shading_bytes, segment_size(1, SHADING_2D));
    for (i = 3; i < COUNT; i ++) {
        test_uint(shading_of(world, children[i])->ids[1], ranks[i]);
    }

    ecs_entity_t last = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, last, Position, {0, 0, 1});
    ecs_set(world, last, Rect, {10, 10});
    frame(world);
    test_assert(shading_of(world, last)->ids[1] > ranks[COUNT - 1]);
    for (i = 3; i < COUNT; i ++) {
        test_uint(shading_of(world, children[i])->ids[1], ranks[i]);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** A shape deleted while disabled frees its slot, so the batch keeps no dead entity: the last
 * slot, which moves into a slot freed later, is a live entity.
 */
void InstanceStreams_disabled_delete_frees_slot(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t boxes[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        boxes[i] = box_new(world, 0, (float)i);
    }

    frame(world);
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    box_render_draws_t *d = ecs_system_get(world, system)->ctx;
    const DrawSlot *slot = ecs_get(world, boxes[0], DrawSlot);
    box_draw_batch_t *b = box_draws_batch(d, slot->batch);
    test_int(ecs_vec_count(&b->entities), 3);
    test_int(ecs_get(world, boxes[2], DrawSlot)->batch, slot->batch);

    ecs_enable(world, boxes[2], false);
    frame(world);
    ecs_delete(world, boxes[2]);
    test_int(ecs_vec_count(&b->entities), 2);

    ecs_delete(world, boxes[0]);
    frame(world);
    test_int(ecs_vec_count(&b->entities), 1);
    test_uint(*ecs_vec_get_t(&b->entities, ecs_entity_t, 0), boxes[1]);
    test_int(ecs_get(world, boxes[1], DrawSlot)->slot, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Get the x translation of the slot of a shape as the extract keeps it.
 *
 * @param world The world
 * @param e The entity
 * @return The x of the translation of its transform
 */
static float slot_x(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = ecs_system_get(world, system);
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(s != NULL && s->ctx != NULL);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_slot_transform(box_draws_batch(s->ctx, slot->batch), slot->slot)->rows[0][3];
}

/** A disabled mesh shape, 2D shape and text leave their batches, and take their slots again
 * once enabled.
 */
void InstanceStreams_disabled_shapes_leave_batches(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t mesh = box_new(world, 0, 0);
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {10, 10});
    ecs_entity_t text = ecs_new(world);
    ecs_set(world, text, Position, {0, 1, 0});
    ecs_set(world, text, Text, {
        .value = "disabled", .size = 16, .font = ecs_lookup(world, "box.text.DefaultFont")
    });
    frame(world);
    frame(world);
    box_render_draws_stats_t stats = frame(world);
    int32_t all = stats.instances + stats.dynamic;
    test_assert(all > 2);
    test_assert(ecs_get(world, mesh, DrawSlot)->batch >= 0);
    test_assert(ecs_get(world, rect, DrawSlot)->batch >= 0);
    test_assert(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots) > 0);

    ecs_enable(world, mesh, false);
    ecs_enable(world, rect, false);
    ecs_enable(world, text, false);
    stats = frame(world);
    test_int(stats.instances + stats.dynamic, 0);
    test_int(ecs_get(world, mesh, DrawSlot)->batch, -1);
    test_int(ecs_get(world, rect, DrawSlot)->batch, -1);
    test_int(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots), 0);

    ecs_enable(world, mesh, true);
    ecs_enable(world, rect, true);
    ecs_enable(world, text, true);
    stats = frame(world);
    test_int(stats.instances + stats.dynamic, all);
    test_assert(ecs_get(world, mesh, DrawSlot)->batch >= 0);
    test_assert(ecs_get(world, rect, DrawSlot)->batch >= 0);
    test_assert(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots) > 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A shape moved while disabled draws where it is once enabled, static or dynamic. */
void InstanceStreams_enabled_shape_takes_current_transform(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t still = box_new(world, 0, 0);
    ecs_entity_t dynamic = box_new(world, 0, 0);
    ecs_add(world, dynamic, DynamicTransform);
    frame(world);
    frame(world);
    test_flt(slot_x(world, still), 0);
    test_flt(slot_x(world, dynamic), 0);

    ecs_enable(world, still, false);
    ecs_enable(world, dynamic, false);
    frame(world);
    ecs_set(world, still, Position, {3, 0, 0});
    ecs_set(world, dynamic, Position, {-3, 0, 0});
    frame(world);
    test_int(ecs_get(world, still, DrawSlot)->batch, -1);
    test_int(ecs_get(world, dynamic, DrawSlot)->batch, -1);

    ecs_enable(world, still, true);
    ecs_enable(world, dynamic, true);
    frame(world);
    test_flt(slot_x(world, still), 3);
    test_flt(slot_x(world, dynamic), -3);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A shape set up while disabled gets no slot, and gets one with its transform once enabled. */
void InstanceStreams_created_disabled_draws_once_enabled(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_enable(world, e, false);
    ecs_set(world, e, Position, {2, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Rgb, {200, 100, 50});
    box_render_draws_stats_t stats = frame(world);
    stats = frame(world);
    test_int(stats.instances + stats.dynamic, 0);
    test_int(ecs_get(world, e, DrawSlot)->batch, -1);

    ecs_enable(world, e, true);
    stats = frame(world);
    test_int(stats.instances + stats.dynamic, 1);
    test_flt(slot_x(world, e), 2);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Create a text with the default font.
 *
 * @param world The world
 * @param e The entity, which may be disabled
 * @param value The string
 */
static void text_set(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *value)
{
    ecs_set(world, e, Position, {0, 1, 0});
    ecs_set(world, e, Text, {
        .value = (char*)value, .size = 16, .font = ecs_lookup(world, "box.text.DefaultFont")
    });
}

/** A text set up while disabled is laid out, and draws its glyphs once enabled. */
void InstanceStreams_text_created_disabled(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    text_set(world, ecs_new(world), "warm");
    frame(world);
    frame(world);

    ecs_entity_t text = ecs_new(world);
    ecs_enable(world, text, false);
    text_set(world, text, "disabled");
    frame(world);
    frame(world);
    test_int(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots), 0);

    ecs_enable(world, text, true);
    frame(world);
    frame(world);
    test_int(ecs_vec_count(&ecs_get(world, text, TextGlyphs)->glyphs), 8);
    test_int(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots), 8);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A text whose value changes while disabled draws the new string once enabled. */
void InstanceStreams_text_changed_while_disabled(void) {
    // renderer/draws
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t text = ecs_new(world);
    text_set(world, text, "disabled");
    frame(world);
    frame(world);
    test_int(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots), 8);

    ecs_enable(world, text, false);
    frame(world);
    text_set(world, text, "abc");
    frame(world);
    test_int(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots), 0);

    ecs_enable(world, text, true);
    frame(world);
    frame(world);
    test_int(ecs_vec_count(&ecs_get(world, text, TextGlyphs)->glyphs), 3);
    test_int(ecs_vec_count(&ecs_get(world, text, TextSlots)->slots), 3);

    ecs_fini(world);
    box_render_null_free(null);
}
