/**
 * @file input/platform.c
 * @brief Translates glfw and browser key codes and callbacks into platform input events.
 *
 * Plain functions without platform headers, so the translation runs and is tested anywhere.
 */

#include "input.h"
#include <string.h>

/** glfw action of a released key or button */
#define BOX_GLFW_RELEASE (0)

/** glfw action of a key that the platform repeats while it is held */
#define BOX_GLFW_REPEAT (2)

/** Wheel pixels of one notch */
#define BOX_DOM_WHEEL_PIXELS (100.0)

/** Wheel lines of one notch */
#define BOX_DOM_WHEEL_LINES (3.0)

/** A platform key code and the key it maps to */
typedef struct box_input_code_t {
    int32_t code; /**< glfw key code */
    Key key;      /**< The key */
} box_input_code_t;

/** A browser key code and the key it maps to */
typedef struct box_input_dom_code_t {
    const char *code; /**< KeyboardEvent.code value */
    Key key;          /**< The key */
} box_input_dom_code_t;

/** glfw key codes outside the letter, digit, function and keypad digit ranges */
static const box_input_code_t box_input_glfw_codes[] = {
    { 32, KeySpace },
    { 39, KeyApostrophe },
    { 44, KeyComma },
    { 45, KeyMinus },
    { 46, KeyPeriod },
    { 47, KeySlash },
    { 59, KeySemicolon },
    { 61, KeyEqual },
    { 91, KeyBracketLeft },
    { 92, KeyBackslash },
    { 93, KeyBracketRight },
    { 96, KeyGrave },
    { 256, KeyEscape },
    { 257, KeyEnter },
    { 258, KeyTab },
    { 259, KeyBackspace },
    { 260, KeyInsert },
    { 261, KeyDelete },
    { 262, KeyRight },
    { 263, KeyLeft },
    { 264, KeyDown },
    { 265, KeyUp },
    { 266, KeyPageUp },
    { 267, KeyPageDown },
    { 268, KeyHome },
    { 269, KeyEnd },
    { 280, KeyCapsLock },
    { 330, KeyKpDecimal },
    { 331, KeyKpDivide },
    { 332, KeyKpMultiply },
    { 333, KeyKpSubtract },
    { 334, KeyKpAdd },
    { 335, KeyKpEnter },
    { 336, KeyKpEqual },
    { 340, KeyShiftLeft },
    { 341, KeyCtrlLeft },
    { 342, KeyAltLeft },
    { 343, KeySuperLeft },
    { 344, KeyShiftRight },
    { 345, KeyCtrlRight },
    { 346, KeyAltRight },
    { 347, KeySuperRight }
};

/** Browser key codes outside the letter, digit, function and keypad digit ranges */
static const box_input_dom_code_t box_input_dom_codes[] = {
    { "Space", KeySpace },
    { "Enter", KeyEnter },
    { "Escape", KeyEscape },
    { "Tab", KeyTab },
    { "Backspace", KeyBackspace },
    { "Insert", KeyInsert },
    { "Delete", KeyDelete },
    { "Home", KeyHome },
    { "End", KeyEnd },
    { "PageUp", KeyPageUp },
    { "PageDown", KeyPageDown },
    { "ArrowLeft", KeyLeft },
    { "ArrowRight", KeyRight },
    { "ArrowUp", KeyUp },
    { "ArrowDown", KeyDown },
    { "ShiftLeft", KeyShiftLeft },
    { "ShiftRight", KeyShiftRight },
    { "ControlLeft", KeyCtrlLeft },
    { "ControlRight", KeyCtrlRight },
    { "AltLeft", KeyAltLeft },
    { "AltRight", KeyAltRight },
    { "MetaLeft", KeySuperLeft },
    { "MetaRight", KeySuperRight },
    { "OSLeft", KeySuperLeft },
    { "OSRight", KeySuperRight },
    { "CapsLock", KeyCapsLock },
    { "Quote", KeyApostrophe },
    { "Comma", KeyComma },
    { "Minus", KeyMinus },
    { "Period", KeyPeriod },
    { "Slash", KeySlash },
    { "Semicolon", KeySemicolon },
    { "Equal", KeyEqual },
    { "BracketLeft", KeyBracketLeft },
    { "Backslash", KeyBackslash },
    { "BracketRight", KeyBracketRight },
    { "Backquote", KeyGrave },
    { "NumpadDecimal", KeyKpDecimal },
    { "NumpadDivide", KeyKpDivide },
    { "NumpadMultiply", KeyKpMultiply },
    { "NumpadSubtract", KeyKpSubtract },
    { "NumpadAdd", KeyKpAdd },
    { "NumpadEnter", KeyKpEnter },
    { "NumpadEqual", KeyKpEqual }
};

