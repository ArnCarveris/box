/**
 * @file platform/platform_web.c
 * @brief Canvas backed window implementation for Emscripten.
 */

#ifdef BOX_PLATFORM_WEB

#include "platform.h"
#include <box/keys.h>
#include <emscripten/html5.h>
#include <emscripten/emscripten.h>
#include <stdlib.h>

/** Window state on web */
struct box_window_t {
    const char *canvas;    /**< Canvas selector */
    int32_t px_width;      /**< Backing store width */
    int32_t px_height;     /**< Backing store height */
    double pt_width;       /**< CSS width */
    double pt_height;      /**< CSS height */
    box_input_fn input_fn; /**< Sink the input events are reported to */
    void *input_ctx;       /**< Context of the sink */
    float cursor_x;        /**< Cursor x in CSS pixels, moved by the pointer lock */
    float cursor_y;        /**< Cursor y in CSS pixels, moved by the pointer lock */
    bool captured;         /**< Pointer lock was requested for the canvas */
};

/** Size the canvas backing store to its CSS size times the device pixel ratio.
 *
 * @param w The window
 */
static void box_window_sync_size(
    box_window_t *w)
{
    emscripten_get_element_css_size(w->canvas, &w->pt_width, &w->pt_height);
    double ratio = emscripten_get_device_pixel_ratio();
    int32_t px_width = (int32_t)(w->pt_width * ratio);
    int32_t px_height = (int32_t)(w->pt_height * ratio);
    if (px_width != w->px_width || px_height != w->px_height) {
        w->px_width = px_width;
        w->px_height = px_height;
        emscripten_set_canvas_element_size(w->canvas, px_width, px_height);
    }
}

/** Report an event to the sink of the window, when it has one.
 *
 * @param w The window
 * @param kind What the event does
 * @param code Key code, button code, code point or boolean flag
 * @param x Cursor x in window points, or scroll x
 * @param y Cursor y in window points, or scroll y
 */
static void box_web_emit(
    box_window_t *w,
    box_input_kind_t kind,
    int32_t code,
    float x,
    float y)
{
    if (w && w->input_fn) {
        box_input_event_t event = { .kind = kind, .code = code, .x = x, .y = y };
        w->input_fn(w->input_ctx, &event);
    }
}

/** Decode the key field of a browser event when it holds one code point.
 *
 * @param key The UTF-8 key field
 * @return The code point, 0 when the field names a key instead
 */
static int32_t box_web_codepoint(
    const char *key)
{
    unsigned char lead = (unsigned char)key[0];
    int32_t result = 0, size = 0;
    if (lead < 0x80) {
        result = lead;
        size = 1;
    } else if ((lead & 0xe0) == 0xc0) {
        result = lead & 0x1f;
        size = 2;
    } else if ((lead & 0xf0) == 0xe0) {
        result = lead & 0x0f;
        size = 3;
    } else if ((lead & 0xf8) == 0xf0) {
        result = lead & 0x07;
        size = 4;
    }

    int32_t i;
    for (i = 1; i < size; i ++) {
        result = (result << 6) | (key[i] & 0x3f);
    }

    if (!size || key[size] != '\0' || result < 0x20 || result == 0x7f) {
        result = 0;
    }

    return result;
}

/** Pick the right hand side of a modifier when the browser reports one.
 *
 * @param code The engine key code of the left hand side
 * @param location The DOM key location
 * @return The engine key code of the side the event came from
 */
static int32_t box_web_key_side(
    int32_t code,
    unsigned int location)
{
    int32_t result = code;
    if (location == DOM_KEY_LOCATION_RIGHT) {
        switch (code) {
        case BoxKeyShiftLeft: result = BoxKeyShiftRight; break;
        case BoxKeyCtrlLeft: result = BoxKeyCtrlRight; break;
        case BoxKeyAltLeft: result = BoxKeyAltRight; break;
        case BoxKeySuperLeft: result = BoxKeySuperRight; break;
        default: break;
        }
    }

    return result;
}

/** Key callback: report the key and the character it typed.
 *
 * @param type The browser event type
 * @param event The keyboard event
 * @param ctx The window
 * @return true when the browser must not act on the event
 */
static bool box_web_key(
    int type,
    const EmscriptenKeyboardEvent *event,
    void *ctx)
{
    box_window_t *w = ctx;
    int32_t code = box_web_key_side(
        box_platform_key_from_native((int32_t)event->keyCode), event->location);
    bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;
    if (code && !event->repeat) {
        box_web_emit(w, down ? BoxInputKeyDown : BoxInputKeyUp, code, 0, 0);
    }

    int32_t point = down ? box_web_codepoint(event->key) : 0;
    if (point) {
        box_web_emit(w, BoxInputChar, point, 0, 0);
    }

    return code && !event->ctrlKey && !event->metaKey;
}

