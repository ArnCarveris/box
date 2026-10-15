/**
 * @file Platform.c
 * @brief glfw and browser events translated by the platform backends reach Input, actions and a
 * camera controller like synthetic input does.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>

static InputEvent surface_events[16];
static int32_t surface_event_count = 0;

static void SurfaceEventsPoll(ecs_world_t *world, void *ctx) {
    (void)ctx;
    for (int32_t i = 0; i < surface_event_count; i ++) {
        box_input_platform_push(world, &surface_events[i]);
    }

    surface_event_count = 0;
}

void Platform_glfw_key_letters_digits(void) {
    // input/platform_backends
    for (int32_t i = 0; i < 26; i ++) {
        test_int(box_input_glfw_key('A' + i), KeyA + i);
    }

    for (int32_t i = 0; i < 10; i ++) {
        test_int(box_input_glfw_key('0' + i), KeyNum0 + i);
    }
}

void Platform_glfw_key_named(void) {
    // input/platform_backends
    test_int(box_input_glfw_key(32), KeySpace);
    test_int(box_input_glfw_key(96), KeyGrave);
    test_int(box_input_glfw_key(39), KeyApostrophe);
    test_int(box_input_glfw_key(44), KeyComma);
    test_int(box_input_glfw_key(45), KeyMinus);
    test_int(box_input_glfw_key(46), KeyPeriod);
    test_int(box_input_glfw_key(47), KeySlash);
    test_int(box_input_glfw_key(59), KeySemicolon);
    test_int(box_input_glfw_key(61), KeyEqual);
    test_int(box_input_glfw_key(91), KeyBracketLeft);
    test_int(box_input_glfw_key(92), KeyBackslash);
    test_int(box_input_glfw_key(93), KeyBracketRight);
    test_int(box_input_glfw_key(256), KeyEscape);
    test_int(box_input_glfw_key(257), KeyEnter);
    test_int(box_input_glfw_key(258), KeyTab);
    test_int(box_input_glfw_key(259), KeyBackspace);
    test_int(box_input_glfw_key(260), KeyInsert);
    test_int(box_input_glfw_key(261), KeyDelete);
    test_int(box_input_glfw_key(262), KeyRight);
    test_int(box_input_glfw_key(263), KeyLeft);
    test_int(box_input_glfw_key(264), KeyDown);
    test_int(box_input_glfw_key(265), KeyUp);
    test_int(box_input_glfw_key(266), KeyPageUp);
    test_int(box_input_glfw_key(267), KeyPageDown);
    test_int(box_input_glfw_key(268), KeyHome);
    test_int(box_input_glfw_key(269), KeyEnd);
    test_int(box_input_glfw_key(280), KeyCapsLock);
    for (int32_t i = 0; i < 12; i ++) {
        test_int(box_input_glfw_key(290 + i), KeyF1 + i);
    }

    for (int32_t i = 0; i < 10; i ++) {
        test_int(box_input_glfw_key(320 + i), KeyKp0 + i);
    }

    test_int(box_input_glfw_key(330), KeyKpDecimal);
    test_int(box_input_glfw_key(331), KeyKpDivide);
    test_int(box_input_glfw_key(332), KeyKpMultiply);
    test_int(box_input_glfw_key(333), KeyKpSubtract);
    test_int(box_input_glfw_key(334), KeyKpAdd);
    test_int(box_input_glfw_key(335), KeyKpEnter);
    test_int(box_input_glfw_key(336), KeyKpEqual);
    test_int(box_input_glfw_key(340), KeyShiftLeft);
    test_int(box_input_glfw_key(341), KeyCtrlLeft);
    test_int(box_input_glfw_key(342), KeyAltLeft);
    test_int(box_input_glfw_key(343), KeySuperLeft);
    test_int(box_input_glfw_key(344), KeyShiftRight);
    test_int(box_input_glfw_key(345), KeyCtrlRight);
    test_int(box_input_glfw_key(346), KeyAltRight);
    test_int(box_input_glfw_key(347), KeySuperRight);
}

void Platform_glfw_key_unknown(void) {
    // input/platform_backends
    test_int(box_input_glfw_key(-1), KeyNone);
    test_int(box_input_glfw_key(0), KeyNone);
    test_int(box_input_glfw_key('a'), KeyNone);
    test_int(box_input_glfw_key(161), KeyNone);
    test_int(box_input_glfw_key(302), KeyNone);
    test_int(box_input_glfw_key(348), KeyNone);
}

void Platform_glfw_button(void) {
    // input/platform_backends
    test_int(box_input_glfw_button(0), KeyMouseLeft);
    test_int(box_input_glfw_button(1), KeyMouseRight);
    test_int(box_input_glfw_button(2), KeyMouseMiddle);
    test_int(box_input_glfw_button(3), KeyNone);
    test_int(box_input_glfw_button(-1), KeyNone);
}

void Platform_dom_key_letters_digits(void) {
    // input/platform_backends
    char code[16];
    for (int32_t i = 0; i < 26; i ++) {
        snprintf(code, sizeof(code), "Key%c", 'A' + i);
        test_int(box_input_dom_key(code), KeyA + i);
    }

    for (int32_t i = 0; i < 10; i ++) {
        snprintf(code, sizeof(code), "Digit%d", i);
        test_int(box_input_dom_key(code), KeyNum0 + i);
        snprintf(code, sizeof(code), "Numpad%d", i);
        test_int(box_input_dom_key(code), KeyKp0 + i);
    }

    for (int32_t i = 1; i <= 12; i ++) {
        snprintf(code, sizeof(code), "F%d", i);
        test_int(box_input_dom_key(code), KeyF1 + i - 1);
    }
}

void Platform_dom_key_named(void) {
    // input/platform_backends
    test_int(box_input_dom_key("Space"), KeySpace);
    test_int(box_input_dom_key("Enter"), KeyEnter);
    test_int(box_input_dom_key("Escape"), KeyEscape);
    test_int(box_input_dom_key("Tab"), KeyTab);
    test_int(box_input_dom_key("Backspace"), KeyBackspace);
    test_int(box_input_dom_key("Insert"), KeyInsert);
    test_int(box_input_dom_key("Delete"), KeyDelete);
    test_int(box_input_dom_key("Home"), KeyHome);
    test_int(box_input_dom_key("End"), KeyEnd);
    test_int(box_input_dom_key("PageUp"), KeyPageUp);
    test_int(box_input_dom_key("PageDown"), KeyPageDown);
    test_int(box_input_dom_key("ArrowLeft"), KeyLeft);
    test_int(box_input_dom_key("ArrowRight"), KeyRight);
    test_int(box_input_dom_key("ArrowUp"), KeyUp);
    test_int(box_input_dom_key("ArrowDown"), KeyDown);
    test_int(box_input_dom_key("ShiftLeft"), KeyShiftLeft);
    test_int(box_input_dom_key("ShiftRight"), KeyShiftRight);
    test_int(box_input_dom_key("ControlLeft"), KeyCtrlLeft);
    test_int(box_input_dom_key("ControlRight"), KeyCtrlRight);
    test_int(box_input_dom_key("AltLeft"), KeyAltLeft);
    test_int(box_input_dom_key("AltRight"), KeyAltRight);
    test_int(box_input_dom_key("MetaLeft"), KeySuperLeft);
    test_int(box_input_dom_key("MetaRight"), KeySuperRight);
    test_int(box_input_dom_key("CapsLock"), KeyCapsLock);
    test_int(box_input_dom_key("Quote"), KeyApostrophe);
    test_int(box_input_dom_key("Comma"), KeyComma);
    test_int(box_input_dom_key("Minus"), KeyMinus);
    test_int(box_input_dom_key("Period"), KeyPeriod);
    test_int(box_input_dom_key("Slash"), KeySlash);
    test_int(box_input_dom_key("Semicolon"), KeySemicolon);
    test_int(box_input_dom_key("Equal"), KeyEqual);
    test_int(box_input_dom_key("BracketLeft"), KeyBracketLeft);
    test_int(box_input_dom_key("Backslash"), KeyBackslash);
    test_int(box_input_dom_key("BracketRight"), KeyBracketRight);
    test_int(box_input_dom_key("Backquote"), KeyGrave);
    test_int(box_input_dom_key("NumpadDecimal"), KeyKpDecimal);
    test_int(box_input_dom_key("NumpadDivide"), KeyKpDivide);
    test_int(box_input_dom_key("NumpadMultiply"), KeyKpMultiply);
    test_int(box_input_dom_key("NumpadSubtract"), KeyKpSubtract);
    test_int(box_input_dom_key("NumpadAdd"), KeyKpAdd);
    test_int(box_input_dom_key("NumpadEnter"), KeyKpEnter);
    test_int(box_input_dom_key("NumpadEqual"), KeyKpEqual);
}

void Platform_dom_key_unknown(void) {
    // input/platform_backends
    test_int(box_input_dom_key(NULL), KeyNone);
    test_int(box_input_dom_key(""), KeyNone);
    test_int(box_input_dom_key("Unidentified"), KeyNone);
    test_int(box_input_dom_key("Keya"), KeyNone);
    test_int(box_input_dom_key("KeyAB"), KeyNone);
    test_int(box_input_dom_key("Digit10"), KeyNone);
    test_int(box_input_dom_key("F13"), KeyNone);
    test_int(box_input_dom_key("F0"), KeyNone);
    test_int(box_input_dom_key("w"), KeyNone);
}

void Platform_dom_button(void) {
    // input/platform_backends
    test_int(box_input_dom_button(0), KeyMouseLeft);
    test_int(box_input_dom_button(1), KeyMouseMiddle);
    test_int(box_input_dom_button(2), KeyMouseRight);
    test_int(box_input_dom_button(3), KeyNone);
}

void Platform_glfw_key_press_release(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_glfw_on_key(world, 'W', 1);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, true);
    test_bool(in->keys[KeyW].pressed, true);

    box_input_glfw_on_key(world, 'W', 2);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, true);
    test_bool(in->keys[KeyW].pressed, false);

    box_input_glfw_on_key(world, 'W', 0);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, false);
    test_bool(in->keys[KeyW].released, true);

    ecs_fini(world);
}

void Platform_glfw_key_unknown_ignored(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_glfw_on_key(world, 161, 1);
    box_input_glfw_on_button(world, 5, 1);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    for (int32_t i = 0; i < BOX_KEY_COUNT; i ++) {
        test_bool(in->keys[i].down, false);
    }

    ecs_fini(world);
}

void Platform_glfw_button_press_release(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_glfw_on_button(world, 1, 1);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseRight].down, true);
    test_bool(in->keys[KeyMouseRight].pressed, true);
    test_bool(in->keys[KeyMouseLeft].down, false);

    box_input_glfw_on_button(world, 1, 0);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyMouseRight].down, false);
    test_bool(in->keys[KeyMouseRight].released, true);

    ecs_fini(world);
}

void Platform_dom_key_text(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(box_input_dom_on_key(world, "KeyA", "a", true, false, false), true);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].pressed, true);
    test_str(in->text, "a");

    test_bool(box_input_dom_on_key(world, "KeyA", "a", false, false, false), true);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].released, true);
    test_str(in->text, "");

    ecs_fini(world);
}

void Platform_dom_key_utf8_text(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_dom_on_key(world, "KeyE", "\xc3\xa9", true, false, false);
    box_input_dom_on_key(world, "Unidentified", "\xe2\x82\xac", true, false, false);
    box_input_dom_on_key(world, "Enter", "Enter", true, false, false);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyE].down, true);
    test_bool(in->keys[KeyEnter].down, true);
    test_str(in->text, "\xc3\xa9\xe2\x82\xac");

    ecs_fini(world);
}

void Platform_dom_key_shortcut(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(box_input_dom_on_key(world, "KeyR", "r", true, false, true), false);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyR].down, true);
    test_str(in->text, "");

    ecs_fini(world);
}

void Platform_dom_key_function_not_consumed(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(box_input_dom_on_key(world, "F5", "F5", true, false, false), false);
    test_bool(box_input_dom_on_key(world, "Unidentified", "", true, false, false), false);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyF5].down, true);
    test_str(in->text, "");

    ecs_fini(world);
}

void Platform_dom_key_grave(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(box_input_dom_on_key(world, "Backquote", "`", true, false, false), true);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyGrave].pressed, true);
    test_str(in->text, "`");

    ecs_fini(world);
}

void Platform_dom_key_repeat(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_dom_on_key(world, "KeyX", "x", true, false, false);
    ecs_progress(world, 0);
    box_input_dom_on_key(world, "KeyX", "x", true, true, false);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyX].down, true);
    test_bool(in->keys[KeyX].pressed, false);
    test_str(in->text, "x");

    ecs_fini(world);
}

void Platform_dom_wheel(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_dom_on_wheel(world, 0, 100, 0);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.scroll[0], 0);
    test_flt(in->mouse.scroll[1], -1);

    box_input_dom_on_wheel(world, -6, -3, 1);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.scroll[0], 2);
    test_flt(in->mouse.scroll[1], 1);

    box_input_dom_on_wheel(world, 0, 1, 2);
    box_input_dom_on_wheel(world, 0, 50, 0);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.scroll[1], -1.5f);

    ecs_fini(world);
}

void Platform_binding_action(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t console = ecs_new(world);
    ecs_set(world, console, Action, { 0 });
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Binding, { .action = console, .key = KeyGrave });

    box_input_glfw_on_key(world, 96, 1);
    ecs_progress(world, 0);
    const ActionState *s = ecs_get(world, console, ActionState);
    test_bool(s->down, true);
    test_bool(s->pressed, true);

    box_input_glfw_on_key(world, 96, 0);
    ecs_progress(world, 0);
    s = ecs_get(world, console, ActionState);
    test_bool(s->down, false);
    test_bool(s->released, true);

    box_input_dom_on_key(world, "Backquote", "`", true, false, false);
    ecs_progress(world, 0);
    s = ecs_get(world, console, ActionState);
    test_bool(s->pressed, true);

    ecs_fini(world);
}

void Platform_binding_modifier(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t save = ecs_new(world);
    ecs_set(world, save, Action, { 0 });
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Binding, { .action = save, .key = KeyS, .ctrl = true });

    box_input_glfw_on_key(world, 'S', 1);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->down, false);

    box_input_glfw_on_key(world, 'S', 0);
    box_input_glfw_on_key(world, 345, 1);
    ecs_progress(world, 0);
    box_input_glfw_on_key(world, 'S', 1);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->down, true);

    box_input_glfw_on_key(world, 345, 0);
    box_input_glfw_on_key(world, 'S', 0);
    box_input_dom_on_key(world, "ControlLeft", "Control", true, false, true);
    box_input_dom_on_key(world, "KeyS", "s", true, false, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, save, ActionState)->down, true);

    ecs_fini(world);
}

void Platform_camera_keys_like_synthetic(void) {
    // input/platform_backends
    ecs_world_t *platform = ecs_init();
    ECS_IMPORT(platform, Box);
    ecs_world_t *synthetic = ecs_init();
    ECS_IMPORT(synthetic, Box);

    ecs_entity_t cam_p = ecs_new(platform);
    ecs_add(platform, cam_p, CameraController);
    ecs_entity_t cam_s = ecs_new(synthetic);
    ecs_add(synthetic, cam_s, CameraController);

    for (int32_t frame = 0; frame < 8; frame ++) {
        if (frame == 0) {
            box_input_glfw_on_key(platform, 'W', 1);
            box_input_key(synthetic, KeyW, true);
        } else if (frame == 2) {
            box_input_glfw_on_key(platform, 'D', 1);
            box_input_glfw_on_key(platform, 340, 1);
            box_input_key(synthetic, KeyD, true);
            box_input_key(synthetic, KeyShiftLeft, true);
        } else if (frame == 3) {
            box_input_glfw_on_key(platform, 'E', 1);
            box_input_glfw_on_key(platform, 'W', 2);
            box_input_key(synthetic, KeyE, true);
        } else if (frame == 5) {
            box_input_glfw_on_key(platform, 'W', 0);
            box_input_glfw_on_key(platform, 'D', 0);
            box_input_glfw_on_key(platform, 340, 0);
            box_input_glfw_on_key(platform, 'E', 0);
            box_input_key(synthetic, KeyW, false);
            box_input_key(synthetic, KeyD, false);
            box_input_key(synthetic, KeyShiftLeft, false);
            box_input_key(synthetic, KeyE, false);
        }

        ecs_progress(platform, 0.25f);
        ecs_progress(synthetic, 0.25f);
        const Position *pp = ecs_get(platform, cam_p, Position);
        const Position *ps = ecs_get(synthetic, cam_s, Position);
        test_flt(pp->x, ps->x);
        test_flt(pp->y, ps->y);
        test_flt(pp->z, ps->z);
    }

    const Position *p = ecs_get(platform, cam_p, Position);
    test_assert(p->x > 0);
    test_assert(p->y > 0);
    test_assert(p->z < -5);

    ecs_fini(platform);
    ecs_fini(synthetic);
}

void Platform_camera_look_like_synthetic(void) {
    // input/platform_backends
    ecs_world_t *platform = ecs_init();
    ECS_IMPORT(platform, Box);
    ecs_world_t *synthetic = ecs_init();
    ECS_IMPORT(synthetic, Box);

    ecs_entity_t cam_p = ecs_new(platform);
    ecs_add(platform, cam_p, CameraController);
    ecs_entity_t cam_s = ecs_new(synthetic);
    ecs_add(synthetic, cam_s, CameraController);

    box_input_mouse_drag(synthetic, &(box_input_sweep_t){
        .from = { 100, 100 }, .to = { 200, 150 }, .steps = 4, .frames = 4,
        .button = KeyMouseRight
    });
    for (int32_t frame = 0; frame < 8; frame ++) {
        if (frame == 0) {
            box_input_platform_push(platform, &(InputEvent){
                .kind = InputMove, .value = { 100, 100 } });
            box_input_glfw_on_button(platform, 1, 1);
        } else if (frame <= 4) {
            box_input_platform_push(platform, &(InputEvent){
                .kind = InputMove, .value = { 100 + 25.0f * frame, 100 + 12.5f * frame } });
        } else if (frame == 5) {
            box_input_glfw_on_button(platform, 1, 0);
        }

        ecs_progress(platform, 0.5f);
        ecs_progress(synthetic, 0.5f);
        const Rotation *rp = ecs_get(platform, cam_p, Rotation);
        const Rotation *rs = ecs_get(synthetic, cam_s, Rotation);
        test_flt(rp->x, rs->x);
        test_flt(rp->y, rs->y);
        test_flt(rp->z, rs->z);
    }

    const Rotation *r = ecs_get(platform, cam_p, Rotation);
    test_assert(fabsf(r->y + 0.3f) < 0.0001f);
    test_assert(fabsf(r->x + 0.15f) < 0.0001f);

    ecs_fini(platform);
    ecs_fini(synthetic);
}

void Platform_camera_look_needs_right_button(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);

    box_input_glfw_on_button(world, 0, 1);
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 10, 10 } });
    ecs_progress(world, 0.5f);
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 90, 50 } });
    ecs_progress(world, 0.5f);

    const Rotation *r = ecs_get(world, cam, Rotation);
    test_flt(r->x, 0);
    test_flt(r->y, 0);

    ecs_fini(world);
}

void Platform_camera_dom_like_synthetic(void) {
    // input/platform_backends
    ecs_world_t *platform = ecs_init();
    ECS_IMPORT(platform, Box);
    ecs_world_t *synthetic = ecs_init();
    ECS_IMPORT(synthetic, Box);

    ecs_entity_t cam_p = ecs_new(platform);
    ecs_add(platform, cam_p, CameraController);
    ecs_entity_t cam_s = ecs_new(synthetic);
    ecs_add(synthetic, cam_s, CameraController);

    for (int32_t frame = 0; frame < 6; frame ++) {
        if (frame == 0) {
            box_input_dom_on_key(platform, "KeyS", "s", true, false, false);
            box_input_dom_on_key(platform, "KeyQ", "q", true, false, false);
            box_input_key(synthetic, KeyS, true);
            box_input_key(synthetic, KeyQ, true);
        } else if (frame == 1) {
            box_input_dom_on_key(platform, "ShiftLeft", "Shift", true, false, false);
            box_input_key(synthetic, KeyShiftLeft, true);
        } else if (frame == 3) {
            box_input_dom_on_key(platform, "KeyS", "s", false, false, false);
            box_input_dom_on_key(platform, "KeyQ", "q", false, false, false);
            box_input_dom_on_key(platform, "ShiftLeft", "Shift", false, false, false);
            box_input_key(synthetic, KeyS, false);
            box_input_key(synthetic, KeyQ, false);
            box_input_key(synthetic, KeyShiftLeft, false);
        }

        ecs_progress(platform, 0.25f);
        ecs_progress(synthetic, 0.25f);
        const Position *pp = ecs_get(platform, cam_p, Position);
        const Position *ps = ecs_get(synthetic, cam_s, Position);
        test_flt(pp->x, ps->x);
        test_flt(pp->y, ps->y);
        test_flt(pp->z, ps->z);
    }

    const Position *p = ecs_get(platform, cam_p, Position);
    test_flt(p->x, 0);
    test_assert(p->z > 0);
    test_assert(p->y < 0);

    ecs_fini(platform);
    ecs_fini(synthetic);
}

void Platform_camera_focus_lost_stops(void) {
    // input/platform_backends
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_add(world, cam, CameraController);
    CameraController *cc = ecs_get_mut(world, cam, CameraController);
    cc->speed = 10;
    cc->acceleration = 1e9f;
    cc->deceleration = 1e9f;

    box_input_glfw_on_key(world, 'W', 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -5);

    box_input_platform_push(world, &(InputEvent){ .kind = InputFocus, .code = 0 });
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -5);

    box_input_glfw_on_key(world, 'W', 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -5);

    box_input_platform_push(world, &(InputEvent){ .kind = InputFocus, .code = 1 });
    box_input_glfw_on_key(world, 'W', 1);
    ecs_progress(world, 0.5f);
    test_flt(ecs_get(world, cam, Position)->z, -10);

    ecs_fini(world);
}

void Platform_surfaces_input_follows_focus(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new(world);
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){ .poll = SurfaceEventsPoll });

    surface_events[0] = (InputEvent){ .kind = InputResize, .surface = a, .value = {800, 600} };
    surface_events[1] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_events[2] = (InputEvent){ .kind = InputResize, .surface = b, .value = {400, 300} };
    surface_event_count = 3;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_uint(in->surface, a);
    test_bool(in->focused, true);
    test_flt(in->size[0], 800);
    test_flt(in->size[1], 600);

    surface_events[0] = (InputEvent){ .kind = InputKeyDown, .surface = b, .key = KeyW };
    surface_events[1] = (InputEvent){ .kind = InputMove, .surface = b, .value = {10, 10} };
    surface_events[2] = (InputEvent){ .kind = InputText, .surface = b, .code = 'b' };
    surface_events[3] = (InputEvent){ .kind = InputScroll, .surface = b, .value = {0, 1} };
    surface_events[4] = (InputEvent){ .kind = InputKeyDown, .surface = a, .key = KeyE };
    surface_events[5] = (InputEvent){ .kind = InputMove, .surface = a, .value = {100, 50} };
    surface_events[6] = (InputEvent){ .kind = InputText, .surface = a, .code = 'a' };
    surface_event_count = 7;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyW].down, false);
    test_bool(in->keys[KeyE].down, true);
    test_flt(in->mouse.position[0], 100);
    test_flt(in->mouse.position[1], 50);
    test_flt(in->mouse.scroll[1], 0);
    test_str(in->text, "a");

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = b, .code = 1 };
    surface_events[1] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 0 };
    surface_event_count = 2;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_uint(in->surface, b);
    test_bool(in->focused, true);
    test_flt(in->size[0], 400);
    test_flt(in->size[1], 300);
    test_bool(in->keys[KeyE].down, false);
    test_bool(in->keys[KeyE].released, true);
    test_flt(in->mouse.position[0], 10);
    test_flt(in->mouse.position[1], 10);

    surface_events[0] = (InputEvent){ .kind = InputKeyDown, .surface = a, .key = KeyQ };
    surface_events[1] = (InputEvent){ .kind = InputKeyDown, .surface = b, .key = KeyR };
    surface_events[2] = (InputEvent){ .kind = InputMove, .surface = b, .value = {5, 5} };
    surface_events[3] = (InputEvent){ .kind = InputResize, .surface = a, .value = {1024, 768} };
    surface_event_count = 4;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyQ].down, false);
    test_bool(in->keys[KeyR].pressed, true);
    test_flt(in->mouse.position[0], 5);
    test_flt(in->mouse.delta[0], -5);
    test_flt(in->mouse.delta[1], -5);
    test_flt(in->size[0], 400);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = b, .code = 0 };
    surface_events[1] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_event_count = 2;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_uint(in->surface, a);
    test_bool(in->focused, true);
    test_flt(in->size[0], 1024);
    test_flt(in->size[1], 768);
    test_bool(in->keys[KeyR].down, false);

    ecs_fini(world);
}

void Platform_surfaces_focus_lost(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new(world);
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){ .poll = SurfaceEventsPoll });

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_events[1] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 0 };
    surface_event_count = 2;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_uint(in->surface, a);
    test_bool(in->focused, false);

    surface_events[0] = (InputEvent){ .kind = InputKeyDown, .surface = a, .key = KeyA };
    surface_events[1] = (InputEvent){ .kind = InputKeyDown, .surface = b, .key = KeyB };
    surface_event_count = 2;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_bool(in->keys[KeyA].down, false);
    test_bool(in->keys[KeyB].down, false);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = b, .code = 1 };
    surface_events[1] = (InputEvent){ .kind = InputKeyDown, .surface = b, .key = KeyB };
    surface_event_count = 2;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_uint(in->surface, b);
    test_bool(in->focused, true);
    test_bool(in->keys[KeyB].pressed, true);

    ecs_fini(world);
}

void Platform_surfaces_untagged_events(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new(world);
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){ .poll = SurfaceEventsPoll });

    surface_events[0] = (InputEvent){ .kind = InputKeyDown, .surface = a, .key = KeyA };
    surface_events[1] = (InputEvent){ .kind = InputKeyDown, .surface = b, .key = KeyB };
    surface_event_count = 2;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_uint(in->surface, 0);
    test_bool(in->keys[KeyA].down, true);
    test_bool(in->keys[KeyB].down, true);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = b, .code = 1 };
    surface_events[1] = (InputEvent){ .kind = InputKeyDown, .key = KeyC };
    surface_event_count = 2;
    box_input_key_press(world, KeyD, 1);
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_uint(in->surface, b);
    test_bool(in->keys[KeyC].pressed, true);
    test_bool(in->keys[KeyD].pressed, true);

    ecs_fini(world);
}

void Platform_surfaces_deleted_surface(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new(world);
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){ .poll = SurfaceEventsPoll });

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_event_count = 1;
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, Input)->surface, a);

    ecs_delete(world, a);
    surface_events[0] = (InputEvent){ .kind = InputKeyDown, .surface = b, .key = KeyB };
    surface_event_count = 1;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_uint(in->surface, 0);
    test_bool(in->keys[KeyB].pressed, true);

    ecs_fini(world);
}

void Platform_surfaces_focus_takes_cursor(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new(world);
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){ .poll = SurfaceEventsPoll });

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_events[1] = (InputEvent){ .kind = InputMove, .surface = a, .value = {100, 50} };
    surface_event_count = 2;
    ecs_progress(world, 0);

    surface_events[0] = (InputEvent){ .kind = InputMove, .surface = b, .value = {590, 380} };
    surface_events[1] = (InputEvent){ .kind = InputMove, .surface = b, .value = {600, 400} };
    surface_event_count = 2;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 100);
    test_flt(in->mouse.position[1], 50);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = b, .code = 1 };
    surface_events[1] = (InputEvent){ .kind = InputKeyDown, .surface = b, .key = KeyMouseLeft };
    surface_event_count = 2;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_uint(in->surface, b);
    test_bool(in->keys[KeyMouseLeft].pressed, true);
    test_flt(in->mouse.position[0], 600);
    test_flt(in->mouse.position[1], 400);
    test_flt(in->mouse.delta[0], 0);
    test_flt(in->mouse.delta[1], 0);

    surface_events[0] = (InputEvent){ .kind = InputMove, .surface = b, .value = {603, 404} };
    surface_event_count = 1;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.delta[0], 3);
    test_flt(in->mouse.delta[1], 4);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_event_count = 1;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 100);
    test_flt(in->mouse.position[1], 50);

    ecs_fini(world);
}

void Platform_surfaces_focus_keeps_pinned_cursor(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new(world);
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){ .poll = SurfaceEventsPoll });

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_events[1] = (InputEvent){ .kind = InputMove, .surface = b, .value = {600, 400} };
    surface_event_count = 2;
    box_input_mouse_move(world, 10, 20, false);
    ecs_progress(world, 0);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = b, .code = 1 };
    surface_event_count = 1;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_uint(in->surface, b);
    test_bool(in->mouse.pinned, true);
    test_flt(in->mouse.position[0], 10);
    test_flt(in->mouse.position[1], 20);

    ecs_fini(world);
}

static int32_t capture_calls = 0;
static ecs_entity_t capture_surfaces[8];
static bool capture_states[8];

static bool CaptureRecord(ecs_world_t *world, ecs_entity_t surface, bool captured, void *ctx) {
    (void)world;
    (void)ctx;
    if (capture_calls < 8) {
        capture_surfaces[capture_calls] = surface;
        capture_states[capture_calls] = captured;
    }

    capture_calls ++;

    return captured;
}

static box_input_dom_pointer_t dom_pointer;
static int32_t dom_releases = 0;

static bool DomCapture(ecs_world_t *world, ecs_entity_t surface, bool captured, void *ctx) {
    (void)world;
    (void)surface;
    (void)ctx;
    dom_pointer.want = captured;
    dom_releases += !captured;

    return captured && dom_pointer.locked;
}

void Platform_capture_follows_request(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    capture_calls = 0;
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = SurfaceEventsPoll, .capture = CaptureRecord });

    ecs_progress(world, 0);
    test_int(capture_calls, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_int(capture_calls, 1);
    test_uint(capture_surfaces[0], 0);
    test_bool(capture_states[0], true);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(capture_calls, 1);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);

    ecs_singleton_get_mut(world, Input)->capture = false;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_int(capture_calls, 2);
    test_bool(capture_states[1], false);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    box_input_queue(world, &(InputEvent){ .kind = InputCapture, .code = 1 });
    ecs_progress(world, 0);
    test_int(capture_calls, 3);
    test_bool(capture_states[2], true);
    test_bool(ecs_singleton_get(world, Input)->capture, true);

    ecs_fini(world);
}

void Platform_capture_follows_focus(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_entity_t b = ecs_new(world);
    capture_calls = 0;
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = SurfaceEventsPoll, .capture = CaptureRecord });

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_event_count = 1;
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_int(capture_calls, 1);
    test_uint(capture_surfaces[0], a);
    test_bool(capture_states[0], true);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 0 };
    surface_event_count = 1;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_int(capture_calls, 2);
    test_uint(capture_surfaces[1], a);
    test_bool(capture_states[1], false);
    test_bool(in->mouse.captured, false);
    test_bool(in->capture, true);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = a, .code = 1 };
    surface_event_count = 1;
    ecs_progress(world, 0);
    test_int(capture_calls, 3);
    test_uint(capture_surfaces[2], a);
    test_bool(capture_states[2], true);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);

    surface_events[0] = (InputEvent){ .kind = InputFocus, .surface = b, .code = 1 };
    surface_event_count = 1;
    ecs_progress(world, 0);
    test_int(capture_calls, 5);
    test_uint(capture_surfaces[3], a);
    test_bool(capture_states[3], false);
    test_uint(capture_surfaces[4], b);
    test_bool(capture_states[4], true);

    box_input_focus(world, false);
    ecs_progress(world, 0);
    test_int(capture_calls, 6);
    test_uint(capture_surfaces[5], b);
    test_bool(capture_states[5], false);

    ecs_fini(world);
}

void Platform_capture_lost_by_platform(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    capture_calls = 0;
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = SurfaceEventsPoll, .capture = CaptureRecord });

    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);

    surface_events[0] = (InputEvent){ .kind = InputCapture, .code = 0 };
    surface_event_count = 1;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->capture, false);
    test_bool(in->mouse.captured, false);
    test_int(capture_calls, 2);
    test_bool(capture_states[1], false);

    ecs_progress(world, 0);
    test_int(capture_calls, 2);

    ecs_fini(world);
}

void Platform_capture_backend_replaced(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    capture_calls = 0;
    surface_event_count = 0;
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    box_input_platform_set(world, &(box_input_platform_t){ .capture = CaptureRecord });
    ecs_progress(world, 0);
    test_int(capture_calls, 1);
    test_bool(capture_states[0], true);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);

    box_input_platform_set(world, NULL);
    test_int(capture_calls, 2);
    test_bool(capture_states[1], false);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    ecs_fini(world);
}

void Platform_capture_starts_fresh_delta(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    capture_calls = 0;
    surface_event_count = 0;
    box_input_platform_set(world, &(box_input_platform_t){
        .poll = SurfaceEventsPoll, .capture = CaptureRecord });

    surface_events[0] = (InputEvent){ .kind = InputResize, .value = {800, 600} };
    surface_events[1] = (InputEvent){ .kind = InputMove, .value = {780, 300} };
    surface_event_count = 2;
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);

    surface_events[0] = (InputEvent){ .kind = InputMove, .value = {400, 300} };
    surface_event_count = 1;
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.delta[0], 0);
    test_flt(in->mouse.delta[1], 0);

    surface_events[0] = (InputEvent){ .kind = InputMove, .value = {410, 290} };
    surface_event_count = 1;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.delta[0], 10);
    test_flt(in->mouse.delta[1], -10);

    ecs_singleton_get_mut(world, Input)->capture = false;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    surface_events[0] = (InputEvent){ .kind = InputMove, .value = {100, 100} };
    surface_event_count = 1;
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.delta[0], 0);
    test_flt(in->mouse.delta[1], 0);

    ecs_fini(world);
}

void Platform_capture_synthetic_input(void) {
    // input_fixes/cursor_capture
    ecs_world_t *platform = ecs_init();
    ECS_IMPORT(platform, Box);
    ecs_world_t *synthetic = ecs_init();
    ECS_IMPORT(synthetic, Box);

    ecs_entity_t cam_p = ecs_new(platform);
    ecs_add(platform, cam_p, CameraController);
    ecs_entity_t cam_s = ecs_new(synthetic);
    ecs_add(synthetic, cam_s, CameraController);
    capture_calls = 0;
    box_input_platform_set(platform, &(box_input_platform_t){ .capture = CaptureRecord });
    ecs_singleton_get_mut(platform, Input)->capture = true;
    ecs_singleton_modified(platform, Input);

    box_input_sweep_t drag = {
        .from = { 100, 100 }, .to = { 200, 150 }, .steps = 4, .frames = 4,
        .button = KeyMouseRight
    };
    box_input_mouse_drag(platform, &drag);
    box_input_mouse_drag(synthetic, &drag);
    box_input_key_press(platform, KeyW, 3);
    box_input_key_press(synthetic, KeyW, 3);
    for (int32_t frame = 0; frame < 8; frame ++) {
        ecs_progress(platform, 0.5f);
        ecs_progress(synthetic, 0.5f);
        test_bool(ecs_singleton_get(platform, Input)->mouse.captured, true);
        const Rotation *rp = ecs_get(platform, cam_p, Rotation);
        const Rotation *rs = ecs_get(synthetic, cam_s, Rotation);
        test_flt(rp->x, rs->x);
        test_flt(rp->y, rs->y);
        const Position *pp = ecs_get(platform, cam_p, Position);
        const Position *ps = ecs_get(synthetic, cam_s, Position);
        test_flt(pp->z, ps->z);
    }

    test_assert(fabsf(ecs_get(platform, cam_p, Rotation)->y) > 0.1f);
    test_assert(ecs_get(platform, cam_p, Position)->z < 0);
    test_int(capture_calls, 1);

    ecs_fini(platform);
    ecs_fini(synthetic);
}

void Platform_dom_lock_moves_past_edge(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    dom_pointer = (box_input_dom_pointer_t){0};
    box_input_platform_set(world, &(box_input_platform_t){ .capture = DomCapture });
    box_input_platform_push(world, &(InputEvent){ .kind = InputResize, .value = {800, 600} });
    box_input_dom_on_move(world, &dom_pointer, (vec2){400, 300}, (vec2){0, 0});
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_bool(dom_pointer.want, true);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    test_bool(box_input_dom_on_lock(world, &dom_pointer, true, true), false);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);

    float x = 400;
    for (int32_t i = 0; i < 4; i ++) {
        box_input_dom_on_move(world, &dom_pointer, (vec2){799, 300}, (vec2){250, -20});
        ecs_progress(world, 0);
        x += 250;
        const Input *in = ecs_singleton_get(world, Input);
        test_flt(in->mouse.delta[0], 250);
        test_flt(in->mouse.delta[1], -20);
        test_flt(in->mouse.position[0], x);
    }

    test_assert(ecs_singleton_get(world, Input)->mouse.position[0] > 800);

    ecs_fini(world);
}

void Platform_dom_lock_escape_no_jump(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    dom_pointer = (box_input_dom_pointer_t){0};
    dom_releases = 0;
    box_input_platform_set(world, &(box_input_platform_t){ .capture = DomCapture });
    box_input_platform_push(world, &(InputEvent){ .kind = InputResize, .value = {800, 600} });
    box_input_dom_on_move(world, &dom_pointer, (vec2){400, 300}, (vec2){0, 0});
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    box_input_dom_on_lock(world, &dom_pointer, true, true);
    ecs_progress(world, 0);
    for (int32_t i = 0; i < 10; i ++) {
        box_input_dom_on_move(world, &dom_pointer, (vec2){799, 300}, (vec2){300, 0});
        ecs_progress(world, 0);
    }

    test_flt(ecs_singleton_get(world, Input)->mouse.position[0], 3400);

    test_bool(box_input_dom_on_lock(world, &dom_pointer, false, true), false);
    box_input_dom_on_move(world, &dom_pointer, (vec2){120, 80}, (vec2){5, 5});
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 120);
    test_flt(in->mouse.position[1], 80);
    test_flt(in->mouse.delta[0], 0);
    test_flt(in->mouse.delta[1], 0);
    test_bool(in->capture, false);
    test_bool(in->mouse.captured, false);
    test_int(dom_releases, 1);
    test_bool(dom_pointer.want, false);

    box_input_dom_on_move(world, &dom_pointer, (vec2){125, 83}, (vec2){5, 3});
    ecs_progress(world, 0);
    in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.delta[0], 5);
    test_flt(in->mouse.delta[1], 3);

    ecs_fini(world);
}

void Platform_dom_release_no_jump(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    dom_pointer = (box_input_dom_pointer_t){0};
    box_input_platform_set(world, &(box_input_platform_t){ .capture = DomCapture });
    box_input_dom_on_move(world, &dom_pointer, (vec2){400, 300}, (vec2){0, 0});
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    box_input_dom_on_lock(world, &dom_pointer, true, true);
    box_input_dom_on_move(world, &dom_pointer, (vec2){400, 300}, (vec2){900, 0});
    ecs_progress(world, 0);

    ecs_singleton_get_mut(world, Input)->capture = false;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_bool(dom_pointer.want, false);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    box_input_dom_on_move(world, &dom_pointer, (vec2){400, 300}, (vec2){50, 0});
    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, Input)->mouse.position[0], 1350);

    box_input_dom_on_lock(world, &dom_pointer, false, true);
    box_input_dom_on_move(world, &dom_pointer, (vec2){410, 300}, (vec2){10, 0});
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_flt(in->mouse.position[0], 410);
    test_flt(in->mouse.delta[0], 0);
    test_bool(in->capture, false);

    ecs_fini(world);
}

void Platform_dom_blur_keeps_request(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    dom_pointer = (box_input_dom_pointer_t){0};
    box_input_platform_set(world, &(box_input_platform_t){ .capture = DomCapture });
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    box_input_dom_on_lock(world, &dom_pointer, true, true);
    ecs_progress(world, 0);

    box_input_dom_on_lock(world, &dom_pointer, false, false);
    box_input_platform_push(world, &(InputEvent){ .kind = InputFocus, .code = 0 });
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->capture, true);
    test_bool(in->mouse.captured, false);
    test_bool(dom_pointer.want, false);

    box_input_platform_push(world, &(InputEvent){ .kind = InputFocus, .code = 1 });
    ecs_progress(world, 0);
    test_bool(dom_pointer.want, true);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    box_input_dom_on_lock(world, &dom_pointer, true, true);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);

    ecs_fini(world);
}

void Platform_dom_unwanted_lock(void) {
    // input_fixes/cursor_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    dom_pointer = (box_input_dom_pointer_t){0};
    box_input_platform_set(world, &(box_input_platform_t){ .capture = DomCapture });
    test_bool(box_input_dom_on_lock(world, &dom_pointer, true, true), true);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);

    box_input_dom_on_lock(world, &dom_pointer, false, true);
    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_bool(dom_pointer.want, true);

    box_input_dom_on_lock_error(world, &dom_pointer, true);
    ecs_progress(world, 0);
    const Input *in = ecs_singleton_get(world, Input);
    test_bool(in->capture, false);
    test_bool(in->mouse.captured, false);
    test_bool(dom_pointer.want, false);

    ecs_fini(world);
}

void Platform_dom_key_ill_formed(void) {
    // input_fixes/input_utf8
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_dom_on_key(world, "Unidentified", "\xED\xA0\x80", true, false, false);
    box_input_dom_on_key(world, "Unidentified", "\xC0\xAF", true, false, false);
    box_input_dom_on_key(world, "Unidentified", "\xF0\x9F\x98\x80", true, false, false);
    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->text, "\xF0\x9F\x98\x80");

    ecs_fini(world);
}