Key box_input_glfw_key(
    int32_t key)
{
    Key result = KeyNone;
    int32_t i, count = (int32_t)(sizeof(box_input_glfw_codes) / sizeof(box_input_glfw_codes[0]));
    if (key >= 'A' && key <= 'Z') {
        result = (Key)(KeyA + (key - 'A'));
    } else if (key >= '0' && key <= '9') {
        result = (Key)(KeyNum0 + (key - '0'));
    } else if (key >= 290 && key <= 301) {
        result = (Key)(KeyF1 + (key - 290));
    } else if (key >= 320 && key <= 329) {
        result = (Key)(KeyKp0 + (key - 320));
    }

    for (i = 0; i < count && !result; i ++) {
        if (box_input_glfw_codes[i].code == key) {
            result = box_input_glfw_codes[i].key;
        }
    }

    return result;
}

Key box_input_glfw_button(
    int32_t button)
{
    Key result = KeyNone;
    if (button == 0) {
        result = KeyMouseLeft;
    } else if (button == 1) {
        result = KeyMouseRight;
    } else if (button == 2) {
        result = KeyMouseMiddle;
    }

    return result;
}

/** Push a key or button event of a platform.
 *
 * @param world The world
 * @param key The key, nothing is pushed for KeyNone
 * @param down Whether the key went down
 * @param repeat Whether the platform repeats a held key
 */
static void box_input_platform_key(
    ecs_world_t *world,
    Key key,
    bool down,
    bool repeat)
{
    if (key != KeyNone) {
        box_input_platform_push(world, &(InputEvent){
            .kind = down ? InputKeyDown : InputKeyUp,
            .key = key,
            .repeat = down && repeat
        });
    }
}

void box_input_glfw_on_key(
    ecs_world_t *world,
    int32_t key,
    int32_t action)
{
    box_input_platform_key(world, box_input_glfw_key(key),
        action != BOX_GLFW_RELEASE, action == BOX_GLFW_REPEAT);
}

void box_input_glfw_on_button(
    ecs_world_t *world,
    int32_t button,
    int32_t action)
{
    box_input_platform_key(world, box_input_glfw_button(button),
        action != BOX_GLFW_RELEASE, false);
}

/** Read a number of one or two digits that ends a string.
 *
 * @param str The digits
 * @return The number, -1 when the string is not one or two digits
 */
static int32_t box_input_dom_number(
    const char *str)
{
    int32_t result = -1;
    size_t len = strlen(str);
    if (len < 1 || len > 2) {
        goto done;
    }

    if (str[0] < '0' || str[0] > '9' || (len == 2 && (str[1] < '0' || str[1] > '9'))) {
        goto done;
    }

    result = len == 2 ? (str[0] - '0') * 10 + (str[1] - '0') : str[0] - '0';
done:
    return result;
}

Key box_input_dom_key(
    const char *code)
{
    Key result = KeyNone;
    int32_t i, n;
    int32_t count = (int32_t)(sizeof(box_input_dom_codes) / sizeof(box_input_dom_codes[0]));
    if (!code) {
        goto done;
    }

    if (!strncmp(code, "Key", 3) && code[3] >= 'A' && code[3] <= 'Z' && !code[4]) {
        result = (Key)(KeyA + (code[3] - 'A'));
    } else if (!strncmp(code, "Digit", 5) && (n = box_input_dom_number(&code[5])) >= 0) {
        result = n <= 9 ? (Key)(KeyNum0 + n) : KeyNone;
    } else if (!strncmp(code, "Numpad", 6) && (n = box_input_dom_number(&code[6])) >= 0) {
        result = n <= 9 ? (Key)(KeyKp0 + n) : KeyNone;
    } else if (code[0] == 'F' && (n = box_input_dom_number(&code[1])) >= 0) {
        result = n >= 1 && n <= 12 ? (Key)(KeyF1 + n - 1) : KeyNone;
    }

    for (i = 0; i < count && !result; i ++) {
        if (!strcmp(box_input_dom_codes[i].code, code)) {
            result = box_input_dom_codes[i].key;
        }
    }

done:
    return result;
}

