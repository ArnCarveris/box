/**
 * @file platform/platform_glfw.c
 * @brief GLFW window implementation for desktop platforms.
 */

#ifdef BOX_PLATFORM_GLFW

#include "platform.h"
#include <box/keys.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <stdio.h>
#include <stdlib.h>

/** Window state on desktop */
struct box_window_t {
    GLFWwindow *window;    /**< GLFW window */
    void *metal_layer;     /**< CAMetalLayer on macOS */
    box_input_fn input_fn; /**< Sink the input events are reported to */
    void *input_ctx;       /**< Context of the sink */
};

/** Number of windows alive, GLFW is terminated when it reaches zero */
static int32_t box_window_count = 0;

#ifdef __APPLE__
void* box_macos_metal_layer(
    void *ns_window);
#endif

/** Report an event to the sink of a window, when it has one.
 *
 * @param window The GLFW window
 * @param kind What the event does
 * @param code Key code, button code, code point or boolean flag
 * @param x Cursor x in window points, or scroll x
 * @param y Cursor y in window points, or scroll y
 */
static void box_glfw_emit(
    GLFWwindow *window,
    box_input_kind_t kind,
    int32_t code,
    float x,
    float y)
{
    box_window_t *w = glfwGetWindowUserPointer(window);
    if (w && w->input_fn) {
        box_input_event_t event = { .kind = kind, .code = code, .x = x, .y = y };
        w->input_fn(w->input_ctx, &event);
    }
}

/** Key callback: report the press and release of a mapped key.
 *
 * @param window The GLFW window
 * @param key The GLFW key code
 * @param scancode The platform scancode
 * @param action Press, release or repeat
 * @param mods Modifier bits
 */
static void box_glfw_key(
    GLFWwindow *window,
    int key,
    int scancode,
    int action,
    int mods)
{
    (void)scancode;
    (void)mods;
    int32_t code = box_platform_key_from_native(key);
    if (code && action != GLFW_REPEAT) {
        box_input_kind_t kind = action == GLFW_PRESS ? BoxInputKeyDown : BoxInputKeyUp;
        box_glfw_emit(window, kind, code, 0, 0);
    }
}

/** Character callback: report a typed code point.
 *
 * @param window The GLFW window
 * @param codepoint The unicode code point
 */
static void box_glfw_char(
    GLFWwindow *window,
    unsigned int codepoint)
{
    box_glfw_emit(window, BoxInputChar, (int32_t)codepoint, 0, 0);
}

/** Cursor callback: report the cursor position in window points.
 *
 * @param window The GLFW window
 * @param x Cursor x
 * @param y Cursor y
 */
static void box_glfw_cursor(
    GLFWwindow *window,
    double x,
    double y)
{
    box_glfw_emit(window, BoxInputMouseMove, 0, (float)x, (float)y);
}

/** Button callback: report the left and the right mouse button.
 *
 * @param window The GLFW window
 * @param button The GLFW button
 * @param action Press or release
 * @param mods Modifier bits
 */
static void box_glfw_button(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    (void)mods;
    int32_t code = 0;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        code = BoxKeyMouseLeft;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        code = BoxKeyMouseRight;
    }

    if (code) {
        box_input_kind_t kind = action == GLFW_PRESS ? BoxInputButtonDown : BoxInputButtonUp;
        box_glfw_emit(window, kind, code, 0, 0);
    }
}

/** Scroll callback: report the wheel offset.
 *
 * @param window The GLFW window
 * @param x Horizontal offset
 * @param y Vertical offset
 */
static void box_glfw_scroll(
    GLFWwindow *window,
    double x,
    double y)
{
    box_glfw_emit(window, BoxInputScroll, 0, (float)x, (float)y);
}

/** Focus callback: report that the window gained or lost keyboard focus.
 *
 * @param window The GLFW window
 * @param focused Nonzero when the window has focus
 */
static void box_glfw_focus(
    GLFWwindow *window,
    int focused)
{
    box_glfw_emit(window, BoxInputFocus, focused ? 1 : 0, 0, 0);
}

