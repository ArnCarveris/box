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
    bool orthographic;   /**< Use an orthographic projection */
    float ortho_height;  /**< Visible height in world units when orthographic, default 10 */
});

/** Renders the camera to a PNG once, the engine removes the component */
ECS_STRUCT(Capture, {
    char *path;  /**< File the frame is written to */
});

/** Rotates the entity every frame so its local +Z faces the active camera */
extern ECS_TAG_DECLARE(Billboard);

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
