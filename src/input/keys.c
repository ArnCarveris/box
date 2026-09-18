/**
 * @file input/keys.c
 * @brief Exports the key codes as script constants under the keys scope.
 */

#include "input_internal.h"

/** One key code with the name it takes under the keys scope */
typedef struct box_key_name_t {
    const char *name; /**< Name of the constant */
    int32_t code;     /**< Engine key code */
} box_key_name_t;

/** Every named key code, in code order, starting with the constant of no key */
static const box_key_name_t box_key_names[] = {
    {"None", BoxKeyNone},
    {"Kp0", BoxKeyKp0},
    {"Kp1", BoxKeyKp1},
    {"Kp2", BoxKeyKp2},
    {"Kp3", BoxKeyKp3},
    {"Kp4", BoxKeyKp4},
    {"Kp5", BoxKeyKp5},
    {"Kp6", BoxKeyKp6},
    {"Backspace", BoxKeyBackspace},
    {"Tab", BoxKeyTab},
    {"Kp7", BoxKeyKp7},
    {"Kp8", BoxKeyKp8},
    {"Kp9", BoxKeyKp9},
    {"Enter", BoxKeyEnter},
    {"F1", BoxKeyF1},
    {"F2", BoxKeyF2},
    {"F3", BoxKeyF3},
    {"F4", BoxKeyF4},
    {"F5", BoxKeyF5},
    {"F6", BoxKeyF6},
    {"F7", BoxKeyF7},
    {"F8", BoxKeyF8},
    {"F9", BoxKeyF9},
    {"F10", BoxKeyF10},
    {"F11", BoxKeyF11},
    {"F12", BoxKeyF12},
    {"KpDecimal", BoxKeyKpDecimal},
    {"Escape", BoxKeyEscape},
    {"KpDivide", BoxKeyKpDivide},
    {"KpMultiply", BoxKeyKpMultiply},
    {"KpSubtract", BoxKeyKpSubtract},
    {"KpAdd", BoxKeyKpAdd},
    {"Space", BoxKeySpace},
    {"KpEnter", BoxKeyKpEnter},
    {"KpEqual", BoxKeyKpEqual},
    {"Apostrophe", BoxKeyApostrophe},
    {"Comma", BoxKeyComma},
    {"Minus", BoxKeyMinus},
    {"Period", BoxKeyPeriod},
    {"Slash", BoxKeySlash},
    {"Num0", BoxKeyNum0},
    {"Num1", BoxKeyNum1},
    {"Num2", BoxKeyNum2},
    {"Num3", BoxKeyNum3},
    {"Num4", BoxKeyNum4},
    {"Num5", BoxKeyNum5},
    {"Num6", BoxKeyNum6},
    {"Num7", BoxKeyNum7},
    {"Num8", BoxKeyNum8},
    {"Num9", BoxKeyNum9},
    {"Semicolon", BoxKeySemicolon},
    {"Equal", BoxKeyEqual},
    {"A", BoxKeyA},
    {"B", BoxKeyB},
    {"C", BoxKeyC},
    {"D", BoxKeyD},
    {"E", BoxKeyE},
    {"F", BoxKeyF},
    {"G", BoxKeyG},
    {"H", BoxKeyH},
    {"I", BoxKeyI},
    {"J", BoxKeyJ},
    {"K", BoxKeyK},
    {"L", BoxKeyL},
    {"M", BoxKeyM},
    {"N", BoxKeyN},
    {"O", BoxKeyO},
    {"P", BoxKeyP},
    {"Q", BoxKeyQ},
    {"R", BoxKeyR},
    {"S", BoxKeyS},
    {"T", BoxKeyT},
    {"U", BoxKeyU},
    {"V", BoxKeyV},
    {"W", BoxKeyW},
    {"X", BoxKeyX},
    {"Y", BoxKeyY},
    {"Z", BoxKeyZ},
    {"LeftBracket", BoxKeyLeftBracket},
    {"Backslash", BoxKeyBackslash},
    {"RightBracket", BoxKeyRightBracket},
    {"Grave", BoxKeyGrave},
    {"Left", BoxKeyLeft},
    {"Right", BoxKeyRight},
    {"Up", BoxKeyUp},
    {"Down", BoxKeyDown},
    {"ShiftLeft", BoxKeyShiftLeft},
    {"ShiftRight", BoxKeyShiftRight},
    {"CtrlLeft", BoxKeyCtrlLeft},
    {"CtrlRight", BoxKeyCtrlRight},
    {"AltLeft", BoxKeyAltLeft},
    {"AltRight", BoxKeyAltRight},
    {"SuperLeft", BoxKeySuperLeft},
    {"SuperRight", BoxKeySuperRight},
    {"Home", BoxKeyHome},
    {"End", BoxKeyEnd},
    {"PageUp", BoxKeyPageUp},
    {"PageDown", BoxKeyPageDown},
    {"Insert", BoxKeyInsert},
    {"CapsLock", BoxKeyCapsLock},
    {"NumLock", BoxKeyNumLock},
    {"ScrollLock", BoxKeyScrollLock},
    {"PrintScreen", BoxKeyPrintScreen},
    {"Pause", BoxKeyPause},
    {"Menu", BoxKeyMenu},
    {"World1", BoxKeyWorld1},
    {"World2", BoxKeyWorld2},
    {"Delete", BoxKeyDelete},
    {"MouseLeft", BoxKeyMouseLeft},
    {"MouseRight", BoxKeyMouseRight},
    {"Shift", BoxKeyShift},
    {"Ctrl", BoxKeyCtrl},
    {"Alt", BoxKeyAlt},
    {"Super", BoxKeySuper}
};

ecs_entity_t box_input_keys_init(
    ecs_world_t *world)
{
    ecs_entity_t type = ecs_enum_init(world, &(ecs_enum_desc_t){
        .entity = ecs_entity(world, { .name = "keys" }),
        .constants = {{ .name = box_key_names[0].name }}
    });

    ecs_entity_t scope = ecs_set_scope(world, type);
    int32_t i, count = (int32_t)(sizeof(box_key_names) / sizeof(box_key_names[0]));
    for (i = 1; i < count; i ++) {
        ecs_entity_t constant = ecs_entity(world, { .name = box_key_names[i].name });
        ecs_set_pair_second(world, constant, EcsConstant, ecs_i32_t, {
            box_key_names[i].code
        });
    }

    ecs_set_scope(world, scope);

    return type;
}
