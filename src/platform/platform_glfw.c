/**
 * @file platform/platform_glfw.c
 * @brief GLFW window implementation for desktop platforms.
 */

#ifdef BOX_PLATFORM_GLFW

#include "platform.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <stdio.h>
#include <stdlib.h>

/** Window state on desktop */
struct box_window_t {
    GLFWwindow *window;    /**< GLFW window */
    void *metal_layer;     /**< CAMetalLayer on macOS */
};

/** Number of windows alive, GLFW is terminated when it reaches zero */
static int32_t box_window_count = 0;

#ifdef __APPLE__
void* box_macos_metal_layer(
    void *ns_window);
#endif

box_window_t* box_window_create(
    const box_window_desc_t *desc)
{
    box_window_t *result = NULL;
    if (!box_window_count && !glfwInit()) {
        fprintf(stderr, "box: failed to initialize GLFW\n");
        goto done;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(desc->width, desc->height, desc->title, NULL, NULL);
    if (!window) {
        fprintf(stderr, "box: failed to create window\n");
        goto done;
    }

    result = calloc(1, sizeof(box_window_t));
    result->window = window;
#ifdef __APPLE__
    result->metal_layer = box_macos_metal_layer(glfwGetCocoaWindow(window));
#endif
    box_window_count ++;
done:
    return result;
}

void box_window_destroy(
    box_window_t *w)
{
    glfwDestroyWindow(w->window);
    free(w);
    if (!-- box_window_count) {
        glfwTerminate();
    }
}

void box_window_poll(
    box_window_t *w)
{
    (void)w;
    glfwPollEvents();
}

bool box_window_should_close(
    const box_window_t *w)
{
    return glfwWindowShouldClose(w->window);
}

void box_window_size(
    const box_window_t *w,
    int32_t *px_width,
    int32_t *px_height,
    int32_t *pt_width,
    int32_t *pt_height)
{
    glfwGetFramebufferSize(w->window, px_width, px_height);
    glfwGetWindowSize(w->window, pt_width, pt_height);
}

void box_window_set_title(
    box_window_t *w,
    const char *title)
{
    glfwSetWindowTitle(w->window, title);
}

void box_window_set_size(
    box_window_t *w,
    int32_t width,
    int32_t height)
{
    glfwSetWindowSize(w->window, width, height);
}

void* box_window_metal_layer(
    box_window_t *w)
{
    return w->metal_layer;
}

const char* box_window_canvas(
    const box_window_t *w)
{
    (void)w;

    return NULL;
}

#endif
