/**
 * @file Parent.c
 * @brief Tests for world transforms of trees built with the Parent component.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

static int32_t parent_events = 0;

static void count_parent_events(
    ecs_iter_t *it)
{
    parent_events += it->count;
}

void Parent_child_of_moved_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {1, 2, 3});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_set(world, parent, Position, {0, 100, 0});
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 102) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_fini(world);
}

void Parent_set_component_parents_entity(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {1, 0, 0});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    ecs_set(world, child, EcsParent, { parent });
    test_assert(ecs_get_parent(world, child) == parent);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_fini(world);
}

void Parent_grandchild_of_moved_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    float a = 1.5707963f;
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {0, 1, 0});
    ecs_entity_t grandchild = ecs_new_w_parent(world, child, "grandchild");
    ecs_set(world, grandchild, Position, {0, 0, 1});
    const WorldTransform *wt = ecs_get(world, grandchild, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 1) < 1e-4f);
    ecs_set(world, parent, Rotation, {0, a, 0});
    test_assert(fabsf(wt->m.v[12] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[14]) < 1e-4f);
    ecs_fini(world);
}

void Parent_scale_inherited_three_levels(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Scale, {2, 2, 2});
    ecs_entity_t b = ecs_new_w_parent(world, a, "b");
    ecs_set(world, b, Position, {1, 0, 0});
    ecs_set(world, b, Scale, {3, 3, 3});
    ecs_entity_t c = ecs_new_w_parent(world, b, "c");
    ecs_set(world, c, Position, {1, 0, 0});
    ecs_set(world, c, Scale, {4, 4, 4});
    const WorldTransform *wt = ecs_get(world, c, WorldTransform);
    vec3 p = mat4_transform_point(&wt->m, vec3_new(1, 0, 0));
    test_assert(fabsf(wt->m.v[12] - 8) < 1e-4f);
    test_assert(fabsf(mat4_at(&wt->m, 0, 0) - 24) < 1e-4f);
    test_assert(fabsf(p.x - 32) < 1e-4f);
    ecs_fini(world);
}

void Parent_child_set_does_not_move_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {5, 5, 5});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {2, 2, 2});
    const WorldTransform *pw = ecs_get(world, parent, WorldTransform);
    const WorldTransform *cw = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(pw->m.v[12] - 5) < 1e-4f);
    test_assert(fabsf(cw->m.v[12] - 7) < 1e-4f);
    test_assert(fabsf(cw->m.v[13] - 7) < 1e-4f);
    ecs_fini(world);
}

void Parent_reparent_to_new_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {10, 0, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {0, 20, 0});
    ecs_entity_t child = ecs_new_w_parent(world, first, "child");
    ecs_set(world, child, Position, {1, 1, 1});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_set(world, child, EcsParent, { second });
    test_assert(ecs_get_parent(world, child) == second);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 21) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 1) < 1e-4f);
    ecs_fini(world);
}

void Parent_remove_parent_makes_root(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 20, 30});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {1, 2, 3});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_remove(world, child, EcsParent);
    test_assert(ecs_get_parent(world, child) == 0);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 1) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_fini(world);
}

void Parent_manual_transform_parent_propagates(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, ManualTransform);
    ecs_set(world, parent, WorldTransform, { .m = {{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 0, 0, 1 }}});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {1, 2, 3});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 101) < 1e-4f);
    test_assert(fabsf(wt->m.v[13] - 2) < 1e-4f);
    test_assert(fabsf(wt->m.v[14] - 3) < 1e-4f);
    ecs_fini(world);
}

void Parent_one_on_set_for_new_child(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = count_parent_events
    });

    parent_events = 0;
    ecs_set(world, child, Position, {1, 0, 0});
    test_int(parent_events, 1);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_fini(world);
}

void Parent_only_changed_subtree_written(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {1, 0, 0});
    ecs_entity_t b = ecs_new_w_parent(world, a, "b");
    ecs_set(world, b, Position, {1, 0, 0});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {5, 0, 0});
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = count_parent_events
    });

    parent_events = 0;
    ecs_set(world, a, Position, {2, 0, 0});
    test_int(parent_events, 2);
    const WorldTransform *wt = ecs_get(world, b, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 3) < 1e-4f);
    ecs_fini(world);
}

void Parent_delete_parent_deletes_subtree(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {1, 0, 0});
    ecs_entity_t grandchild = ecs_new_w_parent(world, child, "grandchild");
    ecs_set(world, grandchild, Position, {1, 0, 0});
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

void Parent_deep_chain(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t chain[5];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        chain[i] = i ? ecs_new_w_parent(world, chain[i - 1], NULL) : ecs_new(world);
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

void Parent_dynamic_parent_static_child(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {0, 1, 0});
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

void Parent_static_parent_dynamic_child(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {1, 0, 0});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12]) < 1e-4f);
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_set(world, child, Position, {2, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 12) < 1e-4f);
    ecs_fini(world);
}

void Parent_dynamic_subtree_under_static_root(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {10, 0, 0});
    ecs_entity_t mover = ecs_new_w_parent(world, root, "mover");
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_parent(world, mover, "leaf");
    ecs_set(world, leaf, Position, {1, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *lw = ecs_get(world, leaf, WorldTransform);
    test_assert(fabsf(lw->m.v[12] - 12) < 1e-4f);
    ecs_set(world, mover, Position, {5, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(lw->m.v[12] - 16) < 1e-4f);
    const WorldTransform *rw = ecs_get(world, root, WorldTransform);
    test_assert(fabsf(rw->m.v[12] - 10) < 1e-4f);
    ecs_fini(world);
}

void Parent_dynamic_node_between_static_levels(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {1, 0, 0});
    ecs_entity_t mid = ecs_new_w_parent(world, root, "mid");
    ecs_add(world, mid, DynamicTransform);
    ecs_set(world, mid, Position, {1, 0, 0});
    ecs_entity_t leaf = ecs_new_w_parent(world, mid, "leaf");
    ecs_set(world, leaf, Position, {1, 0, 0});
    ecs_entity_t tip = ecs_new_w_parent(world, leaf, "tip");
    ecs_set(world, tip, Position, {1, 0, 0});
    ecs_progress(world, 0);
    const WorldTransform *tw = ecs_get(world, tip, WorldTransform);
    test_assert(fabsf(tw->m.v[12] - 4) < 1e-4f);
    ecs_set(world, root, Position, {10, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(tw->m.v[12] - 13) < 1e-4f);
    ecs_fini(world);
}

void Parent_dynamic_emits_no_events(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {1, 0, 0});
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = count_parent_events
    });

    parent_events = 0;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(parent_events, 0);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 2) < 1e-4f);
    ecs_fini(world);
}

void Parent_add_then_remove_dynamic(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, root, "child");
    ecs_set(world, child, Position, {1, 0, 0});
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {2, 0, 0});
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 11) < 1e-4f);
    ecs_remove(world, child, DynamicTransform);
    wt = ecs_get(world, child, WorldTransform);
    test_assert(fabsf(wt->m.v[12] - 12) < 1e-4f);
    ecs_progress(world, 0);
    test_assert(fabsf(wt->m.v[12] - 12) < 1e-4f);
    ecs_fini(world);
}

void Parent_prefab_tree_instantiation(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t prefab = ecs_entity(world, { .name = "Tree" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Position, {1, 0, 0});
    ecs_entity_t body = ecs_new_w_parent(world, prefab, "body");
    ecs_set(world, body, Position, {2, 0, 0});
    ecs_entity_t tip = ecs_new_w_parent(world, body, "tip");
    ecs_set(world, tip, Position, {3, 0, 0});

    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {10, 0, 0});
    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    ecs_iter_t it = ecs_children(world, inst);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &children, ecs_entity_t)[0] = it.entities[i];
        }
    }

    test_int(ecs_vec_count(&children), 1);
    ecs_entity_t inst_body = ecs_vec_get_t(&children, ecs_entity_t, 0)[0];
    test_assert(ecs_get_parent(world, inst_body) == inst);
    const WorldTransform *bw = ecs_get(world, inst_body, WorldTransform);
    test_assert(fabsf(bw->m.v[12] - 12) < 1e-4f);
    ecs_vec_clear(&children);
    it = ecs_children(world, inst_body);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &children, ecs_entity_t)[0] = it.entities[i];
        }
    }

    test_int(ecs_vec_count(&children), 1);
    ecs_entity_t inst_tip = ecs_vec_get_t(&children, ecs_entity_t, 0)[0];
    const WorldTransform *tw = ecs_get(world, inst_tip, WorldTransform);
    test_assert(fabsf(tw->m.v[12] - 15) < 1e-4f);
    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
    ecs_fini(world);
}

void Parent_lookat_under_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_entity_t child = ecs_new_w_parent(world, parent, "child");
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, LookAt, {10, 0, 5});
    ecs_progress(world, 0);
    const WorldTransform *wt = ecs_get(world, child, WorldTransform);
    vec3 f = mat4_transform_dir(&wt->m, vec3_new(0, 0, 1));
    test_assert(fabsf(wt->m.v[12] - 10) < 1e-4f);
    test_assert(fabsf(f.x) < 1e-4f);
    test_assert(fabsf(f.y) < 1e-4f);
    test_assert(fabsf(f.z - 1) < 1e-4f);
    ecs_fini(world);
}

void Parent_name_lookup_through_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t b = ecs_new_w_parent(world, a, "b");
    ecs_entity_t c = ecs_new_w_parent(world, b, "c");
    test_assert(ecs_lookup(world, "a.b.c") == c);
    test_assert(ecs_lookup_child(world, b, "c") == c);
    char *path = ecs_get_path(world, c);
    test_str(path, "a.b.c");
    ecs_os_free(path);
    ecs_fini(world);
}
