/**
 * @file OnDynamic.c
 * @brief box_transform_on_dynamic: which entities listeners see gain or lose DynamicTransform.
 */

#include <box.h>
#include <bake_test.h>

/** What a listener received */
typedef struct {
    ecs_entity_t added[64];   /**< Entities passed with an OnAdd event */
    ecs_entity_t removed[64]; /**< Entities passed with an OnRemove event */
    int32_t add_count;        /**< Entities passed with an OnAdd event */
    int32_t remove_count;     /**< Entities passed with an OnRemove event */
    int32_t freed;            /**< Calls of the free function */
} dynamic_log_t;

/** Record the entities of an event passed to a listener.
 *
 * @param it The iterator, ctx is the log
 */
static void OnDynamic(
    ecs_iter_t *it)
{
    dynamic_log_t *log = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        if (it->event == EcsOnAdd && log->add_count < 64) {
            log->added[log->add_count ++] = it->entities[i];
        } else if (it->event == EcsOnRemove && log->remove_count < 64) {
            log->removed[log->remove_count ++] = it->entities[i];
        }
    }
}

/** Count the calls of the free function of a listener.
 *
 * @param ctx The log
 */
static void OnDynamicFree(
    void *ctx)
{
    dynamic_log_t *log = ctx;
    log->freed ++;
}

void OnDynamic_owned_and_spread(void) {
    // core_fixes/unspread_delete_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    dynamic_log_t log = {0};
    box_transform_on_dynamic(world, OnDynamic, &log, NULL);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {1, 0, 0});
    ecs_progress(world, 0);
    test_int(log.add_count, 0);

    ecs_add(world, parent, DynamicTransform);
    test_int(log.add_count, 2);
    test_uint(log.added[0], parent);
    test_uint(log.added[1], child);
    test_assert(ecs_has(world, child, DynamicTransform));

    ecs_remove(world, parent, DynamicTransform);
    test_int(log.remove_count, 2);
    test_uint(log.removed[0], parent);
    test_uint(log.removed[1], child);
    test_assert(!ecs_has(world, child, DynamicTransform));

    ecs_fini(world);
}

void OnDynamic_parent_component_spread(void) {
    // core_fixes/unspread_delete_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    dynamic_log_t log = {0};
    box_transform_on_dynamic(world, OnDynamic, &log, NULL);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_add(world, parent, DynamicTransform);
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, EcsParent, {parent});
    ecs_set(world, child, Position, {1, 0, 0});
    test_int(log.add_count, 2);
    test_uint(log.added[0], parent);
    test_uint(log.added[1], child);

    ecs_add(world, child, DynamicTransform);
    ecs_remove(world, parent, DynamicTransform);
    test_int(log.remove_count, 1);
    test_uint(log.removed[0], parent);
    test_assert(ecs_has(world, child, DynamicTransform));

    ecs_fini(world);
}

void OnDynamic_prefab_instance(void) {
    // core_fixes/unspread_delete_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    dynamic_log_t log = {0};
    box_transform_on_dynamic(world, OnDynamic, &log, NULL);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, prefab, DynamicTransform);
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, instance, Position, {1, 0, 0});
    test_assert(ecs_has(world, instance, DynamicTransform));
    test_int(log.add_count, 1);
    test_uint(log.added[0], instance);

    ecs_progress(world, 0);
    ecs_set(world, instance, Position, {2, 0, 0});
    ecs_progress(world, 0);
    test_flt(ecs_get(world, instance, WorldTransform)->position[0], 2.0f);

    ecs_remove_pair(world, instance, EcsIsA, prefab);
    test_int(log.remove_count, 1);
    test_uint(log.removed[0], instance);

    ecs_fini(world);
}

void OnDynamic_deleted_children_not_passed(void) {
    // core_fixes/unspread_delete_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    dynamic_log_t log = {0};
    box_transform_on_dynamic(world, OnDynamic, &log, NULL);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_add(world, parent, DynamicTransform);
    ecs_entity_t children[4];
    for (int32_t i = 0; i < 4; i ++) {
        children[i] = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, children[i], Position, {(float)i, 0, 0});
    }

    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, EcsParent, {parent});
    ecs_set(world, other, Position, {1, 0, 0});
    ecs_progress(world, 0);

    ecs_delete(world, parent);
    test_int(log.remove_count, 1);
    test_uint(log.removed[0], parent);

    ecs_progress(world, 0);

    ecs_fini(world);
}

void OnDynamic_ctx_free_on_fini(void) {
    // core_fixes/unspread_delete_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    dynamic_log_t log = {0};
    box_transform_on_dynamic(world, OnDynamic, &log, OnDynamicFree);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    test_int(log.add_count, 1);

    ecs_fini(world);
    test_int(log.freed, 1);
}
