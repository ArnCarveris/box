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

/** Get the CAMetalLayer backing the window on macOS.
 *
 * @param w The window
 * @return The layer, or NULL on other platforms
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
