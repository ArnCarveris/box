/**
 * @file Actions.c
 * @brief Tests for the action layer: bindings, modifiers, emitting and the events.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;

void Actions_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxInput);
}

void Actions_teardown(void)
{
    ecs_fini(w);
}

void Actions_a_binding_presses_holds_and_releases(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_entity_t bind = ecs_entity(w, { .name = "box.input.map.forward" });
    ecs_set(w, bind, Binding, { .mask = BoxKeyW, .action = forward });

    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .duration = 2 });
    ecs_progress(w, 0);
    const InputAction *a = ecs_get(w, forward, InputAction);
    test_bool(a->pressed, true);
    test_bool(a->held, true);
    test_bool(a->released, false);
    test_flt(a->value, 1.0);

    ecs_progress(w, 0);
    a = ecs_get(w, forward, InputAction);
    test_bool(a->pressed, false);
    test_bool(a->held, true);

    ecs_progress(w, 0);
    a = ecs_get(w, forward, InputAction);
    test_bool(a->held, false);
    test_bool(a->released, true);
    test_flt(a->value, 0.0);

    ecs_progress(w, 0);
    a = ecs_get(w, forward, InputAction);
    test_bool(a->released, false);
}

void Actions_a_modifier_binding_matches_its_side(void)
{
    // input/action_layer
    ecs_entity_t save = ecs_entity(w, { .name = "actions.Save" });
    ecs_add(w, save, InputAction);
    ecs_entity_t bind = ecs_new(w);
    ecs_set(w, bind, Binding, {
        .mask = BOX_KEY_MOD_CTRL_LEFT | BoxKeyS, .action = save });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlRight });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyS });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyUp, .code = BoxKeyCtrlRight });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->held, true);
    test_bool(ecs_get(w, save, InputAction)->pressed, true);
}

void Actions_a_family_modifier_matches_either_side(void)
{
    // input/action_layer
    ecs_entity_t save = ecs_entity(w, { .name = "actions.Save" });
    ecs_add(w, save, InputAction);
    ecs_entity_t bind = ecs_new(w);
    ecs_set(w, bind, Binding, {
        .mask = BOX_KEY_MOD_CTRL | BoxKeyS, .action = save });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlRight });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyS });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->held, true);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyUp, .code = BoxKeyCtrlRight });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->held, true);
}

void Actions_a_more_specific_binding_wins(void)
{
    // input/action_layer
    ecs_entity_t save = ecs_entity(w, { .name = "actions.Save" });
    ecs_entity_t stop = ecs_entity(w, { .name = "actions.Stop" });
    ecs_add(w, save, InputAction);
    ecs_add(w, stop, InputAction);
    ecs_set(w, ecs_new(w), Binding, {
        .mask = BOX_KEY_MOD_CTRL | BoxKeyS, .action = save });
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyS, .action = stop });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyS });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, stop, InputAction)->held, true);
    test_bool(ecs_get(w, save, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, stop, InputAction)->held, false);
    test_bool(ecs_get(w, save, InputAction)->held, true);
}

void Actions_a_pure_modifier_binding_fires_while_it_is_held(void)
{
    // input/action_layer
    ecs_entity_t run = ecs_entity(w, { .name = "actions.Run" });
    ecs_add(w, run, InputAction);
    ecs_set(w, ecs_new(w), Binding, {
        .mask = BoxKeyShiftLeft, .action = run });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, run, InputAction)->pressed, true);
    test_bool(ecs_get(w, run, InputAction)->held, true);

    ecs_progress(w, 0);
    test_bool(ecs_get(w, run, InputAction)->held, false);
    test_bool(ecs_get(w, run, InputAction)->released, true);
}

void Actions_a_key_binding_claims_the_modifier_of_a_pure_binding(void)
{
    // input/action_layer
    ecs_entity_t run = ecs_entity(w, { .name = "actions.Run" });
    ecs_entity_t save = ecs_entity(w, { .name = "actions.Save" });
    ecs_add(w, run, InputAction);
    ecs_add(w, save, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyCtrl, .action = run });
    ecs_set(w, ecs_new(w), Binding, {
        .mask = BOX_KEY_MOD_CTRL | BoxKeyS, .action = save });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, run, InputAction)->held, true);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyS });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->held, true);
    test_bool(ecs_get(w, run, InputAction)->held, false);
    test_bool(ecs_get(w, run, InputAction)->released, true);
}

void Actions_two_actions_share_one_mask(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_entity_t fire = ecs_entity(w, { .name = "actions.Fire" });
    ecs_add(w, forward, InputAction);
    ecs_add(w, fire, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = fire });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);
    test_bool(ecs_get(w, fire, InputAction)->held, true);
}

void Actions_two_masks_share_one_action(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyUp, .action = forward });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyUp, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->pressed, true);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);
    test_bool(ecs_get(w, forward, InputAction)->pressed, false);
    test_bool(ecs_get(w, forward, InputAction)->released, false);
}

void Actions_rebinding_moves_the_action_to_another_key(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_entity_t bind = ecs_new(w);
    ecs_set(w, bind, Binding, { .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);

    ecs_progress(w, 0);
    ecs_set(w, bind, Binding, { .mask = BoxKeyUp, .action = forward });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyUp });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);
}

void Actions_unbinding_releases_the_action(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_entity_t bind = ecs_new(w);
    ecs_set(w, bind, Binding, { .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);

    ecs_delete(w, bind);
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, false);
    test_bool(ecs_get(w, forward, InputAction)->released, true);
}

void Actions_a_disabled_map_stops_its_bindings(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_entity_t map = ecs_entity(w, { .name = "map" });
    ecs_add(w, map, InputMap);
    test_bool(ecs_get(w, map, InputMap)->enabled, true);
    ecs_entity_t bind = ecs_entity(w, { .name = "map.forward" });
    ecs_set(w, bind, Binding, { .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);

    ecs_set(w, map, InputMap, { .enabled = false });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, false);
}

void Actions_a_key_pair_on_a_map_binds_an_action(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_entity_t map = ecs_lookup(w, "box.input.map");
    test_assert(map != 0);
    ecs_entity_t key = ecs_lookup(w, "box.input.keys.W");
    test_assert(key != 0);
    ecs_add_pair(w, map, key, forward);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);

    ecs_remove_pair(w, map, key, forward);
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, false);
}

void Actions_emitting_fires_an_action_without_a_key(void)
{
    // input/action_layer
    ecs_entity_t fire = ecs_entity(w, { .name = "actions.Fire" });
    ecs_add(w, fire, InputAction);
    ecs_entity_t button = ecs_entity(w, { .name = "ui.button" });

    ecs_progress(w, 0);
    box_input_emit(w, fire, button);
    const InputAction *a = ecs_get(w, fire, InputAction);
    test_bool(a->pressed, true);
    test_bool(a->released, true);
    test_bool(a->held, false);
    test_flt(a->value, 1.0);

    const ActionEvent *events;
    int32_t count = box_input_events(w, &events);
    test_int(count, 2);
    test_uint(events[0].action, fire);
    test_int(events[0].phase, ActionPressed);
    test_uint(events[0].source, button);
    test_uint(events[0].mask, 0);
    test_int(events[1].phase, ActionReleased);

    ecs_progress(w, 0);
    a = ecs_get(w, fire, InputAction);
    test_bool(a->pressed, false);
    test_bool(a->released, false);
    test_int(box_input_events(w, &events), 0);
}

void Actions_the_event_list_holds_the_phases_of_the_frame(void)
{
    // input/action_layer
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_entity_t bind = ecs_new(w);
    ecs_set(w, bind, Binding, { .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .duration = 1 });
    ecs_progress(w, 0);
    const ActionEvent *events;
    int32_t count = box_input_events(w, &events);
    test_int(count, 2);
    test_uint(events[0].action, forward);
    test_int(events[0].phase, ActionPressed);
    test_uint(events[0].source, bind);
    test_uint(events[0].mask, BoxKeyW);
    test_int(events[1].phase, ActionHeld);

    ecs_progress(w, 0);
    count = box_input_events(w, &events);
    test_int(count, 1);
    test_int(events[0].phase, ActionReleased);
}

void Actions_a_script_authors_the_map(void)
{
    // input/action_layer
    const char *code =
        "using box.input\n"
        "actions {\n"
        "  Forward { InputAction: {description: \"Move forward\"} }\n"
        "  Save { InputAction: {description: \"Save the scene\"} }\n"
        "}\n"
        "input_map {\n"
        "  InputMap\n"
        "  (keys.W, actions.Forward)\n"
        "  save { Binding: {mask: keys.Ctrl|keys.S, action: actions.Save} }\n"
        "}\n";
    test_int(ecs_script_run(w, "input_map", code, NULL), 0);

    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    ecs_entity_t save = ecs_lookup(w, "actions.Save");
    test_assert(forward != 0);
    test_assert(save != 0);
    test_str(ecs_get(w, forward, InputAction)->description, "Move forward");

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);
    test_bool(ecs_get(w, save, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyS });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, save, InputAction)->held, true);
}

void Actions_the_scene_map_binds_its_actions(void)
{
    // input/action_layer
    test_int(ecs_script_run_file(
        w, "../../apps/box_app/etc/scenes/input_map.flecs"), 0);
    ecs_entity_t forward = ecs_lookup(w, "actions.Forward");
    ecs_entity_t run = ecs_lookup(w, "actions.Run");
    test_assert(forward != 0);
    test_assert(run != 0);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->held, true);
    test_bool(ecs_get(w, run, InputAction)->held, true);
}
