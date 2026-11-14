/**
 * @file input/platform.h
 * @brief The glfw and browser backends that the windows of box.surface attach to a world.
 *
 * Only includes flecs, so the window sources can include it next to their platform headers.
 */

#ifndef BOX_INPUT_PLATFORM_H
#define BOX_INPUT_PLATFORM_H

#include <flecs.h>

/** Where the events of a glfw window go, kept by the window for as long as it lives */
typedef struct box_input_glfw_target_t {
    ecs_world_t *world;   /**< World that owns the window */
    ecs_entity_t surface; /**< Surface the window presents, stamped on its events */
} box_input_glfw_target_t;

/** Send the events of a glfw window, stamped with its surface, to the input of a world, and
 * install the glfw backend, which captures the cursor, unless the world already has a backend.
 *
 * box.surface polls glfw once per frame in box.surface.Poll, in the PreFrame phase before the
 * OnLoad box.input.Update, so the events of a frame apply in that frame. Does nothing without
 * box.input.
 *
 * @param target World and surface of the window, must outlive the callbacks
 * @param window The GLFWwindow
 */
void box_input_glfw_attach(
    box_input_glfw_target_t *target,
    void *window);

/** Stop sending the events of a glfw window and capturing its cursor, before it is destroyed.
 *
 * @param window The GLFWwindow
 */
void box_input_glfw_detach(
    void *window);

/** Send the events of the page canvas, stamped with a surface, to the input of a world by
 * installing the browser backend, unless the world already has a backend.
 *
 * Like the renderer, the backend gives the canvas to the next surface once its surface is gone:
 * the surface attached next, else the first surface without a texture.
 *
 * Does nothing when the world has not imported box.input or the page has no canvas.
 *
 * @param world The world that presents to the canvas
 * @param surface The surface that presents to the canvas
 */
void box_input_web_attach(
    ecs_world_t *world,
    ecs_entity_t surface);

#endif
