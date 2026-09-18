/**
 * @file Static.c
 * @brief Tests for observer driven world transforms of static entities.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

static int32_t world_transform_events = 0;

static void count_world_transform(
    ecs_iter_t *it)
{
    world_transform_events += it->count;
}

void Static_position_only(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(wt != NULL);
    test_assert(fabsf(wt->m.v[0] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[5] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[10] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_fini(world);
}

void Static_scale_defaults_to_one(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Scale);
    const Scale *s = ecs_get(world, e, Scale);
    test_assert(s != NULL);
    test_assert(fabsf(s->x - 1) < 1e-4f);
    test_assert(fabsf(s->y - 1) < 1e-4f);
    test_assert(fabsf(s->z - 1) < 1e-4f);
    ecs_set(world, e, Position, {4, 5, 6});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[0] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[5] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[10] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[12] - 4) < 1e-4f);
    ecs_fini(world);
}

void Static_rotation_composition(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    float x = 0.25f, y = 0.5f, z = -0.75f;
    float cx = cosf(x), sx = sinf(x);
    float cy = cosf(y), sy = sinf(y);
    float cz = cosf(z), sz = sinf(z);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 0, 0});
    ecs_set(world, e, Rotation, {x, y, z});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(mat4_at(&wt->m, 0, 0) - cy * cz) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 0, 1) + cy * sz) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 0, 2) - sy) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 1, 0) - (cx * sz + sx * sy * cz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 1, 1) - (cx * cz - sx * sy * sz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 1, 2) + sx * cy) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 2, 0) - (sx * sz - cx * sy * cz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 2, 1) - (sx * cz + cx * sy * sz)) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 2, 2) - cx * cy) < 1e-4f);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    ecs_fini(world);
}

void Static_rotation_and_scale_composition(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    float a = 1.5707963f;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Rotation, {0, a, 0});
    ecs_set(world, e, Scale, {2, 3, 4});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 p = mat4_transform_point(&wt->m, vec3_new(1, 1, 1));
    test_assert(fabsf(mat4_at(&wt->m, 0, 0)) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 2, 0) + 2) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 1, 1) - 3) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 0, 2) - 4) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 2, 2)) < 1e-4f);
    test_assert(fabsf(p.x - 5) < 1e-4f);
    test_assert(fabsf(p.y - 5) < 1e-4f);
    test_assert(fabsf(p.z - 1) < 1e-4f);
    ecs_fini(world);
}

void Static_orientation_wins_over_rotation(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    float a = 1.5707963f;
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rotation, {a, 0, 0});
    ecs_set(world, e, Orientation, {0, a, 0});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x - 1) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z) < 1e-4f);
    ecs_remove(world, e, Orientation);
    ecs_set(world, e, Rotation, {a, 0, 0});
    wt = ecs_get(world, e, WorldTransform);
    f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y + 1) < 1e-4f);
    test_assert(fabsf(f.z) < 1e-4f);
    ecs_fini(world);
}

void Static_child_of_moved_parent(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 2, 3});
    ecs_add_pair(world, child, EcsChildOf, parent);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_set(world, parent, Position, {0, 100, 0});
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 102) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_fini(world);
}

void Static_grandchild_of_moved_parent(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    float a = 1.5707963f;
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {0, 1, 0});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_entity_t grandchild = ecs_new(world);
    ecs_set(world, grandchild, Position, {0, 0, 1});
    ecs_add_pair(world, grandchild, EcsChildOf, child);
    const WorldTransform *wt = ecs_get(world, grandchild, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 1) < 1e-4f);
    ecs_set(world, parent, Rotation, {0, a, 0});
    wt = ecs_get(world, grandchild, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[14]) < 1e-4f);
    ecs_fini(world);
}

void Static_scale_inherited_three_levels(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Scale, {2, 2, 2});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {1, 0, 0});
    ecs_set(world, b, Scale, {3, 3, 3});
    ecs_add_pair(world, b, EcsChildOf, a);
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, Position, {1, 0, 0});
    ecs_set(world, c, Scale, {4, 4, 4});
    ecs_add_pair(world, c, EcsChildOf, b);
    const WorldTransform *wt = ecs_get(world, c, WorldTransform);
    vec3 p = mat4_transform_point(&wt->m, vec3_new(1, 0, 0));
    test_assert(fabsf(wt->m.v[12] - 8) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 0, 0) - 24) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 1, 1) - 24) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 2, 2) - 24) < 1e-4f);
    test_assert(fabsf(p.x - 32) < 1e-4f);
    ecs_fini(world);
}

void Static_child_set_does_not_move_parent(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {5, 5, 5});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 1, 1});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_set(world, child, Position, {2, 2, 2});
    const WorldTransform *pw = ecs_get(world, parent, WorldTransform);
    const WorldTransform *cw = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(pw->m.v[12] - 5) < 1e-4f);
    test_assert(fabsf(pw->m.v[13] - 5) < 1e-4f);
    test_assert(fabsf(cw->m.v[12] - 7) < 1e-4f);
    test_assert(fabsf(cw->m.v[13] - 7) < 1e-4f);
    ecs_fini(world);
}

void Static_reparent_to_new_parent(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {10, 0, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {0, 20, 0});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 1, 1});
    ecs_add_pair(world, child, EcsChildOf, first);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 1) < 1e-4f);
    ecs_add_pair(world, child, EcsChildOf, second);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 21) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 1) < 1e-4f);
    ecs_fini(world);
}

void Static_remove_parent_makes_root(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 20, 30});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 2, 3});
    ecs_add_pair(world, child, EcsChildOf, parent);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_remove_pair(world, child, EcsChildOf, parent);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_fini(world);
}

void Static_manual_transform_not_written(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ManualTransform);
    ecs_set(world, e, WorldTransform, { .m = {{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1 }}});
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Rotation, {1, 1, 1});
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 7) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 8) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 9) < 1e-4f);
    ecs_fini(world);
}

void Static_manual_transform_parent_propagates(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, ManualTransform);
    ecs_set(world, parent, WorldTransform, { .m = {{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 0, 0, 1 }}});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 2, 3});
    ecs_add_pair(world, child, EcsChildOf, parent);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 101) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_fini(world);
}

void Static_one_on_set_per_entity(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t entities[1000];
    int32_t i;
    for (i = 0; i < 1000; i ++) {
        entities[i] = ecs_new(world);
        ecs_set(world, entities[i], Position, {(float)i, 0, 0});
    }

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = count_world_transform
    });

    world_transform_events = 0;
    for (i = 0; i < 1000; i ++) {
        ecs_set(world, entities[i], Position, {0, (float)i, 0});
    }

    test_int(world_transform_events, 1000);
    const WorldTransform *wt = ecs_get(world, entities[999], WorldTransform);
    test_assert(fabsf(wt->m.v[13] - 999) < 1e-4f);
    ecs_fini(world);
}

void Static_deep_chain(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t chain[100];
    int32_t i;
    for (i = 0; i < 100; i ++) {
        chain[i] = ecs_new(world);
        ecs_set(world, chain[i], Position, {1, 0, 0});
        if (i) {
            ecs_add_pair(world, chain[i], EcsChildOf, chain[i - 1]);
        }
    }

    const WorldTransform *wt = ecs_get(world, chain[99], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 100) < 1e-4f);
    ecs_set(world, chain[0], Position, {2, 0, 0});
    wt = ecs_get(world, chain[99], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 101) < 1e-4f);
    wt = ecs_get(world, chain[50], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 52) < 1e-4f);
    ecs_fini(world);
}

void Static_one_on_set_for_new_child(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = count_world_transform
    });

    world_transform_events = 0;
    ecs_set(world, child, Position, {1, 0, 0});
    test_int(world_transform_events, 1);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_fini(world);
}

void Static_delete_parent_deletes_subtree(void)
{
    // scene_core/transform
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 0, 0});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_entity_t grandchild = ecs_new(world);
    ecs_set(world, grandchild, Position, {1, 0, 0});
    ecs_add_pair(world, grandchild, EcsChildOf, child);
    ecs_delete(world, parent);
    test_assert(!ecs_is_alive(world, parent));
    test_assert(!ecs_is_alive(world, child));
    test_assert(!ecs_is_alive(world, grandchild));
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {2, 3, 4});
    const WorldTransform *wt = ecs_get(world, other, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 3) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 4) < 1e-4f);
    ecs_fini(world);
}
