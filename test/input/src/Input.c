/**
 * @file Input.c
 * @brief Tests for the per frame key, text and mouse state of the Input singleton.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;

void Input_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
}

void Input_teardown(void)
{
    ecs_fini(w);
}

void Input_defaults_of_the_singleton(void)
{
    // input/device_layer
    const Input *in = ecs_singleton_get(w, Input);
    test_assert(in != NULL);
    test_bool(in->focused, true);
    test_bool(in->minimized, false);
    test_int(in->text_len, 0);
    int32_t i;
    for (i = 0; i < BOX_KEY_COUNT; i ++) {
        test_bool(in->keys[i].pressed, false);
        test_bool(in->keys[i].state, false);
        test_bool(in->keys[i].current, false);
    }

    test_flt(in->mouse.window.x, 0);
    test_flt(in->mouse.scroll.y, 0);
}

void Input_press_is_set_for_one_frame(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].pressed, true);
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Input)->keys[BoxKeyW].pressed, false);
}

void Input_held_key_keeps_its_state(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeySpace });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeySpace].pressed, false);
    test_bool(in->keys[BoxKeySpace].state, true);
    test_bool(in->keys[BoxKeySpace].current, true);
}

void Input_release_clears_the_state(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyA });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputKeyUp, .code = BoxKeyA });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyA].pressed, false);
    test_bool(in->keys[BoxKeyA].state, false);
    test_bool(in->keys[BoxKeyA].current, false);
}

void Input_press_and_release_in_one_frame_keeps_the_press(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyE });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyUp, .code = BoxKeyE });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_bool(in->keys[BoxKeyE].pressed, true);
    test_bool(in->keys[BoxKeyE].state, false);
}

void Input_mouse_buttons_use_their_own_slots(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseRight });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_int(BoxKeyMouseRight, 129);
    test_bool(in->keys[BoxKeyMouseRight].pressed, true);
    test_bool(in->keys[BoxKeyMouseLeft].state, false);
}

void Input_text_buffer_holds_the_characters_of_the_frame(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'b' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'o' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'x' });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_int(in->text_len, 3);
    test_int(in->text[0], 'b');
    test_int(in->text[1], 'o');
    test_int(in->text[2], 'x');
    ecs_progress(w, 0);
    test_int(ecs_singleton_get(w, Input)->text_len, 0);
}

void Input_text_buffer_encodes_utf8(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 0x20ac });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_int(in->text_len, 3);
    test_int((unsigned char)in->text[0], 0xe2);
    test_int((unsigned char)in->text[1], 0x82);
    test_int((unsigned char)in->text[2], 0xac);
}

void Input_text_buffer_stops_at_its_size(void)
{
    // input/device_layer
    int32_t i;
    for (i = 0; i < BOX_TEXT_MAX + 10; i ++) {
        box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'a' });
    }

    ecs_progress(w, 0);
    test_int(ecs_singleton_get(w, Input)->text_len, BOX_TEXT_MAX);
}

void Input_scroll_accumulates_over_frames(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, 1.5f} });
    box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0.5f, 1.0f} });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.scroll_delta.x, 0.5f);
    test_flt(in->mouse.scroll_delta.y, 2.5f);
    test_flt(in->mouse.scroll.y, 2.5f);
    box_input_inject(w, &(InputEvent){ .kind = InputScroll, .value = {0, -1.0f} });
    ecs_progress(w, 0);
    in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.scroll_delta.y, -1.0f);
    test_flt(in->mouse.scroll.y, 1.5f);
}

void Input_mouse_window_position_follows_the_events(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {12, 34} });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.window.x, 12);
    test_flt(in->mouse.window.y, 34);
    test_flt(in->mouse.delta.x, 0);
    test_flt(in->mouse.delta.y, 0);
}

void Input_mouse_delta_is_the_movement_of_the_frame(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {100, 100} });
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {110, 120} });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.delta.x, 10);
    test_flt(in->mouse.delta.y, 20);
    ecs_progress(w, 0);
    in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.delta.x, 0);
    test_flt(in->mouse.window.x, 110);
}

void Input_mouse_view_and_normalized_come_from_the_surface(void)
{
    // input/device_layer
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 800;
    s->height = 600;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {200, 150} });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.window.x, 200);
    test_flt(in->mouse.window.y, 150);
    test_flt(in->mouse.view.x, -200);
    test_flt(in->mouse.view.y, 150);
    test_flt(in->mouse.normalized.x, -0.5f);
    test_flt(in->mouse.normalized.y, 0.5f);
}

void Input_mouse_center_is_the_origin_of_the_view(void)
{
    // input/device_layer
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 640;
    s->height = 480;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {320, 240} });
    ecs_progress(w, 0);
    const Input *in = ecs_singleton_get(w, Input);
    test_flt(in->mouse.view.x, 0);
    test_flt(in->mouse.view.y, 0);
    test_flt(in->mouse.normalized.x, 0);
    test_flt(in->mouse.normalized.y, 0);
}
