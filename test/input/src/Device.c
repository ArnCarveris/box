/**
 * @file Device.c
 * @brief Keyboard and mouse device state driven by the synthetic queue and a fake platform.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>

static InputEvent platform_events[8];
static int32_t platform_count = 0;
static int32_t platform_polls = 0;
static int32_t platform_frees = 0;

static void PlatformPoll(ecs_world_t *world, void *ctx) {
    (void)ctx;
    platform_polls ++;
    for (int32_t i = 0; i < platform_count; i ++) {
        box_input_platform_push(world, &platform_events[i]);
    }

    platform_count = 0;
}

static void PlatformFree(void *ctx) {
    (void)ctx;
    platform_frees ++;
}

void Device_import(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_lookup(world, "box.input") != 0);
    test_assert(ecs_lookup(world, "box.input.screen") != 0);
    test_assert(ecs_lookup(world, "box.input.Input") == ecs_id(Input));
    test_assert(ecs_lookup(world, "box.input.Key.MouseLeft") != 0);
    test_assert(ecs_lookup(world, "flecs.script.on.key_down") != 0);

    const Input *in = ecs_singleton_get(world, Input);
    test_assert(in != NULL);
    test_bool(in->focused, true);
    test_bool(in->keys[KeyA].down, false);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Device_key_press(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key_press(world, KeyA, 0);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].down, true);
    test_bool(in->keys[KeyA].pressed, true);
    test_bool(in->keys[KeyA].released, false);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].down, false);
    test_bool(in->keys[KeyA].pressed, false);
    test_bool(in->keys[KeyA].released, true);
    test_int(box_input_pending(world), 0);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].down, false);
    test_bool(in->keys[KeyA].released, false);

    ecs_fini(world);
}

void Device_key_press_frames(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key_press(world, KeyW, 3);

    for (int32_t i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        const Input *in = ecs_singleton_get(world, Input);
        test_bool(in->keys[KeyW].down, true);
        test_bool(in->keys[KeyW].pressed, i == 0);
    }

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, false);
    test_bool(in->keys[KeyW].released, true);

    ecs_fini(world);
}

void Device_key_down_up(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key(world, KeySpace, true);
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeySpace].down, true);
    test_bool(in->keys[KeySpace].pressed, false);

    box_input_key(world, KeySpace, false);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeySpace].down, false);
    test_bool(in->keys[KeySpace].released, true);

    ecs_fini(world);
}

void Device_key_down_held(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key(world, KeyD, true);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyD].pressed, true);

    box_input_key(world, KeyD, true);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyD].down, true);
    test_bool(in->keys[KeyD].pressed, false);

    ecs_fini(world);
}

void Device_key_press_sequence(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key_press(world, KeyA, 1);
    box_input_key_press(world, KeyB, 1);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].pressed, true);
    test_bool(in->keys[KeyB].down, false);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].released, true);
    test_bool(in->keys[KeyB].pressed, true);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyB].released, true);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Device_key_type(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(box_input_key_type(world, "Hi", 1), 2);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_str(in->text, "H");
    test_bool(in->keys[KeyH].pressed, true);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_str(in->text, "i");
    test_bool(in->keys[KeyH].released, true);
    test_bool(in->keys[KeyI].pressed, true);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_str(in->text, "");
    test_bool(in->keys[KeyI].released, true);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Device_key_type_no_key(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(box_input_key_type(world, "\xc3\xa9x", 2), 2);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_str(in->text, "\xc3\xa9");
    test_bool(in->keys[KeyE].down, false);

    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->text, "");

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_str(in->text, "x");
    test_bool(in->keys[KeyX].pressed, true);

    ecs_fini(world);
}

void Device_key_from_name(void) {
    // input/devices
    test_int(box_input_key_from_name("W"), KeyW);
    test_int(box_input_key_from_name("w"), KeyW);
    test_int(box_input_key_from_name("space"), KeySpace);
    test_int(box_input_key_from_name(" "), KeySpace);
    test_int(box_input_key_from_name("MouseLeft"), KeyMouseLeft);
    test_int(box_input_key_from_name("ArrowLeft"), KeyLeft);
    test_int(box_input_key_from_name("Control"), KeyCtrlLeft);
    test_int(box_input_key_from_name("1"), KeyNum1);
    test_int(box_input_key_from_name("Kp1"), KeyKp1);
    test_int(box_input_key_from_name("/"), KeySlash);
    test_int(box_input_key_from_name("F12"), KeyF12);
    test_int(box_input_key_from_name("bogus"), KeyNone);
    test_int(box_input_key_from_name(""), KeyNone);
    test_int(box_input_key_from_name(NULL), KeyNone);
}

void Device_key_name(void) {
    // input/devices
    test_str(box_input_key_name(KeyA), "A");
    test_str(box_input_key_name(KeyPageDown), "PageDown");
    test_str(box_input_key_name(KeyMouseRight), "MouseRight");
    test_assert(box_input_key_name((Key)BOX_KEY_COUNT) == NULL);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    for (int32_t i = 1; i < BOX_KEY_COUNT; i ++) {
        char path[64];
        snprintf(path, sizeof(path), "box.input.Key.%s", box_input_key_name((Key)i));
        ecs_entity_t c = ecs_lookup(world, path);
        test_assert(c != 0);
        const int32_t *value = ecs_get_pair_second(world, c, EcsConstant, ecs_i32_t);
        test_assert(value != NULL);
        test_int(*value, i);
    }

    ecs_fini(world);
}

void Device_mouse_move(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_move(world, 10, 20, false);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 10);
    test_flt(in->mouse.position[1], 20);
    test_flt(in->mouse.delta[0], 0);
    test_bool(in->mouse.pinned, true);

    box_input_mouse_move(world, 15, 27, false);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 15);
    test_flt(in->mouse.position[1], 27);
    test_flt(in->mouse.delta[0], 5);
    test_flt(in->mouse.delta[1], 7);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.delta[0], 0);
    test_flt(in->mouse.delta[1], 0);
    test_flt(in->mouse.position[0], 15);

    ecs_fini(world);
}

void Device_mouse_move_normalized(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_queue(world, &(InputEvent){ .kind = InputResize, .value = {200, 100} });
    box_input_mouse_move(world, 0.5f, 0.25f, true);
    ecs_progress(world, 0);

    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->size[0], 200);
    test_flt(in->size[1], 100);
    test_flt(in->mouse.position[0], 100);
    test_flt(in->mouse.position[1], 25);
    test_flt(in->mouse.normalized[0], 0.5f);
    test_flt(in->mouse.normalized[1], 0.25f);

    ecs_fini(world);
}

void Device_mouse_sweep(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_sweep(world, &(box_input_sweep_t){
        .from = {0, 10}, .to = {100, 10}, .steps = 4, .frames = 4 });

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 0);

    for (int32_t i = 1; i <= 4; i ++) {
        ecs_progress(world, 0);
        in = ecs_singleton_get(world, Input);
        test_flt(in->mouse.position[0], 25.0f * (float)i);
        test_flt(in->mouse.position[1], 10);
        test_flt(in->mouse.delta[0], 25);
    }

    test_int(box_input_pending(world), 0);
    test_bool(in->mouse.pinned, true);

    ecs_fini(world);
}

void Device_mouse_sweep_one_frame(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_sweep(world, &(box_input_sweep_t){
        .from = {0, 0}, .to = {40, 80}, .steps = 4, .frames = 1 });

    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, Input)->mouse.position[0], 0);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 40);
    test_flt(in->mouse.position[1], 80);
    test_flt(in->mouse.delta[0], 40);
    test_flt(in->mouse.delta[1], 80);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Device_mouse_release(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_move(world, 10, 10, false);
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = {50, 50} });
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 10);
    test_bool(in->mouse.pinned, true);

    box_input_mouse_release(world);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.pinned, false);

    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = {50, 60} });
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 50);
    test_flt(in->mouse.position[1], 60);

    ecs_fini(world);
}

void Device_mouse_click(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_click(world, KeyNone, (float[]){5, 6}, false, 2);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 5);
    test_flt(in->mouse.position[1], 6);
    test_bool(in->keys[KeyMouseLeft].pressed, true);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseLeft].down, true);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseLeft].down, false);
    test_bool(in->keys[KeyMouseLeft].released, true);

    box_input_mouse_click(world, KeyMouseRight, NULL, false, 1);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseRight].pressed, true);
    test_flt(in->mouse.position[0], 5);

    ecs_fini(world);
}

void Device_mouse_drag(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = {0, 0}, .to = {30, 0}, .steps = 3, .frames = 3,
        .button = KeyMouseRight });

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseRight].pressed, true);
    test_flt(in->mouse.position[0], 0);

    for (int32_t i = 1; i <= 3; i ++) {
        ecs_progress(world, 0);
        in = ecs_singleton_get(world, Input);
        test_bool(in->keys[KeyMouseRight].down, true);
        test_flt(in->mouse.position[0], 10.0f * (float)i);
    }

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseRight].released, true);
    test_flt(in->mouse.position[0], 30);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Device_mouse_scroll(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_scroll(world, 1, -2);
    box_input_mouse_scroll(world, 0, -1);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.scroll[0], 1);
    test_flt(in->mouse.scroll[1], -3);

    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.scroll[0], 0);
    test_flt(in->mouse.scroll[1], 0);

    ecs_fini(world);
}

void Device_focus_lost_releases_keys(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key(world, KeyShiftLeft, true);
    box_input_key(world, KeyMouseLeft, true);
    ecs_progress(world, 0);

    box_input_focus(world, false);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->focused, false);
    test_bool(in->keys[KeyShiftLeft].down, false);
    test_bool(in->keys[KeyShiftLeft].released, true);
    test_bool(in->keys[KeyMouseLeft].down, false);

    box_input_focus(world, true);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->focused, true);

    ecs_fini(world);
}

void Device_focus_ignores_platform(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_focus(world, false);
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyA });
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = {7, 8} });
    box_input_platform_push(world, &(InputEvent){ .kind = InputText, .code = 'a' });
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].down, false);
    test_flt(in->mouse.position[0], 0);
    test_str(in->text, "");

    box_input_key_press(world, KeyB, 1);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyB].pressed, true);

    box_input_platform_push(world, &(InputEvent){ .kind = InputFocus, .code = 1 });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyA });
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->focused, true);
    test_bool(in->keys[KeyA].pressed, true);

    ecs_fini(world);
}

void Device_platform_poll(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    platform_count = 0;
    platform_polls = 0;
    platform_frees = 0;
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = PlatformPoll, .free = PlatformFree });

    platform_events[0] = (InputEvent){ .kind = InputKeyDown, .key = KeyQ };
    platform_events[1] = (InputEvent){ .kind = InputMove, .value = {3, 4} };
    platform_events[2] = (InputEvent){ .kind = InputText, .code = 'q' };
    platform_count = 3;
    ecs_progress(world, 0);
    test_int(platform_polls, 1);

    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyQ].pressed, true);
    test_flt(in->mouse.position[0], 3);
    test_flt(in->mouse.position[1], 4);
    test_bool(in->mouse.pinned, false);
    test_str(in->text, "q");

    platform_events[0] = (InputEvent){ .kind = InputKeyUp, .key = KeyQ };
    platform_events[1] = (InputEvent){ .kind = InputMove, .value = {5, 4} };
    platform_count = 2;
    ecs_progress(world, 0);
    test_int(platform_polls, 2);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyQ].released, true);
    test_flt(in->mouse.delta[0], 2);

    test_int(platform_frees, 0);
    ecs_fini(world);
    test_int(platform_frees, 1);
}

void Device_platform_replace(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    platform_frees = 0;
    platform_polls = 0;
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = PlatformPoll, .free = PlatformFree });
    box_input_platform_set(world, NULL);
    test_int(platform_frees, 1);

    ecs_progress(world, 0);
    test_int(platform_polls, 0);

    ecs_fini(world);
    test_int(platform_frees, 1);
}

void Device_platform_tap_in_one_frame(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyE });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyUp, .key = KeyE });
    ecs_progress(world, 0);

    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyE].pressed, true);
    test_bool(in->keys[KeyE].released, true);
    test_bool(in->keys[KeyE].down, false);

    ecs_fini(world);
}

void Device_platform_resize_unfocused(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_focus(world, false);
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){ .kind = InputResize, .value = {640, 480} });
    ecs_progress(world, 0);

    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->size[0], 640);
    test_flt(in->size[1], 480);

    ecs_fini(world);
}

void Device_reset(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_mouse_move(world, 4, 4, false);
    box_input_key(world, KeyCtrlLeft, true);
    box_input_key_press(world, KeyZ, 10);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyZ].down, true);

    box_input_mouse_sweep(world, &(box_input_sweep_t){ .from = {0, 0}, .to = {9, 9} });
    test_assert(box_input_pending(world) > 0);

    box_input_reset(world);

    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_int(box_input_pending(world), 0);
    test_bool(in->keys[KeyZ].down, false);
    test_bool(in->keys[KeyZ].released, true);
    test_bool(in->keys[KeyCtrlLeft].down, false);
    test_bool(in->mouse.pinned, false);
    test_flt(in->mouse.position[0], 4);

    for (int32_t i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
    }

    test_flt(ecs_singleton_get(world, Input)->mouse.position[0], 4);

    ecs_fini(world);
}

void Device_pending(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_key_press(world, KeyA, 3);
    test_assert(box_input_pending(world) > 0);

    int32_t frames = 0;
    while (box_input_pending(world) && frames < 10) {
        ecs_progress(world, 0);
        frames ++;
    }

    test_int(frames, 4);

    ecs_fini(world);
}

void Device_script_key_down(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "template Probe {\n"
            "  mut downs = 0\n"
            "  mut ups = 0\n"
            "  mut key = \"\"\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.key_down(box.input.screen)\n"
            "      downs = downs + 1\n"
            "      key = e.key\n"
            "    }\n"
            "  }\n"
            "  async {\n"
            "    while true {\n"
            "      await on.key_up(box.input.screen)\n"
            "      ups = ups + 1\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 2);

    typedef struct { int64_t downs, ups; char *key; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    test_assert(p != 0);
    test_assert(mut != 0);

    box_input_key_press(world, KeyEnter, 1);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->downs, 1);
    test_int(m->ups, 0);
    test_str(m->key, "Enter");

    ecs_progress(world, 0);
    m = ecs_get_id(world, p, mut);
    test_int(m->downs, 1);
    test_int(m->ups, 1);

    box_input_key_type(world, "a", 1);
    ecs_progress(world, 0);
    m = ecs_get_id(world, p, mut);
    test_int(m->downs, 2);
    test_str(m->key, "a");

    ecs_fini(world);
}

void Device_script_key_modifiers(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "template Probe {\n"
            "  mut key = \"\"\n"
            "  mut shift = false\n"
            "  mut ctrl = false\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.key_down(box.input.screen)\n"
            "      key = e.key\n"
            "      shift = e.shift\n"
            "      ctrl = e.ctrl\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 1);

    typedef struct { char *key; bool shift; bool ctrl; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");

    box_input_key(world, KeyShiftRight, true);
    box_input_key_press(world, KeyS, 1);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_str(m->key, "s");
    test_bool(m->shift, true);
    test_bool(m->ctrl, false);

    ecs_fini(world);
}

void Device_script_key_repeat(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "template Probe {\n"
            "  mut downs = 0\n"
            "  mut repeat = false\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.key_down(box.input.screen)\n"
            "      downs = downs + 1\n"
            "      repeat = e.repeat\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 1);

    typedef struct { int64_t downs; bool repeat; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyA });
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, p, mut))->downs, 1);
    test_bool(((const Mut*)ecs_get_id(world, p, mut))->repeat, false);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputKeyDown, .key = KeyA, .repeat = true });
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->downs, 2);
    test_bool(m->repeat, true);

    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].down, true);
    test_bool(in->keys[KeyA].pressed, false);

    ecs_fini(world);
}

void Device_script_mouse_move(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "template Probe {\n"
            "  mut moves = 0\n"
            "  mut x: f32 = 0\n"
            "  mut y: f32 = 0\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.move(box.input.screen)\n"
            "      moves = moves + 1\n"
            "      x = e.screen_x\n"
            "      y = e.screen_y\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 1);

    typedef struct { int64_t moves; float x, y; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");

    box_input_mouse_move(world, 12, 34, false);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->moves, 1);
    test_flt(m->x, 12);
    test_flt(m->y, 34);

    ecs_fini(world);
}

void Device_script_mouse_click(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "template Probe {\n"
            "  mut clicks = 0\n"
            "  mut button = -1\n"
            "  mut x: f32 = 0\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.click(box.input.screen)\n"
            "      clicks = clicks + 1\n"
            "      button = e.button\n"
            "      x = e.screen_x\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 1);

    typedef struct { int64_t clicks; int64_t button; float x; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");

    box_input_mouse_click(world, KeyMouseRight, (float[]){40, 50}, false, 1);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, p, mut))->clicks, 0);

    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->clicks, 1);
    test_int(m->button, 2);
    test_flt(m->x, 40);

    ecs_fini(world);
}

void Device_script_mouse_drag(void) {
    // input/devices
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using flecs.script\n"
            "template Probe {\n"
            "  mut drags = 0\n"
            "  mut dx: f32 = 0\n"
            "  mut releases = 0\n"
            "  async {\n"
            "    while true {\n"
            "      const e = await on.drag(box.input.screen)\n"
            "      drags = drags + 1\n"
            "      dx = dx + e.delta_x\n"
            "    }\n"
            "  }\n"
            "  async {\n"
            "    while true {\n"
            "      await on.release(box.input.screen)\n"
            "      releases = releases + 1\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Probe p\n"
    });
    test_assert(s != 0);
    test_int(ecs_script_tasks_progress(world), 2);

    typedef struct { int64_t drags; float dx; int64_t releases; } Mut;
    ecs_entity_t p = ecs_lookup(world, "p");
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = {0, 0}, .to = {30, 0}, .steps = 3, .frames = 3 });
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    const Mut *m = ecs_get_id(world, p, mut);
    test_int(m->drags, 4);
    test_flt(m->dx, 30);
    test_int(m->releases, 1);

    ecs_fini(world);
}