Key box_input_dom_button(
    int32_t button)
{
    Key result = KeyNone;
    if (button == 0) {
        result = KeyMouseLeft;
    } else if (button == 1) {
        result = KeyMouseMiddle;
    } else if (button == 2) {
        result = KeyMouseRight;
    }

    return result;
}

/** Decode a key value that is one printable character.
 *
 * @param key The UTF-8 KeyboardEvent.key value
 * @return The code point, 0 when the value names a key such as Enter or is not printable
 */
static int32_t box_input_dom_char(
    const char *key)
{
    const unsigned char *ptr = (const unsigned char*)key;
    int32_t result = 0, size = 0, i;
    if (!key || !key[0]) {
        goto done;
    }

    if (ptr[0] < 0x80) {
        result = ptr[0];
        size = 1;
    } else if ((ptr[0] & 0xe0) == 0xc0) {
        result = ptr[0] & 0x1f;
        size = 2;
    } else if ((ptr[0] & 0xf0) == 0xe0) {
        result = ptr[0] & 0x0f;
        size = 3;
    } else if ((ptr[0] & 0xf8) == 0xf0) {
        result = ptr[0] & 0x07;
        size = 4;
    }

    for (i = 1; i < size; i ++) {
        if ((ptr[i] & 0xc0) != 0x80) {
            size = 0;
            break;
        }

        result = (result << 6) | (ptr[i] & 0x3f);
    }

    if (!size || ptr[size] || result < 0x20 || result == 0x7f) {
        result = 0;
    }

done:
    return result;
}

bool box_input_dom_on_key(
    ecs_world_t *world,
    const char *code,
    const char *key,
    bool down,
    bool repeat,
    bool shortcut)
{
    Key k = box_input_dom_key(code);
    int32_t text = down && !shortcut ? box_input_dom_char(key) : 0;
    bool function = k >= KeyF1 && k <= KeyF12;
    box_input_platform_key(world, k, down, repeat);
    if (text) {
        box_input_platform_push(world, &(InputEvent){ .kind = InputText, .code = text });
    }

    return k != KeyNone && !function && !shortcut;
}

void box_input_dom_on_wheel(
    ecs_world_t *world,
    double dx,
    double dy,
    int32_t mode)
{
    double scale = 1.0;
    if (mode == 0) {
        scale = 1.0 / BOX_DOM_WHEEL_PIXELS;
    } else if (mode == 1) {
        scale = 1.0 / BOX_DOM_WHEEL_LINES;
    }

    box_input_platform_push(world, &(InputEvent){
        .kind = InputScroll,
        .value = { (float)(-dx * scale), (float)(-dy * scale) }
    });
}

void box_input_dom_on_move(
    ecs_world_t *world,
    box_input_dom_pointer_t *pointer,
    const vec2 position,
    const vec2 movement)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        pointer->cursor[i] = pointer->locked ? pointer->cursor[i] + movement[i] : position[i];
    }

    box_input_platform_push(world, &(InputEvent){
        .kind = InputMove, .value = { pointer->cursor[0], pointer->cursor[1] } });
}

bool box_input_dom_on_lock(
    ecs_world_t *world,
    box_input_dom_pointer_t *pointer,
    bool locked,
    bool page_focused)
{
    bool was = pointer->locked;
    bool result = locked && !pointer->want;
    pointer->locked = locked;
    if (locked != was && !result) {
        box_input_platform_push(world, &(InputEvent){ .kind = InputCaptured, .code = locked });
    }

    if (was && !locked && pointer->want && page_focused) {
        box_input_platform_push(world, &(InputEvent){ .kind = InputCapture, .code = 0 });
    }

    return result;
}

void box_input_dom_on_lock_error(
    ecs_world_t *world,
    box_input_dom_pointer_t *pointer,
    bool page_focused)
{
    if (pointer->want && page_focused) {
        box_input_platform_push(world, &(InputEvent){ .kind = InputCapture, .code = 0 });
    }
}
