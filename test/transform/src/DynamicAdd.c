/**
 * @file DynamicAdd.c
 * @brief Switching many entities between static, dynamic and manual transforms.
 */

#include <box.h>
#include <bake_test.h>

static void DynamicAdd_AddDynamic(
    ecs_iter_t *it)
{
    for (int i = 0; i < it->count; i ++) {
        ecs_add(it->stage, it->entities[i], DynamicTransform);
    }
}

void DynamicAdd_static_100k_add_dynamic(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    int count = 100000;
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        ecs_add(world, ents[i], DynamicTransform);
    }

    for (int i = 0; i < count; i ++) {
        ecs_get_mut(world, ents[i], Position)->y = 1;
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 1);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_static_100k_add_dynamic_in_system(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    int count = 100000;
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
    }

    ecs_progress(world, 0);

    ecs_system(world, {
        .query.terms = {
            { .id = ecs_id(Position) },
            { .id = DynamicTransform, .oper = EcsNot }
        },
        .phase = EcsOnUpdate,
        .callback = DynamicAdd_AddDynamic
    });

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        test_assert(ecs_has(world, ents[i], DynamicTransform));
    }

    for (int i = 0; i < count; i ++) {
        ecs_get_mut(world, ents[i], Position)->y = 1;
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 1);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_static_100k_add_dynamic_before_progress(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    int count = 100000;
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
    }

    for (int i = 0; i < count; i ++) {
        ecs_add(world, ents[i], DynamicTransform);
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_childof_100k_children_add_dynamic_to_root(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    int count = 100000;
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 0, 0});
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
    }

    ecs_progress(world, 0);

    ecs_add(world, root, DynamicTransform);
    ecs_get_mut(world, root, Position)->y = 2;

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        test_assert(ecs_has(world, ents[i], DynamicTransform));
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 2);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_parent_100k_children_add_dynamic_to_root(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    int count = 100000;
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 0, 0});
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_set(world, ents[i], EcsParent, {root});
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
    }

    ecs_progress(world, 0);

    ecs_add(world, root, DynamicTransform);
    ecs_get_mut(world, root, Position)->y = 2;

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        test_assert(ecs_has(world, ents[i], DynamicTransform));
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 2);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_dynamic_100k_remove_dynamic(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    int count = 100000;
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_add(world, ents[i], DynamicTransform);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        ecs_remove(world, ents[i], DynamicTransform);
    }

    for (int i = 0; i < count; i ++) {
        ecs_set(world, ents[i], Position, {(float)i, 3, 0});
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 3);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_static_100k_add_manual(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    int count = 100000;
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        ecs_add(world, ents[i], ManualTransform);
    }

    for (int i = 0; i < count; i ++) {
        ecs_set(world, ents[i], Position, {(float)i, 4, 0});
    }

    ecs_progress(world, 0);

    for (int i = 0; i < count; i ++) {
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 0);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_static_100k_add_velocity(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMovement);

    int count = 100000;
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
    }

    ecs_progress(world, 1.0f);

    for (int i = 0; i < count; i ++) {
        ecs_set(world, ents[i], Velocity, {0, 1, 0});
    }

    ecs_progress(world, 1.0f);

    for (int i = 0; i < count; i ++) {
        test_assert(ecs_has(world, ents[i], DynamicTransform));
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 1);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}

void DynamicAdd_static_100k_shapes_add_dynamic_velocity(void) {
    // engine_core/dynamic_add_crash
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    int count = 100000;
    ecs_entity_t *ents = ecs_os_malloc_n(ecs_entity_t, count);
    for (int i = 0; i < count; i ++) {
        ents[i] = ecs_new(world);
        ecs_set(world, ents[i], Position, {(float)i, 0, 0});
        ecs_set(world, ents[i], Rotation, {0, 0, 0});
        ecs_set(world, ents[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 1.0f);
    ecs_progress(world, 1.0f);

    for (int i = 0; i < count; i ++) {
        ecs_add(world, ents[i], DynamicTransform);
    }

    for (int i = 0; i < count; i ++) {
        ecs_set(world, ents[i], Velocity, {0, 1, 0});
    }

    ecs_progress(world, 1.0f);
    ecs_progress(world, 1.0f);

    test_assert(ecs_singleton_get(world, RenderStatus)->upload_bytes > 0);
    for (int i = 0; i < count; i ++) {
        const WorldTransform *t = ecs_get(world, ents[i], WorldTransform);
        test_assert(t != NULL);
        test_flt(t->m[3][0], (float)i);
        test_flt(t->m[3][1], 2);
    }

    ecs_os_free(ents);
    ecs_fini(world);
}
