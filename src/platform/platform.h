/**
 * @file platform/platform.h
 * @brief Window creation, event polling and surface sources per platform.
 *
 * Implemented by platform_glfw.c on desktop and platform_web.c on wasm.
 */

#ifndef BOX_PLATFORM_H
#define BOX_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque window handle */
typedef struct box_window_t box_window_t;

/** Parameters for creating a window */
typedef struct box_window_desc_t {
    const char *title;   /**< Window title */
    int32_t width;       /**< Width in points */
    int32_t height;      /**< Height in points */
} box_window_desc_t;

/** Create a window; on web this binds to the page canvas.
 *
 * @param desc Creation parameters
 * @return The window, or NULL on failure
 */
box_window_t* box_window_create(
    const box_window_desc_t *desc);

/** Destroy a window.
 *
 * @param w The window
 */
void box_window_destroy(
    box_window_t *w);

/** Process pending OS events.
 *
 * @param w The window
 */
void box_window_poll(
    box_window_t *w);

/** Check whether the user asked to close the window.
 *
 * @param w The window
 * @return true when the window should close
 */
bool box_window_should_close(
    const box_window_t *w);

/** Get the window size in pixels and in points.
 *
 * @param w The window
 * @param px_width Framebuffer width in pixels
 * @param px_height Framebuffer height in pixels
 * @param pt_width Width in points
 * @param pt_height Height in points
 */
void box_window_size(
    const box_window_t *w,
    int32_t *px_width,
    int32_t *px_height,
    int32_t *pt_width,
    int32_t *pt_height);

/** Change the window title.
 *
 * @param w The window
 * @param title The title
 */
void box_window_set_title(
    box_window_t *w,
    const char *title);

/** Change the window size in points.
 *
 * @param w The window
 * @param width New width
 * @param height New height
 */
void box_window_set_size(
    box_window_t *w,
    int32_t width,
    int32_t height);

/** What an OS input event does, mirrors InputEventKind of box/input.h */
typedef enum box_input_kind_t {
    BoxInputKeyDown = 0,     /**< Key went down, code is an engine key code */
    BoxInputKeyUp = 1,       /**< Key came up, code is an engine key code */
    BoxInputChar = 2,        /**< Character typed, code is a unicode code point */
    BoxInputMouseMove = 3,   /**< Cursor moved to x, y in window points */
    BoxInputButtonDown = 4,  /**< Mouse button went down, code is a button code */
    BoxInputButtonUp = 5,    /**< Mouse button came up, code is a button code */
    BoxInputScroll = 6,      /**< Wheel moved by x, y */
    BoxInputFocus = 7,       /**< Window focus changed, code is 1 when focused */
    BoxInputMinimize = 8     /**< Window minimize changed, code is 1 when minimized */
} box_input_kind_t;

/** One input event as the OS reported it */
typedef struct box_input_event_t {
    box_input_kind_t kind; /**< What the event does */
    int32_t code;          /**< Key code, button code, code point or boolean flag */
    float x;               /**< Cursor x in window points, or scroll x */
    float y;               /**< Cursor y in window points, or scroll y */
} box_input_event_t;

/** Sink the window reports its input events to */
typedef void (*box_input_fn)(
    void *ctx,
    const box_input_event_t *event);

/** Report the input events of a window to a sink until it is set again.
 *
 * @param w The window
 * @param fn The sink, NULL to stop reporting
 * @param ctx Context passed to the sink
 */
void box_window_set_input(
    box_window_t *w,
    box_input_fn fn,
    void *ctx);

/** Lock the cursor to the window and hide it, or release it again.
 *
 * @param w The window
 * @param captured true to lock the cursor
 */
void box_window_set_cursor_captured(
    box_window_t *w,
    bool captured);

/** Translate a native key code to an engine key code.
 *
 * @param native The GLFW key code natively, the DOM key code on web
 * @return The engine key code, BoxKeyNone when the key has none
 */
int32_t box_platform_key_from_native(
    int32_t native);

/** Check whether the window is hidden from the user.
 *
 * A minimized window, one another window covers completely or one on a
 * different desktop is occluded; the surface polls this to pace the frame
 * loop down while nothing it draws can be seen.
 *
 * @param w The window
 * @return true when the window is occluded
 */
bool box_window_occluded(
    const box_window_t *w);

/** Get the CAMetalLayer backing the window on macOS, or its HWND on Windows.
 *
 * @param w The window
 * @return The layer or HWND, or NULL on other platforms
 */
void* box_window_metal_layer(
    box_window_t *w);

/** Get the canvas selector of the window on web.
 *
 * @param w The window
 * @return The selector, or NULL on other platforms
 */
const char* box_window_canvas(
    const box_window_t *w);

#ifdef __cplusplus
}
#endif

#endif
