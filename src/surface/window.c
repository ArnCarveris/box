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

/** Seconds after which the refresh rate of a window is read again without a move or a change
 * of the monitors, so a changed display mode is noticed */
#define BOX_WINDOW_REFRESH_RECHECK (2.0)

/** A glfw window and its layer */
struct box_window_t {
    GLFWwindow *window;     /**< The glfw window */
    void *layer;            /**< CAMetalLayer of the content view */
    float refresh;          /**< Refresh rate of the display of the window when last read */
    int32_t refresh_moves;  /**< box_window_moves when the refresh rate was read, -1 never */
    double refresh_time;    /**< glfw time when the refresh rate was read */
};

/** Windows open in the process, glfw terminates when the last one closes */
static int32_t box_window_count = 0;

/** Moves and resizes of windows and changes of the monitors, which may change the display a
 * window is on */
static int32_t box_window_moves = 0;

/** Count a move of a window.
 *
 * @param window The glfw window
 * @param x Left edge in screen coordinates
 * @param y Top edge in screen coordinates
 */
static void box_window_moved(
    GLFWwindow *window,
    int x,
    int y)
{
    (void)window;
    (void)x;
    (void)y;
    box_window_moves ++;
}

/** Count a monitor that was connected or disconnected.
 *
 * @param monitor The monitor
 * @param event GLFW_CONNECTED or GLFW_DISCONNECTED
 */
static void box_window_monitor(
    GLFWmonitor *monitor,
    int event)
{
    (void)monitor;
    (void)event;
    box_window_moves ++;
}

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

    glfwSetMonitorCallback(box_window_monitor);

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
    result->refresh_moves = -1;
    glfwSetWindowPosCallback(window, box_window_moved);
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
    box_window_moves ++;
}

void* box_window_layer(
    box_window_t *window)
{
    return window->layer;
}

/** Read the refresh rate of the monitor under the center of a window from glfw.
 *
 * @param window The window
 * @return Refresh rate in Hz, 0 when unknown
 */
static float box_window_read_refresh(
    box_window_t *window)
{
    int count = 0, x = 0, y = 0, w = 0, h = 0, i;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode;
    glfwGetWindowPos(window->window, &x, &y);
    glfwGetWindowSize(window->window, &w, &h);
    for (i = 0; i < count; i ++) {
        int mx = 0, my = 0;
        const GLFWvidmode *m = glfwGetVideoMode(monitors[i]);
        glfwGetMonitorPos(monitors[i], &mx, &my);
        if (m && x + w / 2 >= mx && x + w / 2 < mx + m->width &&
            y + h / 2 >= my && y + h / 2 < my + m->height)
        {
            monitor = monitors[i];
            break;
        }
    }

    mode = monitor ? glfwGetVideoMode(monitor) : NULL;

    return mode ? (float)mode->refreshRate : 0;
}

float box_window_refresh(
    box_window_t *window)
{
    double now = glfwGetTime();
    if (window->refresh_moves != box_window_moves ||
        now - window->refresh_time >= BOX_WINDOW_REFRESH_RECHECK)
    {
        window->refresh = box_window_read_refresh(window);
        window->refresh_moves = box_window_moves;
        window->refresh_time = now;
    }

    return window->refresh;
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

float box_window_refresh(
    box_window_t *window)
{
    (void)window;

    return 0;
}

bool box_window_closed(
    box_window_t *window)
{
    (void)window;

    return false;
}

#endif
