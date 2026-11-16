/**
 * @file DynamicParentRuns.c
 * @brief Dynamic Parent hierarchies whose siblings of different parents share a table.
 */

#include <box.h>
#include <bake_test.h>

void DynamicParentRuns_interleaved_parents(void) {
    // core_fixes/transform_cleanups
    WorldTransform buf;
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t a = ecs_new(world);
    ecs_add(world, a, DynamicTransform);
    ecs_set(world, a, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t b = ecs_new(world);
    ecs_add(world, b, DynamicTransform);
    ecs_set(world, b, Position, {20.0f, 0.0f, 0.0f});

    ecs_entity_t children[8];
    for (int i = 0; i < 8; i ++) {
        children[i] = ecs_new_w_parent(world, (i % 3) ? a : b, NULL);
        ecs_set(world, children[i], Position, {1.0f, (float)i, 0.0f});
    }

    ecs_progress(world, 0);

    for (int i = 0; i < 8; i ++) {
        WorldTransform t_buf;
        const WorldTransform *t = box_transform_world(world, children[i], &t_buf);
        test_flt(t->m[3][0], (i % 3) ? 11.0f : 21.0f);
        test_flt(t->m[3][1], (float)i);
    }

    ecs_get_mut(world, a, Position)->x = 30.0f;
    ecs_get_mut(world, b, Position)->x = 40.0f;
    ecs_progress(world, 0);

    for (int i = 0; i < 8; i ++) {
        test_flt(box_transform_world(world, children[i], &buf)->m[3][0],
            (i % 3) ? 31.0f : 41.0f);
    }

    ecs_fini(world);
}

void DynamicParentRuns_parent_without_transform(void) {
    // core_fixes/transform_cleanups
    WorldTransform buf;
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t bare = ecs_new_w_parent(world, root, NULL);
    ecs_entity_t placed = ecs_new_w_parent(world, root, NULL);
    ecs_set(world, placed, Position, {5.0f, 0.0f, 0.0f});

    ecs_entity_t under_bare = ecs_new_w_parent(world, bare, NULL);
    ecs_set(world, under_bare, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t under_placed = ecs_new_w_parent(world, placed, NULL);
    ecs_set(world, under_placed, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t under_bare_2 = ecs_new_w_parent(world, bare, NULL);
    ecs_set(world, under_bare_2, Position, {2.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);

    test_flt(box_transform_world(world, under_bare, &buf)->m[3][0], 11.0f);
    test_flt(box_transform_world(world, under_placed, &buf)->m[3][0], 16.0f);
    test_flt(box_transform_world(world, under_bare_2, &buf)->m[3][0], 12.0f);

    ecs_get_mut(world, root, Position)->x = 20.0f;
    ecs_progress(world, 0);

    test_flt(box_transform_world(world, under_bare, &buf)->m[3][0], 21.0f);
    test_flt(box_transform_world(world, under_placed, &buf)->m[3][0], 26.0f);
    test_flt(box_transform_world(world, under_bare_2, &buf)->m[3][0], 22.0f);

    ecs_fini(world);
}
