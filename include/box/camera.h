/**
 * @file box/camera.h
 * @brief Camera component and billboard tag.
 *
 * The box.camera module registers cameras; views pick which camera renders.
 */

#ifndef BOX_CAMERA_H
#define BOX_CAMERA_H

#include "types.h"

#ifdef BOX_CAMERA_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Perspective or orthographic camera, zero fields take engine defaults */
ECS_STRUCT(Camera, {
    float fov;           /**< Vertical field of view in degrees, default 60 */
    float near_;         /**< Near plane distance, default 0.1 */
    float far_;          /**< Far plane distance for orthographic cameras, default 1000 */
    float aspect_ratio;  /**< Width over height, 0 follows the surface */
    bool ortho;          /**< Use an orthographic projection */
    float ortho_height;  /**< Visible height in world units when orthographic, default 10 */
    float ortho_scale;   /**< Surface points per world unit, visible height follows the surface, 0 uses ortho_height */
});

/** Register the box.camera module.
 *
 * @param world The world
 */
void BoxCameraImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