/** Iconify callback: report that the window was minimized or restored.
 *
 * @param window The GLFW window
 * @param iconified Nonzero when the window is minimized
 */
static void box_glfw_iconify(
    GLFWwindow *window,
    int iconified)
{
    box_glfw_emit(window, BoxInputMinimize, iconified ? 1 : 0, 0, 0);
}

/** Install the event callbacks on a window.
 *
 * @param window The GLFW window
 */
static void box_glfw_callbacks(
    GLFWwindow *window)
{
    glfwSetKeyCallback(window, box_glfw_key);
    glfwSetCharCallback(window, box_glfw_char);
    glfwSetCursorPosCallback(window, box_glfw_cursor);
    glfwSetMouseButtonCallback(window, box_glfw_button);
    glfwSetScrollCallback(window, box_glfw_scroll);
    glfwSetWindowFocusCallback(window, box_glfw_focus);
    glfwSetWindowIconifyCallback(window, box_glfw_iconify);
}

int32_t box_platform_key_from_native(
    int32_t native)
{
    int32_t result = BoxKeyNone;
    if (native >= GLFW_KEY_SPACE && native <= GLFW_KEY_GRAVE_ACCENT) {
        result = native;
    } else {
        switch (native) {
        case GLFW_KEY_WORLD_1: result = BoxKeyWorld1; break;
        case GLFW_KEY_WORLD_2: result = BoxKeyWorld2; break;
        case GLFW_KEY_ESCAPE: result = BoxKeyEscape; break;
        case GLFW_KEY_ENTER: result = BoxKeyEnter; break;
        case GLFW_KEY_TAB: result = BoxKeyTab; break;
        case GLFW_KEY_BACKSPACE: result = BoxKeyBackspace; break;
        case GLFW_KEY_INSERT: result = BoxKeyInsert; break;
        case GLFW_KEY_DELETE: result = BoxKeyDelete; break;
        case GLFW_KEY_RIGHT: result = BoxKeyRight; break;
        case GLFW_KEY_LEFT: result = BoxKeyLeft; break;
        case GLFW_KEY_DOWN: result = BoxKeyDown; break;
        case GLFW_KEY_UP: result = BoxKeyUp; break;
        case GLFW_KEY_PAGE_UP: result = BoxKeyPageUp; break;
        case GLFW_KEY_PAGE_DOWN: result = BoxKeyPageDown; break;
        case GLFW_KEY_HOME: result = BoxKeyHome; break;
        case GLFW_KEY_END: result = BoxKeyEnd; break;
        case GLFW_KEY_CAPS_LOCK: result = BoxKeyCapsLock; break;
        case GLFW_KEY_SCROLL_LOCK: result = BoxKeyScrollLock; break;
        case GLFW_KEY_NUM_LOCK: result = BoxKeyNumLock; break;
        case GLFW_KEY_PRINT_SCREEN: result = BoxKeyPrintScreen; break;
        case GLFW_KEY_PAUSE: result = BoxKeyPause; break;
        case GLFW_KEY_F1: result = BoxKeyF1; break;
        case GLFW_KEY_F2: result = BoxKeyF2; break;
        case GLFW_KEY_F3: result = BoxKeyF3; break;
        case GLFW_KEY_F4: result = BoxKeyF4; break;
        case GLFW_KEY_F5: result = BoxKeyF5; break;
        case GLFW_KEY_F6: result = BoxKeyF6; break;
        case GLFW_KEY_F7: result = BoxKeyF7; break;
        case GLFW_KEY_F8: result = BoxKeyF8; break;
        case GLFW_KEY_F9: result = BoxKeyF9; break;
        case GLFW_KEY_F10: result = BoxKeyF10; break;
        case GLFW_KEY_F11: result = BoxKeyF11; break;
        case GLFW_KEY_F12: result = BoxKeyF12; break;
        case GLFW_KEY_KP_0: result = BoxKeyKp0; break;
        case GLFW_KEY_KP_1: result = BoxKeyKp1; break;
        case GLFW_KEY_KP_2: result = BoxKeyKp2; break;
        case GLFW_KEY_KP_3: result = BoxKeyKp3; break;
        case GLFW_KEY_KP_4: result = BoxKeyKp4; break;
        case GLFW_KEY_KP_5: result = BoxKeyKp5; break;
        case GLFW_KEY_KP_6: result = BoxKeyKp6; break;
        case GLFW_KEY_KP_7: result = BoxKeyKp7; break;
        case GLFW_KEY_KP_8: result = BoxKeyKp8; break;
        case GLFW_KEY_KP_9: result = BoxKeyKp9; break;
        case GLFW_KEY_KP_DECIMAL: result = BoxKeyKpDecimal; break;
        case GLFW_KEY_KP_DIVIDE: result = BoxKeyKpDivide; break;
        case GLFW_KEY_KP_MULTIPLY: result = BoxKeyKpMultiply; break;
        case GLFW_KEY_KP_SUBTRACT: result = BoxKeyKpSubtract; break;
        case GLFW_KEY_KP_ADD: result = BoxKeyKpAdd; break;
        case GLFW_KEY_KP_ENTER: result = BoxKeyKpEnter; break;
        case GLFW_KEY_KP_EQUAL: result = BoxKeyKpEqual; break;
        case GLFW_KEY_LEFT_SHIFT: result = BoxKeyShiftLeft; break;
        case GLFW_KEY_LEFT_CONTROL: result = BoxKeyCtrlLeft; break;
        case GLFW_KEY_LEFT_ALT: result = BoxKeyAltLeft; break;
        case GLFW_KEY_LEFT_SUPER: result = BoxKeySuperLeft; break;
        case GLFW_KEY_RIGHT_SHIFT: result = BoxKeyShiftRight; break;
        case GLFW_KEY_RIGHT_CONTROL: result = BoxKeyCtrlRight; break;
        case GLFW_KEY_RIGHT_ALT: result = BoxKeyAltRight; break;
        case GLFW_KEY_RIGHT_SUPER: result = BoxKeySuperRight; break;
        case GLFW_KEY_MENU: result = BoxKeyMenu; break;
        default: break;
        }
    }

    return result;
}

