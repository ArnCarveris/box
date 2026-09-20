/**
 * @file Roots.c
 * @brief Tests for the aggregate bounds of prefab and template roots.
 */

#include <box.h>
#include <bake_test.h>

void Roots_prefab_instance_is_root(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_progress(world, 0);

    test_assert(ecs_has(world, inst, RootBounds));

    ecs_fini(world);
}

void Roots_prefab_is_not_a_root(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add_pair(world, prefab, EcsIsA, base);
    ecs_progress(world, 0);

    test_assert(!ecs_has(world, prefab, RootBounds));
    test_assert(!ecs_has(world, base, RootBounds));

    ecs_fini(world);
}

void Roots_template_instance_is_root(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "template Tree {\n"
        "  trunk { Position: {0, 1, 0} }\n"
        "}\n"
        "tree {\n"
        "  Position: {3, 0, 0}\n"
        "  Tree\n"
        "}\n", NULL), 0);

    ecs_entity_t tree = ecs_lookup(world, "tree");
    test_assert(tree != 0);
    ecs_progress(world, 0);

    test_assert(ecs_has(world, tree, RootBounds));

    ecs_fini(world);
}

void Roots_manual_root_aggregates(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, RootBounds);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, AABB, {{-1, -2, -3}, {1, 2, 3}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, root, RootBounds);
    test_int(rb->count, 1);
    test_flt(rb->min.x, -1);
    test_flt(rb->max.z, 3);

    ecs_fini(world);
}

void Roots_empty_root_has_no_bounds(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 0);
    test_flt(rb->min.x, 0);
    test_flt(rb->max.x, 0);

    ecs_fini(world);
}

void Roots_aggregates_children(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, b, AABB, {{2, 0, 0}, {4, 2, 6}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 2);
    test_flt(rb->min.x, -1);
    test_flt(rb->min.y, -1);
    test_flt(rb->min.z, -1);
    test_flt(rb->max.x, 4);
    test_flt(rb->max.y, 2);
    test_flt(rb->max.z, 6);

    ecs_fini(world);
}

void Roots_aggregates_deep_hierarchy(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t cur = inst;
    int32_t i;
    for (i = 0; i < 8; i ++) {
        cur = ecs_new_w_pair(world, EcsChildOf, cur);
        ecs_set(world, cur, AABB, {{(float)i, 0, 0}, {(float)i + 1, 1, 1}});
    }

    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 8);
    test_flt(rb->min.x, 0);
    test_flt(rb->max.x, 8);

    ecs_fini(world);
}

void Roots_aggregates_parent_storage(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, EcsParent, { inst });
    ecs_set(world, a, AABB, {{-4, -1, -1}, {-2, 1, 1}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 1);
    test_flt(rb->min.x, -4);
    test_flt(rb->max.x, -2);

    ecs_fini(world);
}

void Roots_own_bounds_are_included(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, AABB, {{-5, 0, 0}, {-4, 1, 1}});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 2);
    test_flt(rb->min.x, -5);
    test_flt(rb->max.x, 1);

    ecs_fini(world);
}

void Roots_child_added_updates_bounds(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, b, AABB, {{8, 0, 0}, {9, 1, 1}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 2);
    test_flt(rb->max.x, 9);

    ecs_fini(world);
}

void Roots_child_removed_updates_bounds(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, b, AABB, {{8, 0, 0}, {9, 1, 1}});
    ecs_progress(world, 0);

    ecs_delete(world, b);
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 1);
    test_flt(rb->max.x, 1);

    ecs_fini(world);
}

void Roots_child_moved_updates_bounds(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_set(world, a, AABB, {{20, 0, 0}, {21, 1, 1}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 1);
    test_flt(rb->min.x, 20);
    test_flt(rb->max.x, 21);

    ecs_fini(world);
}

void Roots_reparent_out_of_root(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, b, AABB, {{8, 0, 0}, {9, 1, 1}});
    ecs_progress(world, 0);

    ecs_remove_pair(world, b, EcsChildOf, EcsWildcard);
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 1);
    test_flt(rb->max.x, 1);

    ecs_fini(world);
}

void Roots_reparent_into_other_root(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t first = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t second = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, first);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_add_pair(world, a, EcsChildOf, second);
    ecs_progress(world, 0);

    const RootBounds *from = ecs_get(world, first, RootBounds);
    const RootBounds *to = ecs_get(world, second, RootBounds);
    test_int(from->count, 0);
    test_int(to->count, 1);
    test_flt(to->max.x, 1);

    ecs_fini(world);
}

void Roots_nested_root_counts_once(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t outer = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_entity_t inner = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, inner, EcsChildOf, outer);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, b, AABB, {{5, 0, 0}, {6, 1, 1}});
    ecs_progress(world, 0);

    const RootBounds *in = ecs_get(world, inner, RootBounds);
    test_int(in->count, 1);
    test_flt(in->min.x, 5);

    const RootBounds *out = ecs_get(world, outer, RootBounds);
    test_int(out->count, 2);
    test_flt(out->min.x, 0);
    test_flt(out->max.x, 6);

    ecs_fini(world);
}

void Roots_local_bounds_are_root_space(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {10, 0, 0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{9, -1, -1}, {11, 1, 1}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_flt(rb->min.x, 9);
    test_flt(rb->max.x, 11);
    test_flt(rb->local_min.x, -1);
    test_flt(rb->local_max.x, 1);

    ecs_fini(world);
}

void Roots_rigid_root_move_transforms_local_bounds(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add(world, inst, RigidSubtree);
    ecs_set(world, inst, Position, {0, 0, 0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_set(world, inst, Position, {10, 0, 0});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_int(rb->count, 1);
    test_flt(rb->min.x, 9);
    test_flt(rb->max.x, 11);

    const AABB *aabb = ecs_get(world, a, AABB);
    test_flt(aabb->min.x, -1);

    ecs_fini(world);
}

void Roots_rigid_root_child_change_reaggregates(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add(world, inst, RigidSubtree);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_set(world, a, AABB, {{-3, -1, -1}, {4, 1, 1}});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_flt(rb->min.x, -3);
    test_flt(rb->max.x, 4);

    ecs_fini(world);
}

void Roots_non_rigid_root_reaggregates_on_move(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {0, 0, 0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_set(world, inst, Position, {10, 0, 0});
    ecs_progress(world, 0);

    const RootBounds *rb = ecs_get(world, inst, RootBounds);
    test_flt(rb->min.x, -1);
    test_flt(rb->max.x, 1);

    ecs_fini(world);
}

void Roots_deleted_root_is_forgotten(void)
{
    // collision/aabb_roots
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, a, AABB, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    ecs_delete(world, inst);
    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, inst));
    test_assert(!ecs_is_alive(world, a));

    ecs_fini(world);
}
