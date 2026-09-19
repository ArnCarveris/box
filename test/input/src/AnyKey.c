/**
 * @file AnyKey.c
 * @brief Tests for the any key families a binding resolves on whatever key is down.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;

void AnyKey_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxInput);
}

void AnyKey_teardown(void)
{
    ecs_fini(w);
}

void AnyKey_a_key_fires_the_any_binding(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeySpace });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->pressed, true);
    test_bool(ecs_get(w, start, InputAction)->held, true);
}

void AnyKey_a_mouse_button_fires_the_any_binding(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseRight });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->pressed, true);
}

void AnyKey_a_modifier_fires_the_any_binding(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->pressed, true);
}

void AnyKey_the_event_carries_the_key_that_fired_it(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_entity_t binding = ecs_new_w_pair(w, EcsChildOf, title);
    ecs_set(w, binding, Binding, { .mask = BoxKeyAny, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyQ });
    ecs_progress(w, 0);

    const ActionEvent *events;
    int32_t i, count = box_input_events(w, &events), found = -1;
    for (i = 0; i < count && found < 0; i ++) {
        if (events[i].action == start && events[i].phase == ActionPressed) {
            found = i;
        }
    }

    test_assert(found >= 0);
    test_uint(events[found].mask, BoxKeyQ);
    test_uint(events[found].source, binding);
}

void AnyKey_the_key_that_went_down_this_frame_is_the_one_reported(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyA });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyZ });
    ecs_progress(w, 0);

    const ActionEvent *events;
    int32_t i, count = box_input_events(w, &events), found = -1;
    for (i = 0; i < count && found < 0; i ++) {
        if (events[i].action == start && events[i].phase == ActionHeld) {
            found = i;
        }
    }

    test_assert(found >= 0);
    test_uint(events[found].mask, BoxKeyZ);
}

void AnyKey_a_frame_without_a_key_does_not_fire(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start });

    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->pressed, false);
    test_bool(ecs_get(w, start, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeySpace, .duration = 1 });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, true);

    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, false);
    test_bool(ecs_get(w, start, InputAction)->released, true);
}

void AnyKey_the_key_family_leaves_the_mouse_buttons_alone(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAnyKey, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEnter });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, true);
}

void AnyKey_the_mouse_family_leaves_the_keys_alone(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t fire = ecs_entity(w, { .name = "actions.Fire" });
    ecs_add(w, fire, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAnyMouse, .action = fire });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEnter, .duration = 1 });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, fire, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseRight });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, fire, InputAction)->held, true);
}

void AnyKey_a_binding_with_a_modifier_needs_the_modifier(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BOX_KEY_MOD_CTRL | BoxKeyAny, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyQ, .duration = 1 });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyQ });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, true);
}

void AnyKey_a_higher_map_that_consumes_keeps_the_key_from_an_any_binding(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, close, InputAction);
    ecs_add(w, start, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, ui, InputMap, { .enabled = true, .priority = 1 });
    ecs_set(w, title, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close, .consume = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, start, InputAction)->held, false);
}

void AnyKey_an_any_binding_that_consumes_keeps_its_key_from_a_lower_map(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, start, InputAction);
    ecs_add(w, forward, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, title, InputMap, { .enabled = true, .priority = 1 });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start, .consume = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, true);
    test_bool(ecs_get(w, forward, InputAction)->held, false);
}

void AnyKey_a_key_pair_of_a_map_binds_the_any_family(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t any = ecs_lookup(w, "box.input.keys.Any");
    test_assert(any != 0);
    test_int(ecs_get_pair_second(w, any, EcsConstant, ecs_i32_t)[0], BoxKeyAny);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_add_pair(w, title, any, start);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyF1 });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, start, InputAction)->held, true);
}

void AnyKey_the_binding_list_of_the_action_holds_the_family(void)
{
    // ore_engine/any_key_binding
    ecs_entity_t start = ecs_entity(w, { .name = "actions.Start" });
    ecs_add(w, start, InputAction);
    ecs_entity_t title = ecs_entity(w, { .name = "title_map" });
    ecs_set(w, title, InputMap, { .enabled = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, title), Binding, {
        .mask = BoxKeyAny, .action = start });

    uint64_t masks[4];
    test_int(box_input_bindings(w, start, masks, 4), 1);
    test_uint(masks[0], BoxKeyAny);

    char *name = box_input_mask_name(masks[0]);
    test_str(name, "Any");
    ecs_os_free(name);
}
