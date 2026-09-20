/**
 * @file Mixed.c
 * @brief Tests for trees that mix the ChildOf pair and the Parent component.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

static int32_t mixed_events = 0;

static void count_mixed_events(
    ecs_iter_t *it)
{
    mixed_events += it->count;
}

void Mixed_childof_child_under_parent_node(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t node = ecs_new_w_parent(world, root, "node");
    ecs_set(world, node, Position, {2, 0, 0});
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, node);
    ecs_set(world, leaf, Position, {4, 0, 0});
    const WorldTransform *wt = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 7) < 1e-4f);
    ecs_set(world, root, Position, {10, 0, 0});
    test_assert(fabsf(wt->m.v[12] - 16) < 1e-4f);
    ecs_fini(world);
}

void Mixed_parent_child_under_childof_node(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t node = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, node, Position, {2, 0, 0});
    ecs_entity_t leaf = ecs_new_w_parent(world, node, "leaf");
    ecs_set(world, leaf, Position, {4, 0, 0});
    const WorldTransform *wt = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 7) < 1e-4f);
    ecs_set(world, root, Position, {10, 0, 0});
    test_assert(fabsf(wt->m.v[12] - 16) < 1e-4f);
    ecs_fini(world);
}

void Mixed_deep_chain_alternating(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t chain[5];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        if (!i) {
            chain[i] = ecs_new(world);
        } else if (i & 1) {
            chain[i] = ecs_new_w_parent(world, chain[i - 1], NULL);
        } else {
            chain[i] = ecs_new_w_pair(world, EcsChildOf, chain[i - 1]);
        }

        ecs_set(world, chain[i], Position, {1, 0, 0});
    }

    const WorldTransform *wt = ecs_get(world, chain[4], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 5) < 1e-4f);
    ecs_set(world, chain[0], Position, {2, 0, 0});
    test_assert(fabsf(wt->m.v[12] - 6) < 1e-4f);
    wt = ecs_get(world, chain[2], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 4) < 1e-4f);
    ecs_fini(world);
}

void Mixed_scale_inherited_alternating(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Scale, {2, 2, 2});
    ecs_entity_t b = ecs_new_w_parent(world, a, "b");
    ecs_set(world, b, Position, {1, 0, 0});
    ecs_set(world, b, Scale, {3, 3, 3});
    ecs_entity_t c = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, c, Position, {1, 0, 0});
    ecs_set(world, c, Scale, {4, 4, 4});
    const WorldTransform *wt = ecs_get(world, c, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 8) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 0, 0) - 24) < 1e-4f);
    ecs_fini(world);
}

void Mixed_reparent_childof_to_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {10, 0, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {0, 20, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, first);
    ecs_set(world, child, Position, {1, 1, 1});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_set(world, child, EcsParent, { second });
    test_assert(ecs_get_parent(world, child) == second);
    test_assert(!ecs_has_pair(world, child, EcsChildOf, first));
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 21) < 1e-4f);
    ecs_fini(world);
}

void Mixed_reparent_parent_to_childof(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {10, 0, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {0, 20, 0});
    ecs_entity_t child = ecs_new_w_parent(world, first, "child");
    ecs_set(world, child, Position, {1, 1, 1});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_add_pair(world, child, EcsChildOf, second);
    test_assert(ecs_get_parent(world, child) == second);
    test_assert(!ecs_has(world, child, EcsParent));
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 21) < 1e-4f);
    ecs_fini(world);
}

void Mixed_reparent_back_and_forth(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t pair_parent = ecs_new(world);
    ecs_set(world, pair_parent, Position, {10, 0, 0});
    ecs_entity_t comp_parent = ecs_new(world);
    ecs_set(world, comp_parent, Position, {100, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, pair_parent);
    ecs_set(world, child, Position, {1, 0, 0});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_set(world, child, EcsParent, { comp_parent });
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 101) < 1e-4f);
    ecs_add_pair(world, child, EcsChildOf, pair_parent);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_remove_pair(world, child, EcsChildOf, EcsWildcard);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    ecs_fini(world);
}

void Mixed_reparent_subtree_keeps_descendants(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {10, 0, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {100, 0, 0});
    ecs_entity_t node = ecs_new_w_pair(world, EcsChildOf, first);
    ecs_set(world, node, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_parent(world, node, "leaf");
    ecs_set(world, leaf, Position, {1, 0, 0});
    const WorldTransform *lw = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(lw->m.v[12] - 12) < 1e-4f);
    ecs_set(world, node, EcsParent, { second });
    lw = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(lw->m.v[12] - 102) < 1e-4f);
    test_assert(ecs_get_parent(world, leaf) == node);
    ecs_fini(world);
}

void Mixed_reparent_dynamic_node(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {10, 0, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {100, 0, 0});
    ecs_entity_t node = ecs_new_w_pair(world, EcsChildOf, first);
    ecs_add(world, node, DynamicTransform);
    ecs_set(world, node, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_parent(world, node, "leaf");
    ecs_set(world, leaf, Position, {1, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *lw = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(lw->m.v[12] - 12) < 1e-4f);
    ecs_set(world, node, EcsParent, { second });
    ecs_progress(world, 0);
    lw = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(lw->m.v[12] - 102) < 1e-4f);
    ecs_set(world, node, Position, {5, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(lw->m.v[12] - 106) < 1e-4f);
    ecs_fini(world);
}

void Mixed_dynamic_parent_root_childof_child(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t node = ecs_new_w_parent(world, root, "node");
    ecs_set(world, node, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, node);
    ecs_set(world, leaf, Position, {1, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 3) < 1e-4f);
    ecs_set(world, root, Position, {10, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 12) < 1e-4f);
    ecs_fini(world);
}

void Mixed_dynamic_childof_node_under_parent_root(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {10, 0, 0});
    ecs_entity_t node = ecs_new_w_parent(world, root, "node");
    ecs_add(world, node, DynamicTransform);
    ecs_set(world, node, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, node);
    ecs_set(world, leaf, Position, {1, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 12) < 1e-4f);
    ecs_set(world, node, Position, {5, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 16) < 1e-4f);
    const WorldTransform *rw = ecs_get(world, root, WorldTransform);
    test_assert(fabsf(rw->m.v[12] - 10) < 1e-4f);
    ecs_fini(world);
}

void Mixed_dynamic_cascade_order_alternating(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t chain[5];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        if (!i) {
            chain[i] = ecs_new(world);
        } else if (i & 1) {
            chain[i] = ecs_new_w_parent(world, chain[i - 1], NULL);
        } else {
            chain[i] = ecs_new_w_pair(world, EcsChildOf, chain[i - 1]);
        }

        ecs_set(world, chain[i], Position, {1, 0, 0});
    }

    ecs_add(world, chain[0], DynamicTransform);
    ecs_set(world, chain[0], Position, {2, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, chain[4], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 6) < 1e-4f);
    wt = ecs_get(world, chain[2], WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 4) < 1e-4f);
    ecs_fini(world);
}

void Mixed_dynamic_node_between_static_levels(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t mid = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_add(world, mid, DynamicTransform);
    ecs_set(world, mid, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_parent(world, mid, "leaf");
    ecs_set(world, leaf, Position, {1, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 3) < 1e-4f);
    ecs_set(world, root, Position, {10, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 12) < 1e-4f);
    ecs_fini(world);
}

void Mixed_delete_parent_node_with_children(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t node = ecs_new_w_parent(world, root, "node");
    ecs_set(world, node, Position, {1, 0, 0});
    ecs_entity_t pair_leaf = ecs_new_w_pair(world, EcsChildOf, node);
    ecs_set(world, pair_leaf, Position, {1, 0, 0});
    ecs_entity_t comp_leaf = ecs_new_w_parent(world, node, "comp_leaf");
    ecs_set(world, comp_leaf, Position, {1, 0, 0});
    ecs_entity_t sibling = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, sibling, Position, {5, 0, 0});
    ecs_delete(world, node);
    test_assert(!ecs_is_alive(world, node));
    test_assert(!ecs_is_alive(world, pair_leaf));
    test_assert(!ecs_is_alive(world, comp_leaf));
    test_assert(ecs_is_alive(world, sibling));
    ecs_set(world, root, Position, {100, 0, 0});
    const WorldTransform *sw = ecs_get(world, sibling, WorldTransform);
    test_assert(fabsf(sw->m.v[12] - 105) < 1e-4f);
    ecs_fini(world);
}

void Mixed_only_changed_subtree_written(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t node = ecs_new_w_parent(world, root, "node");
    ecs_set(world, node, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, node);
    ecs_set(world, leaf, Position, {1, 0, 0});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {5, 0, 0});
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = count_mixed_events
    });

    mixed_events = 0;
    ecs_set(world, node, Position, {2, 0, 0});
    test_int(mixed_events, 2);
    mixed_events = 0;
    ecs_set(world, root, Position, {2, 0, 0});
    test_int(mixed_events, 3);
    ecs_fini(world);
}

void Mixed_manual_transform_between_storages(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t manual = ecs_new_w_parent(world, root, "manual");
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, WorldTransform, { .m = {{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 0, 0, 1 }}});
    ecs_entity_t leaf = ecs_new_w_pair(world, EcsChildOf, manual);
    ecs_set(world, leaf, Position, {1, 0, 0});
    const WorldTransform *wt = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 101) < 1e-4f);
    ecs_set(world, root, Position, {50, 0, 0});
    test_assert(fabsf(wt->m.v[12] - 101) < 1e-4f);
    ecs_fini(world);
}
