
/* A friendly warning from bake.test
 * ----------------------------------------------------------------------------
 * This file is generated. To add/remove testcases modify the 'project.json' of
 * the test project. ANY CHANGE TO THIS FILE IS LOST AFTER (RE)BUILDING!
 * ----------------------------------------------------------------------------
 */

#include <bake_test.h>

// Testsuite 'Math'
void Math_identity_is_neutral(void);
void Math_translation_matrix(void);
void Math_scale_matrix(void);
void Math_rotation_x_hand_computed(void);
void Math_rotation_xyz_hand_computed(void);
void Math_rotation_yxz_hand_computed(void);
void Math_trs_composition(void);
void Math_parent_child_multiplication(void);
void Math_facing_z_axis_matches_direction(void);
void Math_facing_euler_round_trip(void);

// Testsuite 'Static'
void Static_position_only(void);
void Static_scale_defaults_to_one(void);
void Static_rotation_composition(void);
void Static_rotation_and_scale_composition(void);
void Static_orientation_wins_over_rotation(void);
void Static_child_of_moved_parent(void);
void Static_grandchild_of_moved_parent(void);
void Static_scale_inherited_three_levels(void);
void Static_child_set_does_not_move_parent(void);
void Static_reparent_to_new_parent(void);
void Static_remove_parent_makes_root(void);
void Static_manual_transform_not_written(void);
void Static_manual_transform_parent_propagates(void);
void Static_one_on_set_per_entity(void);
void Static_one_on_set_for_new_child(void);
void Static_delete_parent_deletes_subtree(void);
void Static_deep_chain(void);

// Testsuite 'Dynamic'
void Dynamic_dynamic_root_updates_each_frame(void);
void Dynamic_dynamic_parent_static_child(void);
void Dynamic_dynamic_grandparent_static_chain(void);
void Dynamic_dynamic_scale_inherited(void);
void Dynamic_orientation_in_dynamic(void);
void Dynamic_dynamic_emits_no_events(void);
void Dynamic_add_then_remove_dynamic(void);
void Dynamic_manual_transform_not_written(void);
void Dynamic_deep_chain_cascade_order(void);
void Dynamic_static_sibling_not_updated_by_system(void);

// Testsuite 'LookAt'
void LookAt_target_in_front(void);
void LookAt_target_behind(void);
void LookAt_target_below(void);
void LookAt_target_above(void);
void LookAt_target_to_the_side(void);
void LookAt_rotation_round_trips(void);
void LookAt_follows_moving_entity(void);
void LookAt_child_of_translated_parent(void);
void LookAt_dynamic_entity(void);

bake_test_case Math_testcases[] = {
    {
        "identity_is_neutral",
        Math_identity_is_neutral
    },
    {
        "translation_matrix",
        Math_translation_matrix
    },
    {
        "scale_matrix",
        Math_scale_matrix
    },
    {
        "rotation_x_hand_computed",
        Math_rotation_x_hand_computed
    },
    {
        "rotation_xyz_hand_computed",
        Math_rotation_xyz_hand_computed
    },
    {
        "rotation_yxz_hand_computed",
        Math_rotation_yxz_hand_computed
    },
    {
        "trs_composition",
        Math_trs_composition
    },
    {
        "parent_child_multiplication",
        Math_parent_child_multiplication
    },
    {
        "facing_z_axis_matches_direction",
        Math_facing_z_axis_matches_direction
    },
    {
        "facing_euler_round_trip",
        Math_facing_euler_round_trip
    }
};

bake_test_case Static_testcases[] = {
    {
        "position_only",
        Static_position_only
    },
    {
        "scale_defaults_to_one",
        Static_scale_defaults_to_one
    },
    {
        "rotation_composition",
        Static_rotation_composition
    },
    {
        "rotation_and_scale_composition",
        Static_rotation_and_scale_composition
    },
    {
        "orientation_wins_over_rotation",
        Static_orientation_wins_over_rotation
    },
    {
        "child_of_moved_parent",
        Static_child_of_moved_parent
    },
    {
        "grandchild_of_moved_parent",
        Static_grandchild_of_moved_parent
    },
    {
        "scale_inherited_three_levels",
        Static_scale_inherited_three_levels
    },
    {
        "child_set_does_not_move_parent",
        Static_child_set_does_not_move_parent
    },
    {
        "reparent_to_new_parent",
        Static_reparent_to_new_parent
    },
    {
        "remove_parent_makes_root",
        Static_remove_parent_makes_root
    },
    {
        "manual_transform_not_written",
        Static_manual_transform_not_written
    },
    {
        "manual_transform_parent_propagates",
        Static_manual_transform_parent_propagates
    },
    {
        "one_on_set_per_entity",
        Static_one_on_set_per_entity
    },
    {
        "one_on_set_for_new_child",
        Static_one_on_set_for_new_child
    },
    {
        "delete_parent_deletes_subtree",
        Static_delete_parent_deletes_subtree
    },
    {
        "deep_chain",
        Static_deep_chain
    }
};

bake_test_case Dynamic_testcases[] = {
    {
        "dynamic_root_updates_each_frame",
        Dynamic_dynamic_root_updates_each_frame
    },
    {
        "dynamic_parent_static_child",
        Dynamic_dynamic_parent_static_child
    },
    {
        "dynamic_grandparent_static_chain",
        Dynamic_dynamic_grandparent_static_chain
    },
    {
        "dynamic_scale_inherited",
        Dynamic_dynamic_scale_inherited
    },
    {
        "orientation_in_dynamic",
        Dynamic_orientation_in_dynamic
    },
    {
        "dynamic_emits_no_events",
        Dynamic_dynamic_emits_no_events
    },
    {
        "add_then_remove_dynamic",
        Dynamic_add_then_remove_dynamic
    },
    {
        "manual_transform_not_written",
        Dynamic_manual_transform_not_written
    },
    {
        "deep_chain_cascade_order",
        Dynamic_deep_chain_cascade_order
    },
    {
        "static_sibling_not_updated_by_system",
        Dynamic_static_sibling_not_updated_by_system
    }
};

bake_test_case LookAt_testcases[] = {
    {
        "target_in_front",
        LookAt_target_in_front
    },
    {
        "target_behind",
        LookAt_target_behind
    },
    {
        "target_below",
        LookAt_target_below
    },
    {
        "target_above",
        LookAt_target_above
    },
    {
        "target_to_the_side",
        LookAt_target_to_the_side
    },
    {
        "rotation_round_trips",
        LookAt_rotation_round_trips
    },
    {
        "follows_moving_entity",
        LookAt_follows_moving_entity
    },
    {
        "child_of_translated_parent",
        LookAt_child_of_translated_parent
    },
    {
        "dynamic_entity",
        LookAt_dynamic_entity
    }
};

static bake_test_suite suites[] = {
    {
        "Math",
        NULL,
        NULL,
        10,
        Math_testcases
    },
    {
        "Static",
        NULL,
        NULL,
        17,
        Static_testcases
    },
    {
        "Dynamic",
        NULL,
        NULL,
        10,
        Dynamic_testcases
    },
    {
        "LookAt",
        NULL,
        NULL,
        9,
        LookAt_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("transform", argc, argv, suites, 4);
}
