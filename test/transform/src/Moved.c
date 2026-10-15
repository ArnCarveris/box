/**
 * @file Moved.c
 * @brief box_transform_on_moved: which entities a TransformStatic run passes to listeners.
 */

#include <box.h>
#include <bake_test.h>

/** What a listener received */
typedef struct {
    ecs_entity_t entities[256]; /**< Entities passed, in order */
    int32_t count;              /**< Entities passed */
    int32_t calls;              /**< Calls of the listener */
    int32_t freed;              /**< Calls of the free function */
} moved_log_t;

/** Record the entities passed to a listener.
 *
 * @param world The world
 * @param entities The entities
 * @param count Number of entities
 * @param ctx The log
 */
static void OnMoved(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx)
{
    moved_log_t *log = ctx;
    (void)world;
    log->calls ++;
    for (int32_t i = 0; i < count && log->count < 256; i ++) {
        log->entities[log->count ++] = entities[i];
    }
}

/** Count the calls of the free function of a listener.
 *
 * @param ctx The log
 */
static void OnMovedFree(
    void *ctx)
{
    moved_log_t *log = ctx;
    log->freed ++;
}

/** Write the WorldTransform of the ManualTransform entity in the context once, at x = 10.
 *
 * @param it The iterator of the system
 */
static void LateManualWrite(
    ecs_iter_t *it)
{
    ecs_entity_t *e = it->ctx;
    if (*e) {
        ecs_set(it->world, *e, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
        *e = 0;
    }
}

void Moved_position_set_passes_subtree(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {5.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    log = (moved_log_t){0};
    ecs_set(world, root, Position, {2.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_int(log.calls, 1);
    bool found_root = false, found_child = false, found_grandchild = false, found_other = false;
    for (int32_t i = 0; i < log.count; i ++) {
        found_root |= log.entities[i] == root;
        found_child |= log.entities[i] == child;
        found_grandchild |= log.entities[i] == grandchild;
        found_other |= log.entities[i] == other;
    }

    test_bool(found_root, true);
    test_bool(found_child, true);
    test_bool(found_grandchild, true);
    test_bool(found_other, false);
    test_flt(ecs_get(world, grandchild, WorldTransform)->m[3][0], 4.0f);

    ecs_fini(world);
}

void Moved_parent_component_passes_subtree(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_parent(world, root, NULL);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t grandchild = ecs_new_w_parent(world, child, NULL);
    ecs_set(world, grandchild, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    log = (moved_log_t){0};
    ecs_set(world, root, Position, {2.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_int(log.calls, 1);
    bool found_root = false, found_child = false, found_grandchild = false;
    for (int32_t i = 0; i < log.count; i ++) {
        found_root |= log.entities[i] == root;
        found_child |= log.entities[i] == child;
        found_grandchild |= log.entities[i] == grandchild;
    }

    test_bool(found_root, true);
    test_bool(found_child, true);
    test_bool(found_grandchild, true);

    ecs_fini(world);
}

void Moved_idle_frame_passes_nothing(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_int(log.calls, 1);
    log = (moved_log_t){0};
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_int(log.calls, 0);
    test_int(log.count, 0);

    ecs_fini(world);
}

void Moved_dynamic_entity_not_passed(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    log = (moved_log_t){0};
    ecs_set(world, e, Position, {2.0f, 0.0f, 0.0f});
    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_int(log.calls, 0);
    test_int(log.count, 0);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 4.0f);

    ecs_fini(world);
}

void Moved_reparent_passes_subtree(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {5.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    log = (moved_log_t){0};
    ecs_add_pair(world, child, EcsChildOf, b);
    ecs_progress(world, 0);

    test_int(log.calls, 1);
    bool found_a = false, found_b = false, found_child = false, found_grandchild = false;
    for (int32_t i = 0; i < log.count; i ++) {
        found_a |= log.entities[i] == a;
        found_b |= log.entities[i] == b;
        found_child |= log.entities[i] == child;
        found_grandchild |= log.entities[i] == grandchild;
    }

    test_bool(found_a, false);
    test_bool(found_b, false);
    test_bool(found_child, true);
    test_bool(found_grandchild, true);
    test_flt(ecs_get(world, grandchild, WorldTransform)->m[3][0], 7.0f);

    ecs_fini(world);
}

void Moved_manual_write_before_pre_store(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    log = (moved_log_t){0};
    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_progress(world, 0);

    test_int(log.calls, 1);
    bool found_child = false;
    for (int32_t i = 0; i < log.count; i ++) {
        found_child |= log.entities[i] == child;
    }

    test_bool(found_child, true);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 11.0f);

    ecs_fini(world);
}

void Moved_manual_write_after_pre_store(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t manual = ecs_new(world);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, WorldTransform, {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    ecs_entity_t writing = manual;
    ecs_system(world, {
        .phase = EcsOnStore,
        .callback = LateManualWrite,
        .ctx = &writing,
        .immediate = true
    });

    log = (moved_log_t){0};
    ecs_progress(world, 0);

    test_uint(writing, 0);
    test_int(log.calls, 0);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 1.0f);

    ecs_progress(world, 0);

    test_int(log.calls, 1);
    bool found_child = false;
    for (int32_t i = 0; i < log.count; i ++) {
        found_child |= log.entities[i] == child;
    }

    test_bool(found_child, true);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 11.0f);

    ecs_fini(world);
}

void Moved_dynamic_removed_passes_subtree(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, e);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    log = (moved_log_t){0};
    ecs_remove(world, e, DynamicTransform);
    ecs_progress(world, 0);

    test_int(log.calls, 1);
    bool found_e = false, found_child = false;
    for (int32_t i = 0; i < log.count; i ++) {
        found_e |= log.entities[i] == e;
        found_child |= log.entities[i] == child;
    }

    test_bool(found_e, true);
    test_bool(found_child, true);

    ecs_fini(world);
}

void Moved_position_removed_passes_entity(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, NULL);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    log = (moved_log_t){0};
    ecs_remove(world, e, Position);
    ecs_progress(world, 0);

    test_int(log.calls, 1);
    bool found_e = false;
    for (int32_t i = 0; i < log.count; i ++) {
        found_e |= log.entities[i] == e;
    }

    test_bool(found_e, true);
    test_bool(ecs_has(world, e, WorldTransform), false);

    ecs_fini(world);
}

void Moved_ctx_free_on_fini(void) {
    // core_fixes/transform_changed
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    moved_log_t log = {0};
    box_transform_on_moved(world, OnMoved, &log, OnMovedFree);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_int(log.freed, 0);
    ecs_fini(world);
    test_int(log.freed, 1);
}
