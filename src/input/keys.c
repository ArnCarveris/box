/**
 * @file input/keys.c
 * @brief Names of the keys and the key values keyboard events carry.
 */

#include "input.h"
#include <ctype.h>
#include <string.h>

_Static_assert(KeyMouseRight + 1 == BOX_KEY_COUNT, "BOX_KEY_COUNT must match Key");

/** Name and keyboard event value of one key */
typedef struct box_input_key_info_t {
    const char *name;  /**< Constant name of the key */
    const char *value; /**< Key value of its keyboard events, NULL for a mouse button */
} box_input_key_info_t;

/** Every key, indexed by Key */
static const box_input_key_info_t box_input_keys[BOX_KEY_COUNT] = {
    [KeyNone] = { "None", "" },
    [KeyA] = { "A", "a" },
    [KeyB] = { "B", "b" },
    [KeyC] = { "C", "c" },
    [KeyD] = { "D", "d" },
    [KeyE] = { "E", "e" },
    [KeyF] = { "F", "f" },
    [KeyG] = { "G", "g" },
    [KeyH] = { "H", "h" },
    [KeyI] = { "I", "i" },
    [KeyJ] = { "J", "j" },
    [KeyK] = { "K", "k" },
    [KeyL] = { "L", "l" },
    [KeyM] = { "M", "m" },
    [KeyN] = { "N", "n" },
    [KeyO] = { "O", "o" },
    [KeyP] = { "P", "p" },
    [KeyQ] = { "Q", "q" },
    [KeyR] = { "R", "r" },
    [KeyS] = { "S", "s" },
    [KeyT] = { "T", "t" },
    [KeyU] = { "U", "u" },
    [KeyV] = { "V", "v" },
    [KeyW] = { "W", "w" },
    [KeyX] = { "X", "x" },
    [KeyY] = { "Y", "y" },
    [KeyZ] = { "Z", "z" },
    [KeyNum0] = { "Num0", "0" },
    [KeyNum1] = { "Num1", "1" },
    [KeyNum2] = { "Num2", "2" },
    [KeyNum3] = { "Num3", "3" },
    [KeyNum4] = { "Num4", "4" },
    [KeyNum5] = { "Num5", "5" },
    [KeyNum6] = { "Num6", "6" },
    [KeyNum7] = { "Num7", "7" },
    [KeyNum8] = { "Num8", "8" },
    [KeyNum9] = { "Num9", "9" },
    [KeySpace] = { "Space", " " },
    [KeyEnter] = { "Enter", "Enter" },
    [KeyEscape] = { "Escape", "Escape" },
    [KeyTab] = { "Tab", "Tab" },
    [KeyBackspace] = { "Backspace", "Backspace" },
    [KeyInsert] = { "Insert", "Insert" },
    [KeyDelete] = { "Delete", "Delete" },
    [KeyHome] = { "Home", "Home" },
    [KeyEnd] = { "End", "End" },
    [KeyPageUp] = { "PageUp", "PageUp" },
    [KeyPageDown] = { "PageDown", "PageDown" },
    [KeyLeft] = { "Left", "ArrowLeft" },
    [KeyRight] = { "Right", "ArrowRight" },
    [KeyUp] = { "Up", "ArrowUp" },
    [KeyDown] = { "Down", "ArrowDown" },
    [KeyF1] = { "F1", "F1" },
    [KeyF2] = { "F2", "F2" },
    [KeyF3] = { "F3", "F3" },
    [KeyF4] = { "F4", "F4" },
    [KeyF5] = { "F5", "F5" },
    [KeyF6] = { "F6", "F6" },
    [KeyF7] = { "F7", "F7" },
    [KeyF8] = { "F8", "F8" },
    [KeyF9] = { "F9", "F9" },
    [KeyF10] = { "F10", "F10" },
    [KeyF11] = { "F11", "F11" },
    [KeyF12] = { "F12", "F12" },
    [KeyShiftLeft] = { "ShiftLeft", "Shift" },
    [KeyShiftRight] = { "ShiftRight", "Shift" },
    [KeyCtrlLeft] = { "CtrlLeft", "Control" },
    [KeyCtrlRight] = { "CtrlRight", "Control" },
    [KeyAltLeft] = { "AltLeft", "Alt" },
    [KeyAltRight] = { "AltRight", "Alt" },
    [KeySuperLeft] = { "SuperLeft", "Meta" },
    [KeySuperRight] = { "SuperRight", "Meta" },
    [KeyCapsLock] = { "CapsLock", "CapsLock" },
    [KeyApostrophe] = { "Apostrophe", "'" },
    [KeyComma] = { "Comma", "," },
    [KeyMinus] = { "Minus", "-" },
    [KeyPeriod] = { "Period", "." },
    [KeySlash] = { "Slash", "/" },
    [KeySemicolon] = { "Semicolon", ";" },
    [KeyEqual] = { "Equal", "=" },
    [KeyBracketLeft] = { "BracketLeft", "[" },
    [KeyBackslash] = { "Backslash", "\\" },
    [KeyBracketRight] = { "BracketRight", "]" },
    [KeyGrave] = { "Grave", "`" },
    [KeyKp0] = { "Kp0", "0" },
    [KeyKp1] = { "Kp1", "1" },
    [KeyKp2] = { "Kp2", "2" },
    [KeyKp3] = { "Kp3", "3" },
    [KeyKp4] = { "Kp4", "4" },
    [KeyKp5] = { "Kp5", "5" },
    [KeyKp6] = { "Kp6", "6" },
    [KeyKp7] = { "Kp7", "7" },
    [KeyKp8] = { "Kp8", "8" },
    [KeyKp9] = { "Kp9", "9" },
    [KeyKpDecimal] = { "KpDecimal", "." },
    [KeyKpDivide] = { "KpDivide", "/" },
    [KeyKpMultiply] = { "KpMultiply", "*" },
    [KeyKpSubtract] = { "KpSubtract", "-" },
    [KeyKpAdd] = { "KpAdd", "+" },
    [KeyKpEnter] = { "KpEnter", "Enter" },
    [KeyKpEqual] = { "KpEqual", "=" },
    [KeyMouseLeft] = { "MouseLeft", NULL },
    [KeyMouseMiddle] = { "MouseMiddle", NULL },
    [KeyMouseRight] = { "MouseRight", NULL }
};

