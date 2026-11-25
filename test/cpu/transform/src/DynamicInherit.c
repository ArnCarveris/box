/**
 * @file DynamicInherit.c
 * @brief Children keep the dynamics of their own when a parent stops being dynamic.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void DynamicInherit_childof_velocity_child_keeps_moving(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMovement);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0.0f, 0.0f, 0.0f});
    ecs_set(world, child, Velocity, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, child, WorldTransform)->position[0], 11.0f);

    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 1.0f);

    test_bool(ecs_has(world, child, DynamicTransform), true);
    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, child, WorldTransform)->position[0], 13.0f);

    ecs_fini(world);
}

void DynamicInherit_parent_velocity_child_keeps_moving(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMovement);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_parent(world, parent, NULL);
    ecs_set(world, child, Position, {0.0f, 0.0f, 0.0f});
    ecs_set(world, child, Velocity, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, child, WorldTransform)->position[0], 11.0f);

    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 1.0f);

    test_bool(ecs_has(world, child, DynamicTransform), true);
    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, child, WorldTransform)->position[0], 13.0f);

    ecs_fini(world);
}

void DynamicInherit_childof_angular_velocity_child_keeps_rotating(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMovement);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {0.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0.0f, 0.0f, 0.0f});
    ecs_set(world, child, Rotation, {0.0f, 0.0f, 0.0f});
    ecs_set(world, child, AngularVelocity, {0.0f, 0.5f, 0.0f});

    ecs_progress(world, 1.0f);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 1.0f);

    test_bool(ecs_has(world, child, DynamicTransform), true);
    test_flt(ecs_get(world, child, WorldTransform)->basis[0][0], cosf(1.0f));

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, child, WorldTransform)->basis[0][0], cosf(1.5f));

    ecs_fini(world);
}

void DynamicInherit_childof_tagged_child_stays_dynamic(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new(world);
    ecs_add(world, child, DynamicTransform);
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);

    test_bool(ecs_has(world, child, DynamicTransform), true);

    Position *p = ecs_get_mut(world, child, Position);
    p->x = 2.0f;
    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);

    ecs_fini(world);
}

void DynamicInherit_parent_tagged_child_stays_dynamic(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new(world);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, EcsParent, {parent});
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);

    test_bool(ecs_has(world, child, DynamicTransform), true);

    Position *p = ecs_get_mut(world, child, Position);
    p->x = 2.0f;
    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);

    ecs_fini(world);
}

void DynamicInherit_childof_inherited_child_becomes_static(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);
    test_bool(ecs_has(world, child, DynamicTransform), true);

    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);

    test_bool(ecs_has(world, child, DynamicTransform), false);

    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);

    ecs_set(world, parent, Position, {20.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 22.0f);

    ecs_fini(world);
}

void DynamicInherit_childof_velocity_grandchild_under_inherited_child(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMovement);

    ecs_entity_t root = ecs_new(world);
    ecs_add(world, root, DynamicTransform);
    ecs_set(world, root, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Position, {0.0f, 0.0f, 0.0f});
    ecs_set(world, grandchild, Velocity, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, grandchild, WorldTransform)->position[0], 12.0f);

    ecs_remove(world, root, DynamicTransform);
    ecs_progress(world, 1.0f);

    test_bool(ecs_has(world, child, DynamicTransform), false);
    test_bool(ecs_has(world, grandchild, DynamicTransform), true);
    test_flt(ecs_get(world, grandchild, WorldTransform)->position[0], 13.0f);

    ecs_set(world, child, Position, {2.0f, 0.0f, 0.0f});
    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);
    test_flt(ecs_get(world, grandchild, WorldTransform)->position[0], 15.0f);

    ecs_fini(world);
}

void DynamicInherit_childof_child_regains_tag_from_new_dynamic_parent(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);
    test_bool(ecs_has(world, child, DynamicTransform), false);

    ecs_add(world, parent, DynamicTransform);
    ecs_progress(world, 0);
    test_bool(ecs_has(world, child, DynamicTransform), true);

    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);
    test_bool(ecs_has(world, child, DynamicTransform), false);

    ecs_fini(world);
}

void DynamicInherit_childof_child_tagged_after_parenting_stays_dynamic(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);

    test_bool(ecs_has(world, child, DynamicTransform), true);

    Position *p = ecs_get_mut(world, child, Position);
    p->x = 2.0f;
    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);

    ecs_fini(world);
}

void DynamicInherit_parent_child_tagged_after_parenting_stays_dynamic(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_parent(world, parent, NULL);
    ecs_add(world, child, DynamicTransform);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);

    test_bool(ecs_has(world, child, DynamicTransform), true);

    Position *p = ecs_get_mut(world, child, Position);
    p->x = 2.0f;
    ecs_progress(world, 0);

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12.0f);

    ecs_fini(world);
}

void DynamicInherit_childof_child_tagged_after_progress_stays_dynamic(void) {
    // core_fixes/dynamic_unspread
    WorldTransform buf;
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t parent = ecs_new(world);
    ecs_add(world, parent, DynamicTransform);
    ecs_set(world, parent, Position, {10.0f, 0.0f, 0.0f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {1.0f, 0.0f, 0.0f});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Position, {1.0f, 0.0f, 0.0f});

    ecs_progress(world, 0);
    test_bool(ecs_has(world, child, DynamicTransform), true);

    ecs_add(world, child, DynamicTransform);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);

    test_bool(ecs_has(world, child, DynamicTransform), true);
    test_bool(ecs_has(world, grandchild, DynamicTransform), true);

    Position *p = ecs_get_mut(world, child, Position);
    p->x = 2.0f;
    ecs_progress(world, 0);

    test_flt(box_transform_world(world, child, &buf)->position[0], 12.0f);
    test_flt(box_transform_world(world, grandchild, &buf)->position[0], 13.0f);

    ecs_fini(world);
}

void DynamicInherit_script_tagged_child_stays_dynamic(void) {
    // core_fixes/dynamic_unspread
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    test_assert(ecs_script_run(world, "scene",
        "using box.transform\n"
        "p {\n"
        "  DynamicTransform\n"
        "  Position: {10, 0, 0}\n"
        "  tagged {\n"
        "    DynamicTransform\n"
        "    Position: {1, 0, 0}\n"
        "  }\n"
        "  untagged {\n"
        "    Position: {1, 0, 0}\n"
        "  }\n"
        "}\n", NULL) == 0);

    ecs_entity_t parent = ecs_lookup(world, "p");
    ecs_entity_t tagged = ecs_lookup(world, "p.tagged");
    ecs_entity_t untagged = ecs_lookup(world, "p.untagged");
    test_assert(parent != 0);
    test_assert(tagged != 0);
    test_assert(untagged != 0);
    test_bool(ecs_has(world, untagged, DynamicTransform), true);

    ecs_progress(world, 0);
    ecs_remove(world, parent, DynamicTransform);
    ecs_progress(world, 0);

    test_bool(ecs_has(world, tagged, DynamicTransform), true);
    test_bool(ecs_has(world, untagged, DynamicTransform), false);

    Position *p = ecs_get_mut(world, tagged, Position);
    p->x = 2.0f;
    ecs_progress(world, 0);

    test_flt(ecs_get(world, tagged, WorldTransform)->position[0], 12.0f);

    ecs_set(world, parent, Position, {20.0f, 0.0f, 0.0f});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, untagged, WorldTransform)->position[0], 21.0f);

    ecs_fini(world);
}
