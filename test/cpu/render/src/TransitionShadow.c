/**
 * @file TransitionShadow.c
 * @brief Tests for the shadow regions of shadow casters a transform transition moves: each
 * frame records the bounds of every pose the resolver can render a caster at, instead of a
 * region over the whole scene.
 *
 * The cases render into the null backend and step the world with a fixed delta, never the wall
 * clock. Rendered transforms are read from the patched copy of the last packet, and the
 * regions are read from the draws, with the light extract that takes them stopped.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"

/** Seconds every frame advances the world, a power of two so the clock is exact */
#define TR_DELTA (0.25f)

/** Create a world that renders into the null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* tr_world(
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
static void tr_fini(
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
static void tr_frame(
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
static ecs_world_t* tr_scene(
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

/** Create a box shape.
 *
 * @param world The world
 * @param parent ChildOf parent, 0 for none
 * @param x Its x position
 * @return The shape
 */
static ecs_entity_t tr_shape(
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
static box_draw_batch_t* tr_batch(
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
static bool tr_slot_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    box_draw_batch_t *b,
    int32_t slot,
    box_render_transform_t *out)
{
    box_render_draws_t *d = box_draws_state(world);
    bool result = false;
    int32_t i;
    box_render_sync(world);
    *out = *box_draws_slot_transform(b, slot);
    if (!b->key.dynamic_transform) {
        goto done;
    }

    uint32_t handle = box_draws_store_handle(box_draws_batch_store(d, b), b->page,
        BOX_RENDER_STREAM_TRANSFORM);
    uint32_t offset = b->first * (uint32_t)sizeof(box_render_transform_t);
    for (i = 0; i < ecs_vec_count(&null->last.uploads) && !result; i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == handle && u->offset == offset) {
            ecs_os_memcpy(out, box_render_packet_data(&null->last,
                u->data + (uint32_t)slot * (uint32_t)sizeof(box_render_transform_t)),
                ECS_SIZEOF(box_render_transform_t));
            result = true;
        }
    }

    test_assert(result);
done:
    return result;
}

/** Read the transform of the shape of an entity as the backend got it in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the transform
 * @return Whether the slot was in a run upload of the last packet
 */
static bool tr_rendered(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_transform_t *out)
{
    return tr_slot_rendered(world, null, tr_batch(world, e),
        ecs_get(world, e, DrawSlot)->slot, out);
}


/** Compute the transform of the target of a shape, from its WorldTransform.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the transform
 */
