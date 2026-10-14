/**
 * @file surface/window.c
 * @brief glfw windows with a CAMetalLayer that the webgpu backend presents to.
 *
 * glfw requires the main thread, so windows are opened, polled and closed by systems and
 * hooks; only the layer is handed to the render thread. A window sends its input events to the
 * world that opened it; in the browser the canvas does, while the first target presents to it.
 */

#include "surface.h"
#include "../input/platform.h"

#ifdef BOX_PLATFORM_GLFW

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <objc/message.h>
#include <objc/runtime.h>

/** A glfw window and its layer */
struct box_window_t {
    GLFWwindow *window;     /**< The glfw window */
    void *layer;            /**< CAMetalLayer of the content view */
};

/** Windows open in the process, glfw terminates when the last one closes */
static int32_t box_window_count = 0;

/** Report a glfw error.
 *
 * @param code Error code
 * @param message Error text
 */
static void box_window_error(
    int code,
    const char *message)
{
    ecs_err("glfw error %d: %s", code, message);
}

/** Give the content view of a window a CAMetalLayer.
 *
 * @param window The glfw window
 * @return The layer
 */
static void* box_window_metal_layer(
    GLFWwindow *window)
{
    id view = glfwGetCocoaView(window);
    id ns_window = glfwGetCocoaWindow(window);
    id layer = ((id (*)(Class, SEL))objc_msgSend)(
        objc_getClass("CAMetalLayer"), sel_registerName("layer"));
    double scale = ((double (*)(id, SEL))objc_msgSend)(
        ns_window, sel_registerName("backingScaleFactor"));
    ((void (*)(id, SEL, double))objc_msgSend)(
        layer, sel_registerName("setContentsScale:"), scale);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(view, sel_registerName("setWantsLayer:"), YES);
    ((void (*)(id, SEL, id))objc_msgSend)(view, sel_registerName("setLayer:"), layer);

    return (void*)layer;
}

box_window_t* box_window_new(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    const char *title)
{
    box_window_t *result = NULL;
    GLFWwindow *window;
    glfwSetErrorCallback(box_window_error);
    if (!box_window_count && !glfwInit()) {
        ecs_err("cannot initialize glfw");
        goto done;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        if (!box_window_count) {
            glfwTerminate();
        }

        goto done;
    }

    result = ecs_os_calloc_t(box_window_t);
    result->window = window;
    result->layer = box_window_metal_layer(window);
    box_window_count ++;
    box_input_glfw_attach(ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world)), window);
done:
    return result;
}

void box_window_free(
    box_window_t *window)
{
    glfwDestroyWindow(window->window);
    ecs_os_free(window);
    box_window_count --;
    if (!box_window_count) {
        glfwTerminate();
    }
}

void box_window_poll(void)
{
    if (box_window_count) {
        glfwPollEvents();
    }
}

void box_window_size(
    box_window_t *window,
    int32_t *width,
    int32_t *height)
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window->window, &w, &h);
    *width = w;
    *height = h;
}

void box_window_points(
    box_window_t *window,
    int32_t *width,
    int32_t *height)
{
    int w = 0, h = 0;
    glfwGetWindowSize(window->window, &w, &h);
    *width = w;
    *height = h;
}

void box_window_resize(
    box_window_t *window,
    int32_t width,
    int32_t height)
{
    glfwSetWindowSize(window->window, width, height);
}

void* box_window_layer(
    box_window_t *window)
{
    return window->layer;
}

bool box_window_closed(
    box_window_t *window)
{
    return glfwWindowShouldClose(window->window) != 0;
}

#else

box_window_t* box_window_new(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    const char *title)
{
    (void)width;
    (void)height;
    (void)title;
    box_input_web_attach(ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world)));

    return NULL;
}

void box_window_free(
    box_window_t *window)
{
    (void)window;
}

void box_window_poll(void)
{
}

void box_window_size(
    box_window_t *window,
    int32_t *width,
    int32_t *height)
{
    (void)window;
    *width = 0;
    *height = 0;
}

void box_window_points(
    box_window_t *window,
    int32_t *width,
    int32_t *height)
{
    (void)window;
    *width = 0;
    *height = 0;
}

void box_window_resize(
    box_window_t *window,
    int32_t width,
    int32_t height)
{
    (void)window;
    (void)width;
    (void)height;
}

void* box_window_layer(
    box_window_t *window)
{
    (void)window;

    return NULL;
}

bool box_window_closed(
    box_window_t *window)
{
    (void)window;

    return false;
}

#endif
