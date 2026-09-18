/**
 * @file box/surface.h
 * @brief Surface component: setting it brings up a window or an offscreen target.
 *
 * The box.surface module connects the Surface component to the platform and renderer.
 */

#ifndef BOX_SURFACE_H
#define BOX_SURFACE_H

#include <flecs.h>

#ifdef BOX_SURFACE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Window or offscreen render target, actual_* and point_* are written by the engine */
ECS_STRUCT(Surface, {
    int32_t width;          /**< Requested width in points */
    int32_t height;         /**< Requested height in points */
    int32_t actual_width;   /**< Framebuffer width in pixels */
    int32_t actual_height;  /**< Framebuffer height in pixels */
    int32_t point_width;    /**< Window width in points */
    int32_t point_height;   /**< Window height in points */
    bool vsync;             /**< Present with vertical sync */
    char *title;            /**< Window title */
    char *write_to_file;    /**< PNG path to write frames to, NULL for none */
    int32_t write_nth;      /**< Write every nth frame with a frame suffix, 0 writes every frame */
});

/** Register the box.surface module.
 *
 * @param world The world
 */
void BoxSurfaceImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
