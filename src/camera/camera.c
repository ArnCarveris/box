/**
 * @file camera/camera.c
 * @brief Registers the box.camera module and builds reversed Z camera matrices.
 */

#define BOX_CAMERA_IMPL
#include <box.h>

/** Construct Camera values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_camera_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Camera *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Camera){
            .fov = BOX_CAMERA_FOV,
            .near_ = BOX_CAMERA_NEAR,
            .far_ = BOX_CAMERA_FAR,
            .ortho_height = BOX_CAMERA_ORTHO_HEIGHT
        };
    }
}

/** Build a reversed Z orthographic projection.
 *
 * @param camera The camera
 * @param aspect Width over height
 * @param height Height of the surface in points
 * @param out Receives the projection matrix
 */
static void box_camera_ortho(
    const Camera *camera,
    float aspect,
    float height,
    mat4 out)
{
    float n = camera->near_;
    float f = camera->far_;
    float visible = camera->ortho_height;
    if (camera->ortho_scale > 0 && height > 0) {
        visible = height / camera->ortho_scale;
    }

    glm_mat4_zero(out);
    out[0][0] = 2.0f / (visible * aspect);
    out[1][1] = 2.0f / visible;
    out[2][2] = 1.0f / (f - n);
    out[3][2] = f / (f - n);
    out[3][3] = 1.0f;
}

/** Build a reversed Z perspective projection.
 *
 * @param camera The camera
 * @param aspect Width over height
 * @param out Receives the projection matrix
 */
static void box_camera_perspective(
    const Camera *camera,
    float aspect,
    mat4 out)
{
    float n = camera->near_;
    float fov = camera->fov;
    float focal = 1.0f / tanf(glm_rad(fov) * 0.5f);
    glm_mat4_zero(out);
    out[0][0] = focal / aspect;
    out[1][1] = focal;
    out[2][3] = -1.0f;
    if (camera->far_ > n) {
        out[2][2] = n / (camera->far_ - n);
        out[3][2] = n * camera->far_ / (camera->far_ - n);
    } else {
        out[3][2] = n;
    }
}

void box_camera_projection(
    const Camera *camera,
    float width,
    float height,
    mat4 out)
{
    float aspect = camera->aspect_ratio;
    if (aspect <= 0) {
        aspect = width > 0 && height > 0 ? width / height : 1.0f;
    }

    if (camera->ortho) {
        box_camera_ortho(camera, aspect, height, out);
    } else {
        box_camera_perspective(camera, aspect, out);
    }
}

void BoxCameraImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCamera);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Camera);
    ecs_set_hooks(world, Camera, { .ctor = box_camera_ctor });
}
