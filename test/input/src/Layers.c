/**
 * @file Layers.c
 * @brief Tests for the map layers, the raise a handler stops and the drag of an action.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;

void Layers_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
}

void Layers_teardown(void)
{
    ecs_fini(w);
}

void Layers_a_binding_that_consumes_keeps_the_key_from_a_lower_map(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, { .enabled = true, .priority = 1 });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close, .consume = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyEscape, .action = cancel });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, cancel, InputAction)->held, false);
}

void Layers_a_higher_map_that_does_not_consume_leaves_the_lower_one_alone(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, { .enabled = true, .priority = 1 });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyEscape, .action = cancel });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, cancel, InputAction)->held, true);
}

void Layers_a_map_that_consumes_covers_its_key_pairs(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t escape = ecs_lookup(w, "box.input.keys.Escape");
    test_assert(escape != 0);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, {
        .enabled = true, .priority = 1, .consume = true });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_add_pair(w, ui, escape, close);
    ecs_add_pair(w, game, escape, cancel);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, cancel, InputAction)->held, false);
}

void Layers_a_disabled_consuming_map_lets_the_key_through(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, {
        .enabled = false, .priority = 1, .consume = true });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyEscape, .action = cancel });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, false);
    test_bool(ecs_get(w, cancel, InputAction)->held, true);
}

void Layers_a_consuming_map_keeps_only_the_keys_it_binds(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, close, InputAction);
    ecs_add(w, forward, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, {
        .enabled = true, .priority = 1, .consume = true });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, forward, InputAction)->held, true);
}

void Layers_maps_of_one_priority_both_resolve(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t one = ecs_entity(w, { .name = "map_one" });
    ecs_entity_t two = ecs_entity(w, { .name = "map_two" });
    ecs_set(w, one, InputMap, {
        .enabled = true, .priority = 2, .consume = true });
    ecs_set(w, two, InputMap, {
        .enabled = true, .priority = 2, .consume = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, one), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, two), Binding, {
        .mask = BoxKeyEscape, .action = cancel });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, cancel, InputAction)->held, true);
}

void Layers_a_binding_without_a_map_sits_at_the_base_layer(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_set(w, ui, InputMap, {
        .enabled = true, .priority = 1, .consume = true });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new(w), Binding, {
        .mask = BoxKeyEscape, .action = cancel });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, cancel, InputAction)->held, false);
}

void Layers_the_events_of_a_frame_start_at_the_highest_map(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, { .enabled = true, .priority = 3 });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyEscape, .action = cancel });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    const ActionEvent *events;
    int32_t i, count = box_input_events(w, &events), first = -1, last = -1;
    for (i = 0; i < count; i ++) {
        if (events[i].action == close && first < 0) {
            first = i;
        }

        if (events[i].action == cancel) {
            last = i;
        }
    }

    test_assert(first >= 0);
    test_assert(last > first);
    test_int(events[first].priority, 3);
    test_int(events[last].priority, 0);
}

void Layers_consuming_an_action_takes_its_key_from_the_maps_below(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, { .enabled = true, .priority = 1 });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyEscape, .action = cancel });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, cancel, InputAction)->held, true);

    test_bool(box_input_consume(w, close), true);
    test_bool(ecs_get(w, close, InputAction)->held, false);
    test_bool(ecs_get(w, cancel, InputAction)->held, false);
}

void Layers_consuming_leaves_the_maps_above_and_beside_it_alone(void)
{
    // ore_engine/input_layers
    ecs_entity_t close = ecs_entity(w, { .name = "actions.Close" });
    ecs_entity_t cancel = ecs_entity(w, { .name = "actions.Cancel" });
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, close, InputAction);
    ecs_add(w, cancel, InputAction);
    ecs_add(w, forward, InputAction);
    ecs_entity_t ui = ecs_entity(w, { .name = "ui_map" });
    ecs_entity_t game = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, ui, InputMap, { .enabled = true, .priority = 1 });
    ecs_set(w, game, InputMap, { .enabled = true, .priority = 0 });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, ui), Binding, {
        .mask = BoxKeyEscape, .action = close });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyEscape, .action = cancel });
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, game), Binding, {
        .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);

    test_bool(box_input_consume(w, cancel), true);
    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, forward, InputAction)->held, true);
}

void Layers_a_script_handler_consumes_the_key_of_the_map_below(void)
{
    // ore_engine/input_layers
    ecs_entity_t script = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "Stopped {}\n"
        "actions {\n"
        "  Close { InputAction }\n"
        "  Cancel { InputAction }\n"
        "}\n"
        "ui_map { InputMap: {enabled: true, priority: 1}\n"
        "  (keys.Escape, actions.Close)\n"
        "}\n"
        "game_map { InputMap: {enabled: true, priority: 0}\n"
        "  (keys.Escape, actions.Cancel)\n"
        "}\n"
        "template Panel {\n"
        "  mut taken = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Close)\n"
        "    const ok = input.consume(actions.Close)\n"
        "    taken = 1\n"
        "  }\n"
        "  if taken == 1 {\n"
        "    Stopped\n"
        "  }\n"
        "}\n"
        "Panel panel\n" });
    test_assert(script != 0);

    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    ecs_entity_t cancel = ecs_lookup(w, "actions.Cancel");
    test_assert(ecs_has_id(w, ecs_lookup(w, "panel"), ecs_lookup(w, "Stopped")));
    test_bool(ecs_get(w, cancel, InputAction)->held, false);
}

void Layers_a_script_handler_that_does_not_consume_leaves_the_map_below(void)
{
    // ore_engine/input_layers
    ecs_entity_t script = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions {\n"
        "  Close { InputAction }\n"
        "  Cancel { InputAction }\n"
        "}\n"
        "ui_map { InputMap: {enabled: true, priority: 1}\n"
        "  (keys.Escape, actions.Close)\n"
        "}\n"
        "game_map { InputMap: {enabled: true, priority: 0}\n"
        "  (keys.Escape, actions.Cancel)\n"
        "}\n"
        "template Panel {\n"
        "  mut seen = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Close)\n"
        "    seen = 1\n"
        "  }\n"
        "  InputMap: {enabled: false}\n"
        "}\n"
        "Panel panel\n" });
    test_assert(script != 0);

    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEscape });
    ecs_progress(w, 0);

    ecs_entity_t close = ecs_lookup(w, "actions.Close");
    ecs_entity_t cancel = ecs_lookup(w, "actions.Cancel");
    test_bool(ecs_get(w, close, InputAction)->held, true);
    test_bool(ecs_get(w, cancel, InputAction)->held, true);
}

void Layers_stopping_outside_a_raise_does_nothing(void)
{
    // ore_engine/input_layers
    test_bool(box_event_stop(w), false);
    test_assert(box_event_current(w) != NULL);
    test_uint(box_event_current(w)->target, 0);
    test_bool(box_event_current(w)->consumed, false);
}

void Layers_consuming_an_action_clears_it_for_the_frame(void)
{
    // ore_engine/input_layers
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->pressed, true);

    test_bool(box_input_consume(w, forward), true);
    const InputAction *a = ecs_get(w, forward, InputAction);
    test_bool(a->pressed, false);
    test_bool(a->held, false);
    test_bool(a->released, false);
    test_flt(a->value, 0.0);
}

void Layers_consuming_drops_the_events_of_the_frame(void)
{
    // ore_engine/input_layers
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_entity_t back = ecs_entity(w, { .name = "actions.Back" });
    ecs_add(w, forward, InputAction);
    ecs_add(w, back, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyS, .action = back });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyS });
    ecs_progress(w, 0);
    box_input_consume(w, forward);

    const ActionEvent *events;
    int32_t i, count = box_input_events(w, &events), mine = 0, other = 0;
    for (i = 0; i < count; i ++) {
        if (events[i].action == forward) {
            mine ++;
        } else if (events[i].action == back) {
            other ++;
        }
    }

    test_int(mine, 0);
    test_assert(other > 0);
}

void Layers_consuming_a_held_action_does_not_press_it_again(void)
{
    // ore_engine/input_layers
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    box_input_consume(w, forward);

    ecs_progress(w, 0);
    const InputAction *a = ecs_get(w, forward, InputAction);
    test_bool(a->pressed, false);
    test_bool(a->held, true);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyUp, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, forward, InputAction)->released, true);
}

void Layers_consuming_an_idle_action_reports_nothing(void)
{
    // ore_engine/input_layers
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });
    ecs_progress(w, 0);

    test_bool(box_input_consume(w, forward), false);
    test_bool(ecs_get(w, forward, InputAction)->held, false);
}

void Layers_a_drag_raises_after_the_threshold(void)
{
    // ore_engine/input_layers
    ecs_entity_t place = ecs_entity(w, { .name = "actions.Place" });
    ecs_add(w, place, InputAction);
    ecs_set(w, ecs_new(w), Binding, {
        .mask = BoxKeyMouseLeft, .action = place });

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {100, 100} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft });
    ecs_progress(w, 0);

    const ActionEvent *events;
    int32_t i, count = box_input_events(w, &events), drags = 0;
    for (i = 0; i < count; i ++) {
        drags += events[i].phase == ActionDragged;
    }

    test_int(drags, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {102, 100} });
    ecs_progress(w, 0);
    count = box_input_events(w, &events);
    for (i = 0, drags = 0; i < count; i ++) {
        drags += events[i].phase == ActionDragged;
    }

    test_int(drags, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {140, 100} });
    ecs_progress(w, 0);
    count = box_input_events(w, &events);
    for (i = 0, drags = 0; i < count; i ++) {
        drags += events[i].phase == ActionDragged;
    }

    test_int(drags, 1);
}

void Layers_a_drag_stops_when_the_action_is_released(void)
{
    // ore_engine/input_layers
    ecs_entity_t place = ecs_entity(w, { .name = "actions.Place" });
    ecs_add(w, place, InputAction);
    ecs_set(w, ecs_new(w), Binding, {
        .mask = BoxKeyMouseLeft, .action = place });

    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {100, 100} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {200, 100} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputButtonUp, .code = BoxKeyMouseLeft });
    ecs_progress(w, 0);

    const ActionEvent *events;
    int32_t i, count = box_input_events(w, &events), drags = 0;
    for (i = 0; i < count; i ++) {
        drags += events[i].phase == ActionDragged;
    }

    test_int(drags, 0);
}

void Layers_a_script_follows_a_drag_with_on_dragged(void)
{
    // ore_engine/input_layers
    ecs_entity_t script = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Place { InputAction } }\n"
        "map { InputMap: {enabled: true}\n"
        "  (keys.MouseLeft, actions.Place)\n"
        "}\n"
        "template Painter {\n"
        "  mut sx = 0\n"
        "  mut cx = 0\n"
        "  async {\n"
        "    const d = await on.dragged(actions.Place)\n"
        "    sx = d.start.window.x\n"
        "    cx = d.cursor.window.x\n"
        "  }\n"
        "  Mouse: {window: {sx, cx}}\n"
        "}\n"
        "Painter painter\n" });
    test_assert(script != 0);

    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {120, 80} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {320, 80} });
    ecs_progress(w, 0);

    ecs_entity_t painter = ecs_lookup(w, "painter");
    test_assert(painter != 0);
    test_flt(ecs_get(w, painter, Mouse)->window.x, 120);
    test_flt(ecs_get(w, painter, Mouse)->window.y, 320);
}

void Layers_a_script_consumes_an_action(void)
{
    // ore_engine/input_layers
    ecs_entity_t forward = ecs_entity(w, { .name = "actions.Forward" });
    ecs_add(w, forward, InputAction);
    ecs_set(w, ecs_new(w), Binding, { .mask = BoxKeyW, .action = forward });

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);

    test_int(ecs_script_run(w, "test",
        "using box.input\n"
        "Taken {}\n"
        "const taken = input.consume(actions.Forward)\n"
        "if taken {\n"
        "  probe { Taken }\n"
        "}\n", NULL), 0);

    ecs_entity_t probe = ecs_lookup(w, "probe");
    test_assert(probe != 0);
    test_assert(ecs_has_id(w, probe, ecs_lookup(w, "Taken")));
    test_bool(ecs_get(w, forward, InputAction)->held, false);
}
