/**
 * @file DrawRangeKey.c
 * @brief Tests for how the DrawRange of a shape is resolved into its batch: its own, inherited
 * from a prefab or from the nearest ancestor, written into the key of its batch and written
 * again when it changes.
 *
 * Every case renders with the null backend and reads the batches and slots of the extract.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"

/** Create a world that renders with a null backend and has a view.
 *
 * @return The world
 */
static ecs_world_t* world_new(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
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
 * @return The box
 */
static ecs_entity_t box_new(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});

    return e;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Get the batch of a shape.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* batch_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(box_draws_state(world), slot->batch);
}

/** Check the range in the batch key of a shape.
 *
 * @param world The world
 * @param e The shape
 * @param near Expected near
 * @param far Expected far
 * @param fade Expected fade
 */
static void range_check(
    ecs_world_t *world,
    ecs_entity_t e,
    float near,
    float far,
    float fade)
{
    box_draw_batch_t *b = batch_of(world, e);
    test_flt(b->key.range[0], near);
    test_flt(b->key.range[1], far);
    test_flt(b->key.range[2], fade);
}

void DrawRangeKey_none(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t e = box_new(world, 0);
    frame(world);
    range_check(world, e, 0, 0, 0);
    float range[3] = {1, 1, 1};
    test_bool(box_draws_range(world, e, range), false);
    test_flt(range[0], 0);
    test_flt(range[1], 0);
    test_flt(range[2], 0);
    ecs_fini(world);
}

void DrawRangeKey_own(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t e = box_new(world, 0);
    ecs_set(world, e, DrawRange, {.near_ = 2, .far_ = 30, .fade = 1.5f});
    frame(world);
    range_check(world, e, 2, 30, 1.5f);
    ecs_fini(world);
}

void DrawRangeKey_invalid_values_ignored(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t e = box_new(world, 0);
    ecs_set(world, e, DrawRange, {.near_ = -2, .far_ = 0, .fade = 3});
    frame(world);
    range_check(world, e, 0, 0, 0);
    ecs_fini(world);
}

void DrawRangeKey_from_parent(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, DrawRange, {.far_ = 20, .fade = 2});
    ecs_entity_t mid = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_entity_t e = box_new(world, mid);
    frame(world);
    range_check(world, e, 0, 20, 2);
    ecs_fini(world);
}

void DrawRangeKey_nearest_ancestor(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, DrawRange, {.far_ = 20});
    ecs_entity_t mid = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, mid, DrawRange, {.far_ = 10});
    ecs_entity_t e = box_new(world, mid);
    frame(world);
    range_check(world, e, 0, 10, 0);
    ecs_fini(world);
}

void DrawRangeKey_from_prefab(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, DrawRange, {.far_ = 25, .fade = 3});
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, b, Position, {2, 0, 0});
    frame(world);
    range_check(world, a, 0, 25, 3);
    range_check(world, b, 0, 25, 3);
    test_assert(batch_of(world, a) == batch_of(world, b));

    ecs_set(world, prefab, DrawRange, {.far_ = 40, .fade = 3});
    frame(world);
    range_check(world, a, 0, 40, 3);
    range_check(world, b, 0, 40, 3);
    ecs_fini(world);
}

void DrawRangeKey_prefab_child(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, DrawRange, {.far_ = 15});
    ecs_entity_t part = ecs_new_w_pair(world, EcsChildOf, prefab);
    ecs_add_id(world, part, EcsPrefab);
    ecs_set(world, part, Position, {0, 0, 0});
    ecs_set(world, part, Box, {1, 1, 1});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {0, 0, 0});
    frame(world);
    ecs_iter_t it = ecs_children(world, inst);
    ecs_entity_t child = 0;
    while (ecs_children_next(&it)) {
        if (it.count) {
            child = it.entities[0];
        }
    }

    test_assert(child != 0);
    range_check(world, child, 0, 15, 0);
    ecs_fini(world);
}

void DrawRangeKey_different_ranges_split_batches(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t a = box_new(world, 0);
    ecs_entity_t b = box_new(world, 0);
    ecs_entity_t c = box_new(world, 0);
    ecs_set(world, a, DrawRange, {.far_ = 10});
    ecs_set(world, b, DrawRange, {.far_ = 20});
    frame(world);
    test_assert(batch_of(world, a) != batch_of(world, b));
    test_assert(batch_of(world, a) != batch_of(world, c));
    test_assert(batch_of(world, b) != batch_of(world, c));

    ecs_set(world, b, DrawRange, {.far_ = 10});
    frame(world);
    test_assert(batch_of(world, a) == batch_of(world, b));
    ecs_fini(world);
}

void DrawRangeKey_removed(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, DrawRange, {.far_ = 20});
    ecs_entity_t e = box_new(world, root);
    frame(world);
    range_check(world, e, 0, 20, 0);
    ecs_remove(world, root, DrawRange);
    frame(world);
    range_check(world, e, 0, 0, 0);
    ecs_fini(world);
}

void DrawRangeKey_kept_by_tint(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, DrawRange, {.far_ = 20, .fade = 1});
    ecs_entity_t e = box_new(world, root);
    frame(world);
    ecs_set(world, root, Tint, {255, 0, 0, 128});
    frame(world);
    range_check(world, e, 0, 20, 1);
    ecs_fini(world);
}

void DrawRangeKey_packet_batch(void) {
    ecs_world_t *world = world_new();
    ecs_entity_t e = box_new(world, 0);
    ecs_set(world, e, DrawRange, {.near_ = 1, .far_ = 9, .fade = 0.5f});
    frame(world);
    box_render_batch_t out = {0};
    out.pipeline = BOX_RENDER_SHADER_MESH;
    test_bool(box_render_batch_ranged(&out), false);
    out.range[1] = 9;
    test_bool(box_render_batch_ranged(&out), true);
    out.pipeline = BOX_RENDER_SHADER_SHAPE2D;
    test_bool(box_render_batch_ranged(&out), false);
    range_check(world, e, 1, 9, 0.5f);
    ecs_fini(world);
}
