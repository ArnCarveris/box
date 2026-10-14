/**
 * @file input/platform.h
 * @brief The glfw and browser backends that the windows of box.surface attach to a world.
 *
 * Only includes flecs, so the window sources can include it next to their platform headers.
 */

#ifndef BOX_INPUT_PLATFORM_H
#define BOX_INPUT_PLATFORM_H

#include <flecs.h>

/** Send the events of a glfw window to the input of a world and install the glfw backend,
 * which polls glfw at the start of InputUpdate, unless the world already has a backend.
 *
 * Does nothing when the world has not imported box.input.
 *
 * @param world The world that owns the window
 * @param window The GLFWwindow
 */
void box_input_glfw_attach(
    ecs_world_t *world,
    void *window);

/** Send the events of the page canvas to the input of a world by installing the browser
 * backend, unless the world already has a backend.
 *
 * Does nothing when the world has not imported box.input or the page has no canvas.
 *
 * @param world The world that presents to the canvas
 */
void box_input_web_attach(
    ecs_world_t *world);

#endif
