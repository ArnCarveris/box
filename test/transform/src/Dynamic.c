/**
 * @file Dynamic.c
 * @brief Tests for the per frame world transform system.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

static int32_t dynamic_events = 0;

static void count_dynamic_events(
    ecs_iter_t *it)
{
    dynamic_events += it->count;
}

void Dynamic_dynamic_root_updates_each_frame(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1, 2, 3});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12]) < 1e-4f);
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_set(world, e, Position, {4, 5, 6});
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 4) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 5) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 6) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_dynamic_parent_static_child(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {0, 1, 0});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 1) < 1e-4f);
    ecs_set(world, parent, Position, {5, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 5) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 1) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_dynamic_grandparent_static_chain(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_set(world, root, Position, {0, 0, 1});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 1});
    ecs_add_pair(world, parent, EcsChildOf, root);
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {0, 0, 1});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_set(world, root, Position, {0, 0, 10});
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[14] - 12) < 1e-4f);
    const WorldTransform *pw = ecs_get(world, parent, WorldTransform);
    test_assert(fabsf(pw->m.v[14] - 11) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_dynamic_scale_inherited(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Scale, {2, 2, 2});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 0, 0});
    ecs_set(world, child, Scale, {3, 3, 3});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 2) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 0, 0) - 6) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 1, 1) - 6) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 2, 2) - 6) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_orientation_in_dynamic(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    float a = 1.5707963f;
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Rotation, {a, 0, 0});
    ecs_set(world, e, Orientation, {0, a, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x - 1) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_dynamic_emits_no_events(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = count_dynamic_events
    });

    dynamic_events = 0;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(dynamic_events, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_add_then_remove_dynamic(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 0, 0});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, Position, {2, 0, 0});
    wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    ecs_remove(world, e, DynamicTransform);
    wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 2) < 1e-4f);
    ecs_set(world, e, Position, {3, 0, 0});
    test_assert(fabsf(wt->m.v[12] - 3) < 1e-4f);
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 3) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_manual_transform_not_written(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ManualTransform);
    ecs_add(world, e, DynamicTransform);
    ecs_set(world, e, WorldTransform, { .m = {{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1 }}});
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 7) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 8) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 9) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_deep_chain_cascade_order(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t chain[100];
    int32_t i;
    for (i = 0; i < 100; i ++) {
        chain[i] = ecs_new(world);
        ecs_set(world, chain[i], Position, {1, 0, 0});
        if (i) {
            ecs_add_pair(world, chain[i], EcsChildOf, chain[i - 1]);
        }
    }

    ecs_add(world, chain[0], DynamicTransform);
    ecs_set(world, chain[0], Position, {2, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, chain[99], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 101) < 1e-4f);
    wt = ecs_get(world, chain[50], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 52) < 1e-4f);
    ecs_fini(world);
}

void Dynamic_static_sibling_not_updated_by_system(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t dynamic = ecs_new(world);
    ecs_add(world, dynamic, DynamicTransform);
    ecs_set(world, dynamic, Position, {1, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 0, 0});
    ecs_set(world, e, WorldTransform, { .m = {{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 42, 0, 0, 1 }}});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 42) < 1e-4f);
    const WorldTransform *dw = ecs_get(world, dynamic, WorldTransform);
    test_assert(fabsf(dw->m.v[12] - 1) < 1e-4f);
    ecs_fini(world);
}
