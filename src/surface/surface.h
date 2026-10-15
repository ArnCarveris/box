/**
 * @file surface/surface.h
 * @brief Windows of the box.surface sources, created and polled on the main thread.
 *
 * Only includes flecs, so the window sources can include platform headers next to it.
 */

#ifndef BOX_SURFACE_INTERNAL_H
#define BOX_SURFACE_INTERNAL_H

#include <flecs.h>

/** A platform window that a surface presents to */
typedef struct box_window_t box_window_t;

/** Open a window, available when built with BOX_PLATFORM_GLFW, whose input events go to a world.
 *
 * In the browser no window opens, but the page canvas sends its input events to the world.
 *
 * @param world The world or a stage of it
 * @param surface The surface the window presents, stamped on its input events
 * @param width Width in points
 * @param height Height in points
 * @param title Title of the window
 * @return The window, NULL when windows are not available or it could not be opened
 */
box_window_t* box_window_new(
    ecs_world_t *world,
    ecs_entity_t surface,
    int32_t width,
    int32_t height,
    const char *title);

/** Close a window.
 *
 * @param window The window
 */
void box_window_free(
    box_window_t *window);

/** Process the events of every window, once per frame in SurfacePoll. */
void box_window_poll(void);

/** Read the size of the framebuffer of a window.
 *
 * @param window The window
 * @param width Receives the width in pixels
 * @param height Receives the height in pixels
 */
void box_window_size(
    box_window_t *window,
    int32_t *width,
    int32_t *height);

/** Read the size of a window in points.
 *
 * @param window The window
 * @param width Receives the width in points
 * @param height Receives the height in points
 */
void box_window_points(
    box_window_t *window,
    int32_t *width,
    int32_t *height);

/** Change the size of a window.
 *
 * @param window The window
 * @param width Width in points
 * @param height Height in points
 */
void box_window_resize(
    box_window_t *window,
    int32_t width,
    int32_t height);

/** Get the layer webgpu presents a window with.
 *
 * @param window The window
 * @return The CAMetalLayer of the window
 */
void* box_window_layer(
    box_window_t *window);

/** Get the refresh rate of the display a window is on: the monitor under its center. glfw is
 * asked again only after a window moved or resized, a monitor changed or two seconds passed.
 *
 * @param window The window
 * @return Refresh rate in Hz, 0 when unknown
 */
float box_window_refresh(
    box_window_t *window);

/** Check whether the user asked to close a window.
 *
 * @param window The window
 * @return Whether the window should close
 */
bool box_window_closed(
    box_window_t *window);

#endif
