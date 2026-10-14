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

/** Send the events of a glfw window, stamped with its surface, to the input of a world.
 *
 * box.surface polls glfw once per frame in SurfacePoll, which runs before InputUpdate, so the
 * events of a frame apply in that frame. Does nothing when the world has not imported box.input.
 *
 * @param target World and surface of the window, must outlive the callbacks
 * @param window The GLFWwindow
 */
void box_input_glfw_attach(
    box_input_glfw_target_t *target,
    void *window);

/** Send the events of the page canvas, stamped with a surface, to the input of a world by
 * installing the browser backend, unless the world already has a backend.
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