static void tr_target(
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
static void tr_test_static(
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

/** Stop the light extract, which takes and clears the shadow regions every frame, and have
 * the draws record them, so a case reads the regions of each frame.
 *
 * @param world The world
 * @return box_render_shadow_region_t, the regions recorded since the case cleared them
 */
static ecs_vec_t* tr_regions(
    ecs_world_t *world)
{
    ecs_entity_t lights = ecs_lookup(world, "box.render.RenderExtractLights");
    test_assert(lights != 0);
    ecs_enable(world, lights, false);
    box_render_draws_shadow_record(world, true);
    ecs_vec_t *result = box_render_draws_shadow_regions(world);
    test_assert(result != NULL);

    return result;
}

/** Get the world bounds of the shape of an entity at the transform the backend got in the last
 * packet, with the region math of the shadow regions.
 *
 * @param world The world
 * @param null The backend
 * @param e The entity
 * @param out Receives the bounds
 */
static void tr_rendered_bounds(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e,
    box_render_shadow_region_t *out)
{
    box_render_transform_t t;
    box_draw_batch_t *b = tr_batch(world, e);
    int32_t slot = ecs_get(world, e, DrawSlot)->slot;
    test_assert(tr_rendered(world, null, e, &t));
    test_assert(box_draws_caster(b));
    test_assert(box_draws_shadow_bounds(world, b, &t, &t, box_draws_slot_surface(b, slot), out));
}

/** Check that the regions recorded since they were cleared are each within an x range, which
 * no region over the whole scene is, and that one of them holds bounds.
 *
 * @param regions The regions
 * @param bounds The bounds
 * @param min_x Least x a region may reach
 * @param max_x Largest x a region may reach
 */
static void tr_test_regions(
    const ecs_vec_t *regions,
    const box_render_shadow_region_t *bounds,
    float min_x,
    float max_x)
{
    bool found = false;
    int32_t i, k;
    test_assert(ecs_vec_count(regions) > 0);
    for (i = 0; i < ecs_vec_count(regions); i ++) {
        const box_render_shadow_region_t *r = ecs_vec_get_t(
            regions, box_render_shadow_region_t, i);
        bool inside = r->scene == bounds->scene;
        test_assert(r->min[0] >= min_x);
        test_assert(r->max[0] <= max_x);
        for (k = 0; k < 3; k ++) {
            test_assert(isfinite(r->min[k]));
            test_assert(isfinite(r->max[k]));
            inside = inside && r->min[k] <= bounds->min[k] + 1e-4f &&
                r->max[k] >= bounds->max[k] - 1e-4f;
        }

        found = found || inside;
    }

    test_assert(found);
}

/* A box 1 wide moves from x = 0 to 4 and is sent back to -4 at x = 2, which the game thread
 * never sees: the regions of each frame hold the pose it renders and stay within -4.5 to 4.5,
 * the frame it ends in holds the pose of the frame before, and a start once it is idle covers
 * only its new range */
void TransitionShadow_interrupted(void) {
    // engine_core/transition
    box_render_null_t *null;
    box_render_shadow_region_t bounds, last;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t e = tr_shape(world, 0, 0);
    box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){ .duration = 1.0f });
    tr_frame(world);

    ecs_set(world, e, Position, {4, 0, 0});
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, e, &bounds);
        tr_test_regions(regions, &bounds, -0.6f, 4.6f);
    }

    ecs_set(world, e, Position, {-4, 0, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, e, &bounds);
        tr_test_regions(regions, &bounds, -4.6f, 4.6f);
        last = bounds;
    }

    test_assert(last.min[0] < 2.0f);
    test_assert(last.max[0] > -4.0f);
    ecs_vec_clear(regions);
    tr_frame(world);
    test_assert(!ecs_has_id(world, e, TransitionTransform));
    tr_test_regions(regions, &last, -4.6f, 4.6f);

    ecs_set(world, e, Position, {-2, 0, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, e, &bounds);
        tr_test_regions(regions, &bounds, -4.6f, -1.4f);
    }

    tr_frame(world);
    tr_test_static(world, null, e);
    tr_fini(world, null);
}

/** Check the regions of a child 3 units out along x of a parent that turns about y and is sent
 * back halfway, while the child moves up on its own, for a ChildOf or a Parent hierarchy.
 *
 * @param non_fragmenting Whether the child has a Parent component instead of a ChildOf pair
 */
static void tr_shadow_child(
    bool non_fragmenting)
{
    box_render_null_t *null;
    box_render_shadow_region_t bounds;
    ecs_world_t *world = tr_scene(&null);
    ecs_vec_t *regions = tr_regions(world);
    ecs_entity_t parent = tr_shape(world, 0, 0);
    ecs_entity_t child;
    int32_t i;
    if (non_fragmenting) {
        child = ecs_new(world);
        ecs_set(world, child, EcsParent, {parent});
        ecs_set(world, child, Box, {1, 1, 1});
    } else {
        child = tr_shape(world, parent, 0);
    }

    ecs_set(world, child, Position, {3, 0, 0});
    ecs_set(world, parent, Rotation, {0, 0, 0});
    box_transition_set(world, parent, ecs_id(Rotation), &(TransitionSpec){
        .duration = 1.0f, .timing_function = EaseInOut });
    box_transition_set(world, child, ecs_id(Position), &(TransitionSpec){ .duration = 0.5f });
    tr_frame(world);

    ecs_set(world, parent, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, child, Position, {3, 2, 0});
    for (i = 0; i < 2; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, child, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
        tr_rendered_bounds(world, null, parent, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
    }

    ecs_set(world, parent, Rotation, {0, -0.5f, 0});
    for (i = 0; i < 4; i ++) {
        ecs_vec_clear(regions);
        tr_frame(world);
        tr_rendered_bounds(world, null, child, &bounds);
        tr_test_regions(regions, &bounds, -5.5f, 5.5f);
    }

    tr_frame(world);
    tr_test_static(world, null, parent);
    tr_test_static(world, null, child);
    tr_fini(world, null);
}

void TransitionShadow_nested_childof(void) {
    // engine_core/transition
    tr_shadow_child(false);
}

void TransitionShadow_nested_parent(void) {
    // engine_core/transition
    tr_shadow_child(true);
}