/** Move callback: report the cursor, following the pointer lock when it is held.
 *
 * @param type The browser event type
 * @param event The mouse event
 * @param ctx The window
 * @return false, the browser keeps acting on the event
 */
static bool box_web_move(
    int type,
    const EmscriptenMouseEvent *event,
    void *ctx)
{
    (void)type;
    box_window_t *w = ctx;
    if (w->captured) {
        w->cursor_x += (float)event->movementX;
        w->cursor_y += (float)event->movementY;
    } else {
        w->cursor_x = (float)event->targetX;
        w->cursor_y = (float)event->targetY;
    }

    box_web_emit(w, BoxInputMouseMove, 0, w->cursor_x, w->cursor_y);

    return false;
}

/** Button callback: report the left and the right mouse button.
 *
 * @param type The browser event type
 * @param event The mouse event
 * @param ctx The window
 * @return true when a button of the engine was reported
 */
static bool box_web_button(
    int type,
    const EmscriptenMouseEvent *event,
    void *ctx)
{
    box_window_t *w = ctx;
    int32_t code = 0;
    if (event->button == 0) {
        code = BoxKeyMouseLeft;
    } else if (event->button == 2) {
        code = BoxKeyMouseRight;
    }

    if (code) {
        bool down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
        box_web_emit(w, down ? BoxInputButtonDown : BoxInputButtonUp, code, 0, 0);
    }

    return code != 0;
}

/** Wheel callback: report the wheel offset in notches, positive upwards.
 *
 * @param type The browser event type
 * @param event The wheel event
 * @param ctx The window
 * @return true, the page does not scroll with the canvas
 */
static bool box_web_wheel(
    int type,
    const EmscriptenWheelEvent *event,
    void *ctx)
{
    (void)type;
    box_window_t *w = ctx;
    float scale = 1.0f;
    if (event->deltaMode == DOM_DELTA_PIXEL) {
        scale = 0.01f;
    } else if (event->deltaMode == DOM_DELTA_PAGE) {
        scale = 3.0f;
    }

    box_web_emit(w, BoxInputScroll, 0,
        (float)event->deltaX * scale, (float)-event->deltaY * scale);

    return true;
}

/** Focus callback: report that the page gained or lost focus.
 *
 * @param type The browser event type
 * @param event The focus event
 * @param ctx The window
 * @return false, the browser keeps acting on the event
 */
static bool box_web_focus(
    int type,
    const EmscriptenFocusEvent *event,
    void *ctx)
{
    (void)event;
    box_web_emit(ctx, BoxInputFocus, type == EMSCRIPTEN_EVENT_FOCUS ? 1 : 0, 0, 0);

    return false;
}

/** Visibility callback: report a hidden page the way a minimized window is reported.
 *
 * @param type The browser event type
 * @param event The visibility event
 * @param ctx The window
 * @return false, the browser keeps acting on the event
 */
static bool box_web_visibility(
    int type,
    const EmscriptenVisibilityChangeEvent *event,
    void *ctx)
{
    (void)type;
    box_web_emit(ctx, BoxInputMinimize, event->hidden ? 1 : 0, 0, 0);

    return false;
}

/** Register the browser callbacks: pointer events on the canvas, keys on the window.
 *
 * @param w The window
 */
static void box_web_callbacks(
    box_window_t *w)
{
    const char *window_target = EMSCRIPTEN_EVENT_TARGET_WINDOW;
    emscripten_set_keydown_callback(window_target, w, false, box_web_key);
    emscripten_set_keyup_callback(window_target, w, false, box_web_key);
    emscripten_set_focus_callback(window_target, w, false, box_web_focus);
    emscripten_set_blur_callback(window_target, w, false, box_web_focus);
    emscripten_set_visibilitychange_callback(w, false, box_web_visibility);
    emscripten_set_mousemove_callback(w->canvas, w, false, box_web_move);
    emscripten_set_mousedown_callback(w->canvas, w, false, box_web_button);
    emscripten_set_mouseup_callback(w->canvas, w, false, box_web_button);
    emscripten_set_wheel_callback(w->canvas, w, false, box_web_wheel);
}

