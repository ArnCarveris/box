/**
 * @file input/platform_glfw.c
 * @brief The glfw backend: window callbacks push device events, stamped with the surface of the
 * window, to the world owning the window.
 *
 * glfw runs the callbacks inside glfwPollEvents on the main thread, which box.surface calls once
 * per frame in SurfacePoll (PreFrame), before InputUpdate (OnLoad) applies them.
 */

#include "input.h"
#include "platform.h"

#ifdef BOX_PLATFORM_GLFW

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/** Start pushing the events of a window: its surface is stamped on them until the end.
 *
 * @param window The glfw window
 * @return The world the window sends its events to
 */
static ecs_world_t* box_glfw_begin(
    GLFWwindow *window)
{
    box_input_glfw_target_t *target = glfwGetWindowUserPointer(window);
    box_input_platform_source(target->world, target->surface);

    return target->world;
}

/** Stop stamping the surface of a window on pushed events.
 *
 * @param world The world of the window
 */
static void box_glfw_end(
    ecs_world_t *world)
{
    box_input_platform_source(world, 0);
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
    ecs_world_t *world = box_glfw_begin(window);
    (void)scancode;
    (void)mods;
    box_input_glfw_on_key(world, key, action);
    box_glfw_end(world);
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
    ecs_world_t *world = box_glfw_begin(window);
    (void)mods;
    box_input_glfw_on_button(world, button, action);
    box_glfw_end(world);
}

/** Push an event of a window.
 *
 * @param window The glfw window
 * @param event The event
 */
static void box_glfw_push(
    GLFWwindow *window,
    const InputEvent *event)
{
    ecs_world_t *world = box_glfw_begin(window);
    box_input_platform_push(world, event);
    box_glfw_end(world);
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
    box_glfw_push(window, &(InputEvent){ .kind = InputText, .code = (int32_t)codepoint });
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
    box_glfw_push(window, &(InputEvent){ .kind = InputMove, .value = { (float)x, (float)y } });
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
    box_glfw_push(window, &(InputEvent){ .kind = InputScroll, .value = { (float)x, (float)y } });
}

/** Push a focus change. A gain first reports where the cursor is, because macOS sends no
 * cursor moves to a window without focus and the click that focuses it has no move of its own.
 *
 * @param window The glfw window
 * @param focused Nonzero when the window has keyboard focus
 */
static void box_glfw_focus(
    GLFWwindow *window,
    int focused)
{
    double x = 0, y = 0;
    if (focused) {
        glfwGetCursorPos(window, &x, &y);
        box_glfw_cursor(window, x, y);
    }

    box_glfw_push(window, &(InputEvent){ .kind = InputFocus, .code = focused ? 1 : 0 });
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
    box_glfw_push(window, &(InputEvent){
        .kind = InputResize, .value = { (float)width, (float)height } });
}

void box_input_glfw_attach(
    box_input_glfw_target_t *target,
    void *window)
{
    GLFWwindow *w = window;
    int width = 0, height = 0;
    if (!ecs_lookup(target->world, "box.input")) {
        goto done;
    }

    glfwSetWindowUserPointer(w, target);
    glfwSetKeyCallback(w, box_glfw_key);
    glfwSetMouseButtonCallback(w, box_glfw_button);
    glfwSetCharCallback(w, box_glfw_char);
    glfwSetCursorPosCallback(w, box_glfw_cursor);
    glfwSetScrollCallback(w, box_glfw_scroll);
    glfwSetWindowFocusCallback(w, box_glfw_focus);
    glfwSetWindowSizeCallback(w, box_glfw_size);
    glfwGetWindowSize(w, &width, &height);
    box_glfw_size(w, width, height);
    if (glfwGetWindowAttrib(w, GLFW_FOCUSED)) {
        box_glfw_focus(w, 1);
    }

done:
    return;
}

#else

void box_input_glfw_attach(
    box_input_glfw_target_t *target,
    void *window)
{
    (void)target;
    (void)window;
}

#endif
