/**
 * @file Platform.c
 * @brief glfw and browser events translated by the platform backends reach Input, actions and a
 * camera controller like synthetic input does.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>

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

    box_input_glfw_on_key(world, 345, 1);
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