/** Compare two strings without regard to case.
 *
 * @param a The first string
 * @param b The second string
 * @return Whether the strings are equal
 */
static bool box_input_name_eq(
    const char *a,
    const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a ++;
        b ++;
    }

    return *a == *b;
}

const char* box_input_key_name(
    Key key)
{
    const char *result = NULL;
    if (key >= KeyNone && key < BOX_KEY_COUNT) {
        result = box_input_keys[key].name;
    }

    return result;
}

const char* box_input_key_value(
    Key key)
{
    const char *result = NULL;
    if (key > KeyNone && key < BOX_KEY_COUNT) {
        result = box_input_keys[key].value;
    }

    return result;
}

Key box_input_key_from_char(
    int32_t code)
{
    Key result = KeyNone;
    int32_t i;
    if (code == '\n' || code == '\r') {
        result = KeyEnter;
    } else if (code == '\t') {
        result = KeyTab;
    } else if (code > 0 && code < 128) {
        char c = (char)tolower(code);
        for (i = KeyNone + 1; i < BOX_KEY_COUNT && !result; i ++) {
            const char *value = box_input_keys[i].value;
            if (value && value[0] == c && !value[1]) {
                result = (Key)i;
            }
        }
    }

    return result;
}

Key box_input_key_from_name(
    const char *name)
{
    Key result = KeyNone;
    int32_t i;
    if (!name || !name[0]) {
        goto done;
    }

    if (!name[1]) {
        result = box_input_key_from_char((unsigned char)name[0]);
        goto done;
    }

    for (i = KeyNone + 1; i < BOX_KEY_COUNT && !result; i ++) {
        if (box_input_name_eq(name, box_input_keys[i].name)) {
            result = (Key)i;
        }
    }

    for (i = KeyNone + 1; i < BOX_KEY_COUNT && !result; i ++) {
        const char *value = box_input_keys[i].value;
        if (value && box_input_name_eq(name, value)) {
            result = (Key)i;
        }
    }

done:
    return result;
}
