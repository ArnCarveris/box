/**
 * @file platform/platform_web.c
 * @brief Canvas backed window implementation for Emscripten.
 */

#ifdef BOX_PLATFORM_WEB

#include "platform.h"
#include <emscripten/html5.h>
#include <emscripten/emscripten.h>
#include <stdlib.h>

/** Window state on web */
struct box_window_t {
    const char *canvas;    /**< Canvas selector */
    int32_t px_width;      /**< Backing store width */
    int32_t px_height;     /**< Backing store height */
    double pt_width;       /**< CSS width */
    double pt_height;      /**< CSS height */
};

/** Size the canvas backing store to its CSS size times the device pixel ratio.
 *
 * @param w The window
 */
static void box_window_sync_size(
    box_window_t *w)
{
    emscripten_get_element_css_size(w->canvas, &w->pt_width, &w->pt_height);
    double ratio = emscripten_get_device_pixel_ratio();
    int32_t px_width = (int32_t)(w->pt_width * ratio);
    int32_t px_height = (int32_t)(w->pt_height * ratio);
    if (px_width != w->px_width || px_height != w->px_height) {
        w->px_width = px_width;
        w->px_height = px_height;
        emscripten_set_canvas_element_size(w->canvas, px_width, px_height);
    }
}

box_window_t* box_window_create(
    const box_window_desc_t *desc)
{
    (void)desc;
    box_window_t *w = calloc(1, sizeof(box_window_t));
    w->canvas = "#canvas";
    box_window_sync_size(w);
    return w;
}

void box_window_destroy(
    box_window_t *w)
{
    free(w);
}

void box_window_poll(
    box_window_t *w)
{
    box_window_sync_size(w);
}

bool box_window_should_close(
    const box_window_t *w)
{
    (void)w;
    return false;
}

void box_window_size(
    const box_window_t *w,
    int32_t *px_width,
    int32_t *px_height,
    int32_t *pt_width,
    int32_t *pt_height)
{
    *px_width = w->px_width;
    *px_height = w->px_height;
    *pt_width = (int32_t)w->pt_width;
    *pt_height = (int32_t)w->pt_height;
}

void box_window_set_title(
    box_window_t *w,
    const char *title)
{
    (void)w;
    emscripten_set_window_title(title);
}

void box_window_set_size(
    box_window_t *w,
    int32_t width,
    int32_t height)
{
    (void)w; (void)width; (void)height;
}

void* box_window_metal_layer(
    box_window_t *w)
{
    (void)w;
    return NULL;
}

const char* box_window_canvas(
    const box_window_t *w)
{
    return w->canvas;
}

#endif
