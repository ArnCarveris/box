/**
 * @file Transitions.c
 * @brief Tests for starting, replacing, delaying and timing a transition.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/transition/transition_internal.h"

void Transitions_map_gives_an_entity_the_private_state(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Rgb: {10, 20, 30}\n"
        "  Transition: [Rgb: {duration: 1}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(e != 0);
    test_assert(ecs_has(world, e, TransitionState));
    ecs_fini(world);
}

void Transitions_entity_without_a_map_has_no_state(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Rgb, {10, 20, 30});
    test_assert(!ecs_has(world, e, TransitionState));
    ecs_fini(world);
}

void Transitions_first_assignment_does_not_move(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Rgb: {10, 20, 30}\n"
        "  Transition: [Rgb: {duration: 1}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    const TransitionState *state = ecs_get(world, e, TransitionState);
    test_assert(state != NULL);
    test_bool(state->dirty, false);

    Rgb value = *ecs_get(world, e, Rgb);
    box_transition_display(state, ecs_id(Rgb), 0.5, &value);
    test_int(value.r, 10);
    test_int(value.g, 20);
    test_int(value.b, 30);
    ecs_fini(world);
}

void Transitions_assignment_starts_a_transition(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Rgb: {10, 0, 0}\n"
        "  Transition: [Rgb: {duration: 1, timing_function: Linear}]\n"
        "}\n",
        NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Rgb, {110, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    test_bool(state->dirty, true);

    Rgb value = *ecs_get(world, e, Rgb);
    test_bool(box_transition_display(state, ecs_id(Rgb), 0.0, &value), true);
    test_int(value.r, 10);
    box_transition_display(state, ecs_id(Rgb), 0.5, &value);
    test_int(value.r, 60);
    ecs_fini(world);
}

void Transitions_transition_reaches_the_target(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Rgb: {10, 0, 0}\n"
        "  Transition: [Rgb: {duration: 1, timing_function: Linear}]\n"
        "}\n",
        NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Rgb, {110, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Rgb value = *ecs_get(world, e, Rgb);
    box_transition_display(state, ecs_id(Rgb), 1.0, &value);
    test_int(value.r, 110);
    box_transition_display(state, ecs_id(Rgb), 4.0, &value);
    test_int(value.r, 110);
    test_flt(state->until, 1.0);
    ecs_fini(world);
}

void Transitions_delay_holds_the_start_value(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, delay: 1, timing_function: Linear}] }\n",
        NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 0);
    box_transition_display(state, ecs_id(Position), 1.5, &value);
    test_flt(value.x, 50);
    box_transition_display(state, ecs_id(Position), 2.0, &value);
    test_flt(value.x, 100);
    ecs_fini(world);
}

void Transitions_assignment_replaces_the_transition_in_flight(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: Linear}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});
    ecs_progress(world, 0.5);
    ecs_set(world, e, Position, {200, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 50);
    box_transition_display(state, ecs_id(Position), 1.0, &value);
    test_flt(value.x, 125);
    box_transition_display(state, ecs_id(Position), 1.5, &value);
    test_flt(value.x, 200);
    ecs_fini(world);
}

void Transitions_assigned_twice_in_a_frame_starts_from_the_displayed_value(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {10, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: Linear}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});
    ecs_set(world, e, Position, {210, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.0, &value);
    test_flt(value.x, 10);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 110);
    ecs_fini(world);
}

void Transitions_linear_is_half_way_at_half_time(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: Linear}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.25, &value);
    test_flt(value.x, 25);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 50);
    ecs_fini(world);
}

void Transitions_ease_in_lags_at_half_time(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: EaseIn}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 25);
    ecs_fini(world);
}

void Transitions_ease_out_leads_at_half_time(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: EaseOut}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 75);
    ecs_fini(world);
}

void Transitions_ease_in_out_is_symmetric(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: EaseInOut}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.25, &value);
    test_flt(value.x, 12.5);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 50);
    box_transition_display(state, ecs_id(Position), 0.75, &value);
    test_flt(value.x, 87.5);
    ecs_fini(world);
}

void Transitions_ease_is_half_way_at_half_time(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: Ease}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.25, &value);
    test_flt(value.x, 15.625);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 50);
    ecs_fini(world);
}

void Transitions_prefab_map_is_inherited_by_an_instance(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "prefab Smooth {\n"
        "  Transition: [Position: {duration: 1, timing_function: Linear}]\n"
        "}\n"
        "e : Smooth {\n"
        "  Position: {0, 0, 0}\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(e != 0);
    test_assert(ecs_has(world, e, TransitionState));
    test_assert(ecs_get(world, e, Transition) != NULL);
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    test_int(ecs_vec_count(&state->records), 1);
    Position value = *ecs_get(world, e, Position);
    box_transition_display(state, ecs_id(Position), 0.5, &value);
    test_flt(value.x, 50);
    ecs_fini(world);
}

void Transitions_component_without_a_key_is_not_transitioned(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Position: {0, 0, 0}\n"
        "  Rgb: {0, 0, 0}\n"
        "  Transition: [Rgb: {duration: 1}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Position value = *ecs_get(world, e, Position);
    test_bool(box_transition_display(state, ecs_id(Position), 0.5, &value), false);
    test_flt(value.x, 100);
    test_bool(state->dirty, false);
    ecs_fini(world);
}

void Transitions_unsupported_component_has_no_layout(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_assert(ecs_has(world, ecs_id(Position), TransitionType));
    test_assert(!ecs_has(world, ecs_id(WorldTransform), TransitionType));

    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [WorldTransform: {duration: 1}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Position, {100, 0, 0});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    test_int(ecs_vec_count(&state->records), 0);
    test_bool(state->dirty, false);
    ecs_fini(world);
}

void Transitions_color_bytes_are_interpolated(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Rgb: {0, 0, 0}\n"
        "  Transition: [Rgb: {duration: 1, timing_function: Linear}]\n"
        "}\n",
        NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_set(world, e, Rgb, {200, 100, 50});

    const TransitionState *state = ecs_get(world, e, TransitionState);
    Rgb value = *ecs_get(world, e, Rgb);
    box_transition_display(state, ecs_id(Rgb), 0.5, &value);
    test_int(value.r, 100);
    test_int(value.g, 50);
    test_int(value.b, 25);
    ecs_fini(world);
}

void Transitions_transition_value_pair_is_published(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.transform\n"
        "using box.transition\n"
        "e { Position: {0, 0, 0}\n"
        "    Transition: [Position: {duration: 1, timing_function: Linear}] }\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    ecs_add_pair(world, e, ecs_id(TransitionValue), ecs_id(Position));
    ecs_set(world, e, Position, {100, 0, 0});
    ecs_progress(world, 0.5);

    const TransitionValue *published = ecs_get_pair(world, e, TransitionValue, ecs_id(Position));
    test_assert(published != NULL);
    test_flt(published->value[0], 50);
    ecs_fini(world);
}

void Transitions_removing_the_map_removes_the_state(void)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransition);
    test_int(ecs_script_run(world, "test",
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Rgb: {10, 20, 30}\n"
        "  Transition: [Rgb: {duration: 1}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    test_assert(ecs_has(world, e, TransitionState));
    ecs_remove(world, e, Transition);
    test_assert(!ecs_has(world, e, TransitionState));
    ecs_fini(world);
}