box_window_t* box_window_create(
    const box_window_desc_t *desc)
{
    box_window_t *result = NULL;
    if (!box_window_count && !glfwInit()) {
        fprintf(stderr, "box: failed to initialize GLFW\n");
        goto done;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(desc->width, desc->height, desc->title, NULL, NULL);
    if (!window) {
        fprintf(stderr, "box: failed to create window\n");
        goto done;
    }

    result = calloc(1, sizeof(box_window_t));
    result->window = window;
    glfwSetWindowUserPointer(window, result);
    box_glfw_callbacks(window);
#ifdef __APPLE__
    result->metal_layer = box_macos_metal_layer(glfwGetCocoaWindow(window));
#endif
    box_window_count ++;
done:
    return result;
}

void box_window_destroy(
    box_window_t *w)
{
    glfwDestroyWindow(w->window);
    free(w);
    if (!-- box_window_count) {
        glfwTerminate();
    }
}

void box_window_poll(
    box_window_t *w)
{
    (void)w;
    glfwPollEvents();
}

bool box_window_should_close(
    const box_window_t *w)
{
    return glfwWindowShouldClose(w->window);
}

void box_window_size(
    const box_window_t *w,
    int32_t *px_width,
    int32_t *px_height,
    int32_t *pt_width,
    int32_t *pt_height)
{
    glfwGetFramebufferSize(w->window, px_width, px_height);
    glfwGetWindowSize(w->window, pt_width, pt_height);
}

void box_window_set_title(
    box_window_t *w,
    const char *title)
{
    glfwSetWindowTitle(w->window, title);
}

void box_window_set_size(
    box_window_t *w,
    int32_t width,
    int32_t height)
{
    glfwSetWindowSize(w->window, width, height);
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
    glfwSetInputMode(w->window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(w->window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
}

void* box_window_metal_layer(
    box_window_t *w)
{
    return w->metal_layer;
}

const char* box_window_canvas(
    const box_window_t *w)
{
    (void)w;

    return NULL;
}

#endif
