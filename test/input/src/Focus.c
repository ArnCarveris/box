/**
 * @file Focus.c
 * @brief Tests that real input is ignored while the window is away and never phantoms.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;

void Focus_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
}

void Focus_teardown(void)
{
    ecs_fini(w);
}

void Focus_a_new_world_is_focused(void)
{
    // input/device_layer
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->focused, true);
    test_bool(in->minimized, false);
}

void Focus_focus_loss_clears_the_key_state(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].state, true);
    box_input_inject(w, &(InputEvent){ .kind = InputFocus, .code = 0 });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->focused, false);
    test_bool(in->keys[BoxKeyW].state, false);
    test_bool(in->keys[BoxKeyW].current, false);
    test_bool(in->keys[BoxKeyMouseLeft].state, false);
}

void Focus_refocus_presses_nothing(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputFocus, .code = 0 });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputFocus, .code = 1 });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->focused, true);
    test_bool(in->keys[BoxKeyW].pressed, false);
    test_bool(in->keys[BoxKeyW].state, false);
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].pressed, false);
}

void Focus_refocus_does_not_jump_the_cursor(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {20, 30} });
    ecs_progress(w, 0);
    test_flt(ecs_singleton_get(w, Input)->mouse.delta.y, 20);
    box_input_inject(w, &(InputEvent){ .kind = InputFocus, .code = 0 });
    box_input_inject(w, &(InputEvent){ .kind = InputFocus, .code = 1 });
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {500, 400} });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.window.x, 500);
    test_flt(in->mouse.window.y, 400);
    test_flt(in->mouse.delta.x, 0);
    test_flt(in->mouse.delta.y, 0);
}

void Focus_platform_keys_are_ignored_while_unfocused(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputFocus, .code = 0 });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW, .source = InputSourcePlatform });
    box_input_inject(w, &(InputEvent){
        .kind = InputChar, .code = 'w', .source = InputSourcePlatform });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyW].pressed, false);
    test_bool(in->keys[BoxKeyW].state, false);
    test_int(in->text_len, 0);
}

void Focus_platform_keys_are_ignored_while_minimized(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputMinimize, .code = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->minimized, true);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyS, .source = InputSourcePlatform });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyS].state, false);
}

void Focus_injected_keys_are_applied_while_unfocused(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputFocus, .code = 0 });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyD });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->focused, false);
    test_bool(in->keys[BoxKeyD].pressed, true);
    test_bool(in->keys[BoxKeyD].state, true);
}

void Focus_minimize_event_sets_and_clears_the_flag(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputMinimize, .code = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->minimized, true);
    box_input_inject(w, &(InputEvent){
        .kind = InputMinimize, .code = 0, .source = InputSourcePlatform });
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyF, .source = InputSourcePlatform });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->minimized, false);
    test_bool(in->keys[BoxKeyF].pressed, true);
}
