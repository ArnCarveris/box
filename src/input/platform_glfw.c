/**
 * @file input/platform_glfw.c
 * @brief The glfw backend: window callbacks push device events to the world owning the window.
 *
 * glfw runs the callbacks inside glfwPollEvents on the main thread, which the backend calls at
 * the start of InputUpdate, so the events of a frame apply in that frame.
 */

#include "input.h"
#include "platform.h"
#include "../surface/surface.h"

#ifdef BOX_PLATFORM_GLFW

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/** Get the world a window sends its events to.
 *
 * @param window The glfw window
 * @return The world
 */
static ecs_world_t* box_glfw_world(
    GLFWwindow *window)
{
    return glfwGetWindowUserPointer(window);
}

/** Push a key event.
 *
 * @param window The glfw window
 * @param key The glfw key code
 * @param scancode The platform scancode
 * @param action Release, press or repeat
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
    box_input_glfw_on_key(box_glfw_world(window), key, action);
}

/** Push a mouse button event.
 *
 * @param window The glfw window
 * @param button The glfw button
 * @param action Release or press
 * @param mods Modifier bits
 */
static void box_glfw_button(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    (void)mods;
    box_input_glfw_on_button(box_glfw_world(window), button, action);
}

/** Push a typed character.
 *
 * @param window The glfw window
 * @param codepoint The unicode code point
 */
static void box_glfw_char(
    GLFWwindow *window,
    unsigned int codepoint)
{
    box_input_platform_push(box_glfw_world(window), &(InputEvent){
        .kind = InputText, .code = (int32_t)codepoint });
}

/** Push a cursor move in window points.
 *
 * @param window The glfw window
 * @param x Cursor x
 * @param y Cursor y
 */
static void box_glfw_cursor(
    GLFWwindow *window,
    double x,
    double y)
{
    box_input_platform_push(box_glfw_world(window), &(InputEvent){
        .kind = InputMove, .value = { (float)x, (float)y } });
}

/** Push a wheel movement.
 *
 * @param window The glfw window
 * @param x Horizontal offset
 * @param y Vertical offset, positive when the wheel turns away from the user
 */
static void box_glfw_scroll(
    GLFWwindow *window,
    double x,
    double y)
{
    box_input_platform_push(box_glfw_world(window), &(InputEvent){
        .kind = InputScroll, .value = { (float)x, (float)y } });
}

/** Push a focus change.
 *
 * @param window The glfw window
 * @param focused Nonzero when the window has keyboard focus
 */
static void box_glfw_focus(
    GLFWwindow *window,
    int focused)
{
    box_input_platform_push(box_glfw_world(window), &(InputEvent){
        .kind = InputFocus, .code = focused ? 1 : 0 });
}

/** Push a window size in points.
 *
 * @param window The glfw window
 * @param width Width in points
 * @param height Height in points
 */
static void box_glfw_size(
    GLFWwindow *window,
    int width,
    int height)
{
    box_input_platform_push(box_glfw_world(window), &(InputEvent){
        .kind = InputResize, .value = { (float)width, (float)height } });
}

/** Run the glfw callbacks of every window, the poll of the backend.
 *
 * @param world The world
 * @param ctx Unused
 */
static void box_glfw_poll(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    box_window_poll();
}

void box_input_glfw_attach(
    ecs_world_t *world,
    void *window)
{
    GLFWwindow *w = window;
    int width = 0, height = 0;
    if (!ecs_lookup(world, "box.input")) {
        goto done;
    }

    glfwSetWindowUserPointer(w, world);
    glfwSetKeyCallback(w, box_glfw_key);
    glfwSetMouseButtonCallback(w, box_glfw_button);
    glfwSetCharCallback(w, box_glfw_char);
    glfwSetCursorPosCallback(w, box_glfw_cursor);
    glfwSetScrollCallback(w, box_glfw_scroll);
    glfwSetWindowFocusCallback(w, box_glfw_focus);
    glfwSetWindowSizeCallback(w, box_glfw_size);
    glfwGetWindowSize(w, &width, &height);
    box_glfw_size(w, width, height);
    if (!box_input_device(world)->backend.poll) {
        box_input_platform_set(world, &(box_input_platform_t){ .poll = box_glfw_poll });
    }

done:
    return;
}

#else

void box_input_glfw_attach(
    ecs_world_t *world,
    void *window)
{
    (void)world;
    (void)window;
}

#endif
