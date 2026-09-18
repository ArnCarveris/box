/**
 * @file Codes.c
 * @brief Tests for the key code table, its script constants and the native mapping.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "../../../src/platform/platform.h"

static ecs_world_t *w;

void Codes_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxInput);
}

void Codes_teardown(void)
{
    ecs_fini(w);
}

void Codes_printable_keys_are_ascii(void)
{
    // input/device_layer
    test_int(BoxKeyA, 'A');
    test_int(BoxKeyW, 'W');
    test_int(BoxKeyZ, 'Z');
    test_int(BoxKeyNum0, '0');
    test_int(BoxKeyNum9, '9');
    test_int(BoxKeySpace, ' ');
    test_int(BoxKeyComma, ',');
    test_int(BoxKeyGrave, '`');
    test_int(BoxKeyTab, '\t');
    test_int(BoxKeyEnter, '\r');
    test_int(BoxKeyEscape, 27);
    test_int(BoxKeyBackspace, 8);
    test_int(BoxKeyDelete, 127);
}

void Codes_mouse_buttons_are_128_and_129(void)
{
    // input/device_layer
    test_int(BoxKeyMouseLeft, 128);
    test_int(BoxKeyMouseRight, 129);
    test_int(BOX_KEY_COUNT, 130);
    ecs_entity_t left = ecs_lookup(w, "box.input.keys.MouseLeft");
    test_assert(left != 0);
    test_int(ecs_get_pair_second(w, left, EcsConstant, ecs_i32_t)[0], 128);
}

void Codes_spare_codes_are_unique_and_fit_the_table(void)
{
    // input/device_layer
    ecs_entity_t keys = ecs_lookup(w, "box.input.keys");
    test_assert(keys != 0);
    bool seen[BoxKeySuper + 1] = {0};
    int32_t count = 0;
    ecs_iter_t it = ecs_children(w, keys);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const ecs_i32_t *value = ecs_get_pair_second(
                w, it.entities[i], EcsConstant, ecs_i32_t);
            test_assert(value != NULL);
            test_assert(value[0] >= 0);
            test_assert(value[0] <= BoxKeySuper);
            test_bool(seen[value[0]], false);
            seen[value[0]] = true;
            count ++;
        }
    }

    test_int(count, 114);
    test_bool(seen[BoxKeyLeft], true);
    test_bool(seen[BoxKeyF12], true);
    test_bool(seen[BoxKeyKpEqual], true);
}

void Codes_keys_are_exported_as_script_constants(void)
{
    // input/device_layer
    ecs_entity_t key = ecs_lookup(w, "box.input.keys.W");
    test_assert(key != 0);
    test_int(ecs_get_pair_second(w, key, EcsConstant, ecs_i32_t)[0], BoxKeyW);
    key = ecs_lookup(w, "box.input.keys.Space");
    test_assert(key != 0);
    test_int(ecs_get_pair_second(w, key, EcsConstant, ecs_i32_t)[0], 32);
    key = ecs_lookup(w, "box.input.keys.ShiftLeft");
    test_assert(key != 0);
    test_int(ecs_get_pair_second(w, key, EcsConstant, ecs_i32_t)[0], BoxKeyShiftLeft);
    key = ecs_lookup(w, "box.input.keys.None");
    test_assert(key != 0);
    test_int(ecs_get_pair_second(w, key, EcsConstant, ecs_i32_t)[0], 0);
}

void Codes_family_modifiers_are_reserved_above_the_slots(void)
{
    // input/device_layer
    test_int(BoxKeyShift, BOX_KEY_COUNT);
    test_int(BoxKeyCtrl, BOX_KEY_COUNT + 1);
    test_int(BoxKeyAlt, BOX_KEY_COUNT + 2);
    test_int(BoxKeySuper, BOX_KEY_COUNT + 3);
    ecs_entity_t key = ecs_lookup(w, "box.input.keys.Ctrl");
    test_assert(key != 0);
    test_int(ecs_get_pair_second(w, key, EcsConstant, ecs_i32_t)[0], BoxKeyCtrl);
    test_uint(BOX_KEY_MOD_SHIFT, BOX_KEY_MOD_SHIFT_LEFT | BOX_KEY_MOD_SHIFT_RIGHT);
    test_uint(BOX_KEY_MOD_CTRL_LEFT, 1ull << 34);
    test_uint(BOX_KEY_MASK & BOX_KEY_MOD_ALT, 0);
}

void Codes_glfw_printable_keys_map_to_themselves(void)
{
    // input/device_layer
    test_int(box_platform_key_from_native(87), BoxKeyW);
    test_int(box_platform_key_from_native(32), BoxKeySpace);
    test_int(box_platform_key_from_native(48), BoxKeyNum0);
    test_int(box_platform_key_from_native(96), BoxKeyGrave);
    test_int(box_platform_key_from_native(61), BoxKeyEqual);
}

void Codes_glfw_named_keys_map_to_spare_codes(void)
{
    // input/device_layer
    test_int(box_platform_key_from_native(256), BoxKeyEscape);
    test_int(box_platform_key_from_native(257), BoxKeyEnter);
    test_int(box_platform_key_from_native(258), BoxKeyTab);
    test_int(box_platform_key_from_native(259), BoxKeyBackspace);
    test_int(box_platform_key_from_native(261), BoxKeyDelete);
    test_int(box_platform_key_from_native(263), BoxKeyLeft);
    test_int(box_platform_key_from_native(265), BoxKeyUp);
    test_int(box_platform_key_from_native(290), BoxKeyF1);
    test_int(box_platform_key_from_native(301), BoxKeyF12);
    test_int(box_platform_key_from_native(320), BoxKeyKp0);
    test_int(box_platform_key_from_native(336), BoxKeyKpEqual);
    test_int(box_platform_key_from_native(340), BoxKeyShiftLeft);
    test_int(box_platform_key_from_native(345), BoxKeyCtrlRight);
    test_int(box_platform_key_from_native(348), BoxKeyMenu);
}

void Codes_glfw_unknown_keys_map_to_none(void)
{
    // input/device_layer
    test_int(box_platform_key_from_native(-1), BoxKeyNone);
    test_int(box_platform_key_from_native(302), BoxKeyNone);
    test_int(box_platform_key_from_native(1000), BoxKeyNone);
}

void Codes_reflection_describes_the_singleton(void)
{
    // input/device_layer
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {4, 8} });
    ecs_progress(w, 0);
    char *expr = ecs_ptr_to_expr(w, ecs_id(Input), ecs_singleton_get(w, Input));
    test_assert(expr != NULL);
    test_assert(strstr(expr, "keys") != NULL);
    test_assert(strstr(expr, "mouse") != NULL);
    test_assert(strstr(expr, "focused") != NULL);
    test_assert(strstr(expr, "window") != NULL);
    ecs_os_free(expr);
    const EcsStruct *type = ecs_get(w, ecs_id(Input), EcsStruct);
    test_assert(type != NULL);
    test_int(ecs_vec_count(&type->members), 6);
}
