/**
 * @file transition_test.h
 * @brief Helpers the transition suites share: a world that renders into the null backend,
 * stepped with a fixed delta, and the reads of the slots the backend got in the last packet.
 */

#ifndef TRANSITION_TEST_H
#define TRANSITION_TEST_H

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

/** Seconds every frame advances the world, a power of two so the clock is exact */
#define TR_DELTA (0.25f)

/** Create a world that renders into the null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static inline ecs_world_t* tr_world(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Delete a world and its null backend.
 *
 * @param world The world
 * @param null The backend
 */
static inline void tr_fini(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_fini(world);
    box_render_null_free(null);
}

/** Run one frame and wait until the render thread resolved its packet.
 *
 * @param world The world
 */
static inline void tr_frame(
    ecs_world_t *world)
{
    ecs_progress(world, TR_DELTA);
    box_render_sync(world);
}

/** Create a world that renders into the null backend with a view, so the batches are sent.
 *
 * @param null Receives the backend
 * @return The world
 */
static inline ecs_world_t* tr_scene(
    box_render_null_t **null)
{
    ecs_world_t *world = tr_world(null);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 20});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Read the rendered Rgb of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The value the resolver computed for the last packet
 */
static inline Rgb tr_rgb(
    ecs_world_t *world,
    ecs_entity_t e)
{
    Rgb result = {0};
    box_render_sync(world);
    test_assert(box_render_transition_test_value(world, e, ecs_id(Rgb), &result));

    return result;
}

/** Count the records of an op in the last packet.
 *
 * @param null The backend
 * @param op The op
 * @return The records
 */
static inline int32_t tr_records(
    const box_render_null_t *null,
    box_render_transition_op_t op)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.transitions); i ++) {
        const box_render_transition_record_t *r = ecs_vec_get_t(
            &null->last.transitions, box_render_transition_record_t, i);
        result += r->op == op;
    }

    return result;
}

/** Count the records of an op in the last packet, once the render thread resolved it.
 *
 * @param world The world
 * @param null The backend
 * @param op The op
 * @return The records
 */
static inline int32_t tr_ops(
    ecs_world_t *world,
    const box_render_null_t *null,
    box_render_transition_op_t op)
{
    box_render_sync(world);

    return tr_records(null, op);
}

/** Create a box shape.
 *
 * @param world The world
 * @param parent ChildOf parent, 0 for none
 * @param x Its x position
 * @return The shape
 */
static inline ecs_entity_t tr_shape(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Box, {1, 2, 3});
    ecs_set(world, e, Rgb, {200, 100, 50});

    return e;
}

/** Get the batch of the shape of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The batch
 */
static inline box_draw_batch_t* tr_batch(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_draws_t *d = box_draws_state(world);
    const DrawSlot *ds = ecs_get(world, e, DrawSlot);
    test_assert(d != NULL);
    test_assert(ds != NULL);
    test_assert(ds->batch >= 0);

    return box_draws_batch(d, ds->batch);
}

/** Read a slot of a stream of a batch from the run upload of the last packet: the upload of
 * the store handle of the stream at the first slot of the batch, which the resolver patched.
 * A batch that is not dynamic for the stream has no run upload, and out is left as it is.
 *
 * @param world The world
 * @param null The backend
 * @param b The batch
 * @param slot The slot
 * @param stream BOX_RENDER_STREAM_TRANSFORM, which is dynamic for a dynamic_transform batch,
 *        or BOX_RENDER_STREAM_SHADING, which is dynamic for a dynamic_material batch
 * @param stride Bytes of a slot of the stream
 * @param out Receives the stride bytes of the slot
 * @return Whether the slot was in a run upload of the last packet
 */
static inline bool tr_slot_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    box_draw_batch_t *b,
    int32_t slot,
    int32_t stream,
    uint32_t stride,
    void *out)
{
    box_render_draws_t *d = box_draws_state(world);
    bool dynamic = stream == BOX_RENDER_STREAM_TRANSFORM
        ? b->key.dynamic_transform : b->key.dynamic_material;
    bool result = false;
    int32_t i;
    box_render_sync(world);
    if (!dynamic) {
        goto done;
    }

    uint32_t handle = box_draws_store_handle(box_draws_batch_store(d, b), b->page, stream);
    uint32_t offset = b->first * stride;
    for (i = 0; i < ecs_vec_count(&null->last.uploads) && !result; i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == handle && u->offset == offset) {
            ecs_os_memcpy(out, box_render_packet_data(&null->last,
                u->data + (uint32_t)slot * stride), (ecs_size_t)stride);
            result = true;
        }
    }

    test_assert(result);
done:
    return result;
}

/** Read a transform slot of a batch as the backend got it in the last packet: the patched
 * bytes of the run upload of a dynamic_transform batch, else the static slot the game thread
 * keeps, which the backend has.
 *
 * @param world The world
 * @param null The backend
 * @param b The batch
 * @param slot The slot
 * @param out Receives the transform
 * @return Whether the slot was in a run upload of the last packet
 */
static inline bool tr_transform_slot(
    ecs_world_t *world,
    const box_render_null_t *null,
    box_draw_batch_t *b,
    int32_t slot,
    box_render_transform_t *out)
{
    box_render_sync(world);
    *out = *box_draws_slot_transform(b, slot);

    return tr_slot_rendered(world, null, b, slot, BOX_RENDER_STREAM_TRANSFORM,
        (uint32_t)sizeof(box_render_transform_t), out);
}

/** Read the transform of the shape of an entity as the backend got it in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the transform
 * @return Whether the slot was in a run upload of the last packet
 */
static inline bool tr_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_transform_t *out)
{
    return tr_transform_slot(world, null, tr_batch(world, e),
        ecs_get(world, e, DrawSlot)->slot, out);
}

/** Compute the transform of the target of a shape, from its WorldTransform.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the transform
 */
static inline void tr_target(
    ecs_world_t *world,
    ecs_entity_t e,
    box_render_transform_t *out)
{
    box_draws_transform(out, ecs_get(world, e, WorldTransform), ecs_get(world, e, ShapeMesh));
}

/** Check that the shape of an entity is back in a static batch and its slot holds its target,
 * byte for byte.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 */
static inline void tr_test_static(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    box_render_transform_t rendered, expected;
    test_assert(!tr_rendered(world, null, e, &rendered));
    test_assert(!ecs_has_id(world, e, TransitionTransform));
    tr_target(world, e, &expected);
    test_assert(!ecs_os_memcmp_t(&rendered, &expected, box_render_transform_t));
}

#endif