int32_t box_platform_key_from_native(
    int32_t native)
{
    int32_t result = BoxKeyNone;
    bool digit = native >= BoxKeyNum0 && native <= BoxKeyNum9;
    if (digit || (native >= BoxKeyA && native <= BoxKeyZ)) {
        result = native;
    } else {
        switch (native) {
        case 8: result = BoxKeyBackspace; break;
        case 9: result = BoxKeyTab; break;
        case 13: result = BoxKeyEnter; break;
        case 16: result = BoxKeyShiftLeft; break;
        case 17: result = BoxKeyCtrlLeft; break;
        case 18: result = BoxKeyAltLeft; break;
        case 19: result = BoxKeyPause; break;
        case 20: result = BoxKeyCapsLock; break;
        case 27: result = BoxKeyEscape; break;
        case 32: result = BoxKeySpace; break;
        case 33: result = BoxKeyPageUp; break;
        case 34: result = BoxKeyPageDown; break;
        case 35: result = BoxKeyEnd; break;
        case 36: result = BoxKeyHome; break;
        case 37: result = BoxKeyLeft; break;
        case 38: result = BoxKeyUp; break;
        case 39: result = BoxKeyRight; break;
        case 40: result = BoxKeyDown; break;
        case 44: result = BoxKeyPrintScreen; break;
        case 45: result = BoxKeyInsert; break;
        case 46: result = BoxKeyDelete; break;
        case 91: result = BoxKeySuperLeft; break;
        case 92: result = BoxKeySuperRight; break;
        case 93: result = BoxKeyMenu; break;
        case 96: result = BoxKeyKp0; break;
        case 97: result = BoxKeyKp1; break;
        case 98: result = BoxKeyKp2; break;
        case 99: result = BoxKeyKp3; break;
        case 100: result = BoxKeyKp4; break;
        case 101: result = BoxKeyKp5; break;
        case 102: result = BoxKeyKp6; break;
        case 103: result = BoxKeyKp7; break;
        case 104: result = BoxKeyKp8; break;
        case 105: result = BoxKeyKp9; break;
        case 106: result = BoxKeyKpMultiply; break;
        case 107: result = BoxKeyKpAdd; break;
        case 109: result = BoxKeyKpSubtract; break;
        case 110: result = BoxKeyKpDecimal; break;
        case 111: result = BoxKeyKpDivide; break;
        case 112: result = BoxKeyF1; break;
        case 113: result = BoxKeyF2; break;
        case 114: result = BoxKeyF3; break;
        case 115: result = BoxKeyF4; break;
        case 116: result = BoxKeyF5; break;
        case 117: result = BoxKeyF6; break;
        case 118: result = BoxKeyF7; break;
        case 119: result = BoxKeyF8; break;
        case 120: result = BoxKeyF9; break;
        case 121: result = BoxKeyF10; break;
        case 122: result = BoxKeyF11; break;
        case 123: result = BoxKeyF12; break;
        case 144: result = BoxKeyNumLock; break;
        case 145: result = BoxKeyScrollLock; break;
        case 186: result = BoxKeySemicolon; break;
        case 187: result = BoxKeyEqual; break;
        case 188: result = BoxKeyComma; break;
        case 189: result = BoxKeyMinus; break;
        case 190: result = BoxKeyPeriod; break;
        case 191: result = BoxKeySlash; break;
        case 192: result = BoxKeyGrave; break;
        case 219: result = BoxKeyLeftBracket; break;
        case 220: result = BoxKeyBackslash; break;
        case 221: result = BoxKeyRightBracket; break;
        case 222: result = BoxKeyApostrophe; break;
        default: break;
        }
    }

    return result;
}

box_window_t* box_window_create(
    const box_window_desc_t *desc)
{
    (void)desc;
    box_window_t *w = calloc(1, sizeof(box_window_t));
    w->canvas = "#canvas";
    box_window_sync_size(w);
    box_web_callbacks(w);

    return w;
}

void box_window_destroy(
    box_window_t *w)
{
    free(w);
}

void box_window_poll(
    box_window_t *w)
{
    box_window_sync_size(w);
}

bool box_window_should_close(
    const box_window_t *w)
{
    (void)w;

    return false;
}

void box_window_size(
    const box_window_t *w,
    int32_t *px_width,
    int32_t *px_height,
    int32_t *pt_width,
    int32_t *pt_height)
{
    *px_width = w->px_width;
    *px_height = w->px_height;
    *pt_width = (int32_t)w->pt_width;
    *pt_height = (int32_t)w->pt_height;
}

void box_window_set_title(
    box_window_t *w,
    const char *title)
{
    (void)w;
    emscripten_set_window_title(title);
}

void box_window_set_size(
    box_window_t *w,
    int32_t width,
    int32_t height)
{
    (void)w; (void)width; (void)height;
}

void box_window_set_input(
    box_window_t *w,
    box_input_fn fn,
    void *ctx)
{
    w->input_fn = fn;
    w->input_ctx = ctx;
}

void box_window_set_cursor_captured(
    box_window_t *w,
    bool captured)
{
    w->captured = captured;
    if (captured) {
        emscripten_request_pointerlock(w->canvas, true);
    } else {
        emscripten_exit_pointerlock();
    }
}

void* box_window_metal_layer(
    box_window_t *w)
{
    (void)w;

    return NULL;
}

const char* box_window_canvas(
    const box_window_t *w)
{
    return w->canvas;
}

#endif
