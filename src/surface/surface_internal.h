/**
 * @file surface/surface_internal.h
 * @brief Runtime objects behind a Surface, shared with the render module.
 */

#ifndef BOX_SURFACE_INTERNAL_H
#define BOX_SURFACE_INTERNAL_H

#include <box.h>
#include "../platform/platform.h"
#include "../renderer/renderer.h"

/** Runtime objects behind a Surface */
typedef struct SurfaceImpl {
    box_window_t *window;       /**< Window, NULL when headless */
    box_renderer_t *renderer;   /**< Renderer producing frames */
    box_packet_t *packet;       /**< Packet of the frame being built, between acquire and submit */
    int32_t width;              /**< Last applied requested width */
    int32_t height;             /**< Last applied requested height */
    char *title;                /**< Last applied title */
} SurfaceImpl;

extern ECS_COMPONENT_DECLARE(SurfaceImpl);

#endif
