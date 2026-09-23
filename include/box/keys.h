/**
 * @file box/keys.h
 * @brief Engine key codes and the modifier bits of the key masks of the action layer.
 *
 * The codes are exported as script constants under the keys scope by box.input, and
 * the platform layer translates the native codes of the OS to them.
 */

#ifndef BOX_KEYS_H
#define BOX_KEYS_H

#ifdef __cplusplus
extern "C" {
#endif

/** Slots of Input.keys: 128 key codes plus the two mouse buttons */
#define BOX_KEY_COUNT (130)

/** Mask of the key code in the u64 key masks of the action layer */
#define BOX_KEY_MASK (0xffffffffull)

/** Key mask bit of the left shift modifier */
#define BOX_KEY_MOD_SHIFT_LEFT (1ull << 32)

/** Key mask bit of the right shift modifier */
#define BOX_KEY_MOD_SHIFT_RIGHT (1ull << 33)

/** Key mask bit of the left control modifier */
#define BOX_KEY_MOD_CTRL_LEFT (1ull << 34)

/** Key mask bit of the right control modifier */
#define BOX_KEY_MOD_CTRL_RIGHT (1ull << 35)

/** Key mask bit of the left alt modifier */
#define BOX_KEY_MOD_ALT_LEFT (1ull << 36)

/** Key mask bit of the right alt modifier */
#define BOX_KEY_MOD_ALT_RIGHT (1ull << 37)

/** Key mask bit of the left super modifier */
#define BOX_KEY_MOD_SUPER_LEFT (1ull << 38)

/** Key mask bit of the right super modifier */
#define BOX_KEY_MOD_SUPER_RIGHT (1ull << 39)

/** Key mask family matching either shift */
#define BOX_KEY_MOD_SHIFT (BOX_KEY_MOD_SHIFT_LEFT | BOX_KEY_MOD_SHIFT_RIGHT)

/** Key mask family matching either control */
#define BOX_KEY_MOD_CTRL (BOX_KEY_MOD_CTRL_LEFT | BOX_KEY_MOD_CTRL_RIGHT)

/** Key mask family matching either alt */
#define BOX_KEY_MOD_ALT (BOX_KEY_MOD_ALT_LEFT | BOX_KEY_MOD_ALT_RIGHT)

/** Key mask family matching either super */
#define BOX_KEY_MOD_SUPER (BOX_KEY_MOD_SUPER_LEFT | BOX_KEY_MOD_SUPER_RIGHT)

/** Mask of every modifier bit of a key mask */
#define BOX_KEY_MOD_MASK (0xffull << 32)

/** Engine key codes, exported as script constants under the keys scope.
 *
 * Printable keys use their uppercase ASCII code (keys.W is 87, keys.Space 32); Backspace, Tab,
 * Enter, Escape and Delete keep their control code. Other keys use unused ranges: 97 to 126 for
 * arrows, modifiers and navigation, 14 to 25 for F1 to F12, the remaining gaps for the keypad.
 *
 * Mouse buttons are 128 and 129 so they bind like keys. Codes above them are mask only (family
 * modifiers and any families) and are not slots of Input.keys.
 */
typedef enum box_key_t {
    BoxKeyNone = 0,           /**< No key */
    BoxKeyKp0 = 1,            /**< Keypad 0 */
    BoxKeyKp1 = 2,            /**< Keypad 1 */
    BoxKeyKp2 = 3,            /**< Keypad 2 */
    BoxKeyKp3 = 4,            /**< Keypad 3 */
    BoxKeyKp4 = 5,            /**< Keypad 4 */
    BoxKeyKp5 = 6,            /**< Keypad 5 */
    BoxKeyKp6 = 7,            /**< Keypad 6 */
    BoxKeyBackspace = 8,      /**< Backspace, ASCII */
    BoxKeyTab = 9,            /**< Tab, ASCII */
    BoxKeyKp7 = 10,           /**< Keypad 7 */
    BoxKeyKp8 = 11,           /**< Keypad 8 */
    BoxKeyKp9 = 12,           /**< Keypad 9 */
    BoxKeyEnter = 13,         /**< Enter, ASCII */
    BoxKeyF1 = 14,            /**< Function key 1 */
    BoxKeyF2 = 15,            /**< Function key 2 */
    BoxKeyF3 = 16,            /**< Function key 3 */
    BoxKeyF4 = 17,            /**< Function key 4 */
    BoxKeyF5 = 18,            /**< Function key 5 */
    BoxKeyF6 = 19,            /**< Function key 6 */
    BoxKeyF7 = 20,            /**< Function key 7 */
    BoxKeyF8 = 21,            /**< Function key 8 */
    BoxKeyF9 = 22,            /**< Function key 9 */
    BoxKeyF10 = 23,           /**< Function key 10 */
    BoxKeyF11 = 24,           /**< Function key 11 */
    BoxKeyF12 = 25,           /**< Function key 12 */
    BoxKeyKpDecimal = 26,     /**< Keypad decimal separator */
    BoxKeyEscape = 27,        /**< Escape, ASCII */
    BoxKeyKpDivide = 28,      /**< Keypad divide */
    BoxKeyKpMultiply = 29,    /**< Keypad multiply */
    BoxKeyKpSubtract = 30,    /**< Keypad subtract */
    BoxKeyKpAdd = 31,         /**< Keypad add */
    BoxKeySpace = 32,         /**< Space, ASCII */
    BoxKeyKpEnter = 33,       /**< Keypad enter */
    BoxKeyKpEqual = 34,       /**< Keypad equal */
    BoxKeyApostrophe = 39,    /**< Apostrophe, ASCII */
    BoxKeyComma = 44,         /**< Comma, ASCII */
    BoxKeyMinus = 45,         /**< Minus, ASCII */
    BoxKeyPeriod = 46,        /**< Period, ASCII */
    BoxKeySlash = 47,         /**< Slash, ASCII */
    BoxKeyNum0 = 48,          /**< Digit 0, ASCII */
    BoxKeyNum1 = 49,          /**< Digit 1, ASCII */
    BoxKeyNum2 = 50,          /**< Digit 2, ASCII */
    BoxKeyNum3 = 51,          /**< Digit 3, ASCII */
    BoxKeyNum4 = 52,          /**< Digit 4, ASCII */
    BoxKeyNum5 = 53,          /**< Digit 5, ASCII */
    BoxKeyNum6 = 54,          /**< Digit 6, ASCII */
    BoxKeyNum7 = 55,          /**< Digit 7, ASCII */
    BoxKeyNum8 = 56,          /**< Digit 8, ASCII */
    BoxKeyNum9 = 57,          /**< Digit 9, ASCII */
    BoxKeySemicolon = 59,     /**< Semicolon, ASCII */
    BoxKeyEqual = 61,         /**< Equal, ASCII */
    BoxKeyA = 65,             /**< Letter A, ASCII */
    BoxKeyB = 66,             /**< Letter B, ASCII */
    BoxKeyC = 67,             /**< Letter C, ASCII */
    BoxKeyD = 68,             /**< Letter D, ASCII */
    BoxKeyE = 69,             /**< Letter E, ASCII */
    BoxKeyF = 70,             /**< Letter F, ASCII */
    BoxKeyG = 71,             /**< Letter G, ASCII */
    BoxKeyH = 72,             /**< Letter H, ASCII */
    BoxKeyI = 73,             /**< Letter I, ASCII */
    BoxKeyJ = 74,             /**< Letter J, ASCII */
    BoxKeyK = 75,             /**< Letter K, ASCII */
    BoxKeyL = 76,             /**< Letter L, ASCII */
    BoxKeyM = 77,             /**< Letter M, ASCII */
    BoxKeyN = 78,             /**< Letter N, ASCII */
    BoxKeyO = 79,             /**< Letter O, ASCII */
    BoxKeyP = 80,             /**< Letter P, ASCII */
    BoxKeyQ = 81,             /**< Letter Q, ASCII */
    BoxKeyR = 82,             /**< Letter R, ASCII */
    BoxKeyS = 83,             /**< Letter S, ASCII */
    BoxKeyT = 84,             /**< Letter T, ASCII */
    BoxKeyU = 85,             /**< Letter U, ASCII */
    BoxKeyV = 86,             /**< Letter V, ASCII */
    BoxKeyW = 87,             /**< Letter W, ASCII */
    BoxKeyX = 88,             /**< Letter X, ASCII */
    BoxKeyY = 89,             /**< Letter Y, ASCII */
    BoxKeyZ = 90,             /**< Letter Z, ASCII */
    BoxKeyLeftBracket = 91,   /**< Left bracket, ASCII */
    BoxKeyBackslash = 92,     /**< Backslash, ASCII */
    BoxKeyRightBracket = 93,  /**< Right bracket, ASCII */
    BoxKeyGrave = 96,         /**< Grave accent, ASCII */
    BoxKeyLeft = 97,          /**< Left arrow */
    BoxKeyRight = 98,         /**< Right arrow */
    BoxKeyUp = 99,            /**< Up arrow */
    BoxKeyDown = 100,         /**< Down arrow */
    BoxKeyShiftLeft = 101,    /**< Left shift */
    BoxKeyShiftRight = 102,   /**< Right shift */
    BoxKeyCtrlLeft = 103,     /**< Left control */
    BoxKeyCtrlRight = 104,    /**< Right control */
    BoxKeyAltLeft = 105,      /**< Left alt */
    BoxKeyAltRight = 106,     /**< Right alt */
    BoxKeySuperLeft = 107,    /**< Left super */
    BoxKeySuperRight = 108,   /**< Right super */
    BoxKeyHome = 109,         /**< Home */
    BoxKeyEnd = 110,          /**< End */
    BoxKeyPageUp = 111,       /**< Page up */
    BoxKeyPageDown = 112,     /**< Page down */
    BoxKeyInsert = 113,       /**< Insert */
    BoxKeyCapsLock = 114,     /**< Caps lock */
    BoxKeyNumLock = 115,      /**< Num lock */
    BoxKeyScrollLock = 116,   /**< Scroll lock */
    BoxKeyPrintScreen = 117,  /**< Print screen */
    BoxKeyPause = 118,        /**< Pause */
    BoxKeyMenu = 119,         /**< Menu */
    BoxKeyWorld1 = 120,       /**< Non US key 1 */
    BoxKeyWorld2 = 121,       /**< Non US key 2 */
    BoxKeyDelete = 127,       /**< Delete, ASCII */
    BoxKeyMouseLeft = 128,    /**< Left mouse button */
    BoxKeyMouseRight = 129,   /**< Right mouse button */
    BoxKeyShift = 130,        /**< Either shift, mask only */
    BoxKeyCtrl = 131,         /**< Either control, mask only */
    BoxKeyAlt = 132,          /**< Either alt, mask only */
    BoxKeySuper = 133,        /**< Either super, mask only */
    BoxKeyAny = 134,          /**< Any key or mouse button, mask only */
    BoxKeyAnyKey = 135,       /**< Any key of the keyboard, mask only */
    BoxKeyAnyMouse = 136      /**< Any mouse button, mask only */
} box_key_t;

#ifdef __cplusplus
}
#endif

#endif
