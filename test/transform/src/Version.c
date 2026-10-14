#include <box.h>
#include <bake_test.h>
#include <math.h>

void Version_static_version_is_frame(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});

    test_uint(ecs_get(world, e, WorldTransformVersion)->version, 0);

    ecs_progress(world, 0);

    const ecs_world_info_t *info = ecs_get_world_info(world);
    test_uint(ecs_get(world, e, WorldTransformVersion)->version,
        (uint64_t)info->frame_count_total);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_set(world, e, Position, {4, 5, 6});
    ecs_progress(world, 0);

    test_uint(ecs_get(world, e, WorldTransformVersion)->version,
        (uint64_t)info->frame_count_total);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 4);

    ecs_fini(world);
}

void Version_static_version_unchanged_without_changes(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});

    ecs_progress(world, 0);

    uint64_t version = ecs_get(world, e, WorldTransformVersion)->version;
    test_assert(version != 0);

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_uint(ecs_get(world, e, WorldTransformVersion)->version, version);

    ecs_fini(world);
}

void Version_static_child_version_set_by_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {2, 0, 0});

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_progress(world, 0);

    const ecs_world_info_t *info = ecs_get_world_info(world);
    test_uint(ecs_get(world, parent, WorldTransformVersion)->version,
        (uint64_t)info->frame_count_total);
    test_uint(ecs_get(world, child, WorldTransformVersion)->version,
        (uint64_t)info->frame_count_total);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 12);

    ecs_fini(world);
}

void Version_static_parent_child_version_set_by_parent(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, NULL);
    ecs_set(world, child, Position, {2, 0, 0});

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_progress(world, 0);

    const ecs_world_info_t *info = ecs_get_world_info(world);
    test_uint(ecs_get(world, parent, WorldTransformVersion)->version,
        (uint64_t)info->frame_count_total);
    test_uint(ecs_get(world, child, WorldTransformVersion)->version,
        (uint64_t)info->frame_count_total);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 12);

    ecs_fini(world);
}

void Version_static_dirty_entity_w_current_version_skipped(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});

    ecs_progress(world, 0);

    const ecs_world_info_t *info = ecs_get_world_info(world);
    ecs_set(world, e, Position, {4, 5, 6});
    ecs_get_mut(world, e, WorldTransformVersion)->version =
        (uint64_t)info->frame_count_total + 1;

    ecs_progress(world, 0);

    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 1);

    ecs_set(world, e, Position, {7, 8, 9});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 7);

    ecs_fini(world);
}

void Version_dynamic_version_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    test_assert(ecs_has(world, child, DynamicTransform));
    ecs_set(world, child, Position, {2, 0, 0});

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 3);
    test_uint(ecs_get(world, parent, WorldTransformVersion)->version, 0);
    test_uint(ecs_get(world, child, WorldTransformVersion)->version, 0);

    ecs_fini(world);
}

void Version_manual_version_not_written(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add(world, child, ManualTransform);
    ecs_set(world, child, Position, {2, 0, 0});

    ecs_progress(world, 0);

    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_progress(world, 0);

    test_assert(ecs_get(world, parent, WorldTransformVersion)->version != 0);
    test_uint(ecs_get(world, child, WorldTransformVersion)->version, 0);

    ecs_fini(world);
}
