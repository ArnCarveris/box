/**
 * @file box/camera.h
 * @brief Camera component and its reversed Z projection.
 *
 * The box.camera module registers cameras; views pick which camera renders. A camera looks
 * down its local -Z axis; its projections map the near plane to depth 1 and the far plane to 0.
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

/** Field of view in degrees of a constructed camera */
#define BOX_CAMERA_FOV (60.0f)

/** Near plane distance of a constructed camera */
#define BOX_CAMERA_NEAR (0.1f)

/** Far plane distance of a constructed camera */
#define BOX_CAMERA_FAR (1000.0f)

/** Visible height in world units of a constructed orthographic camera */
#define BOX_CAMERA_ORTHO_HEIGHT (10.0f)

/** Perspective or orthographic camera, values apply as specified, defaults are set by its ctor */
ECS_STRUCT(Camera, {
    float fov;           /**< Vertical field of view in degrees, default 60 */
    float near_;         /**< Near plane distance, default 0.1, a perspective camera with
                          *   near <= 0 draws nothing */
    float far_;          /**< Far plane distance, default 1000, 0 has none in perspective;
                          *   far <= near (other than a perspective 0) draws nothing */
    float aspect_ratio;  /**< Width over height, 0 follows the surface */
    bool ortho;          /**< Use an orthographic projection */
    float ortho_height;  /**< Visible height in world units when orthographic, default 10 */
    float ortho_scale;   /**< Surface points per world unit, 0 uses ortho_height */
});

/** Build the reversed Z projection of a camera for a surface.
 *
 * Depth is 1 at the near plane and 0 at the far plane, or at infinity for a perspective
 * camera without a far plane. The aspect ratio follows the surface unless the camera sets
 * one; ortho_scale divides the surface height in points into the visible height.
 *
 * @param camera The camera
 * @param width Width of the surface in points
 * @param height Height of the surface in points
 * @param out Receives the projection matrix
 */
void box_camera_projection(
    const Camera *camera,
    float width,
    float height,
    mat4 out);

/** Check whether the planes of a camera leave a depth range to draw in.
 *
 * Orthographic cameras need far > near; perspective cameras need near > 0 and a far plane of
 * 0 (none) or beyond near. A camera without a depth range draws nothing and warns when set.
 *
 * @param camera The camera
 * @return Whether the camera has a depth range
 */
bool box_camera_has_depth(
    const Camera *camera);

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
