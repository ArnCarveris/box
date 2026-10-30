/**
 * @file command/cmd_fit.c
 * @brief The fit command: move a camera to a tight fit of the scene it draws.
 *
 * The camera keeps its direction and backs off from the center of the content until the
 * corners of every mesh lie inside its frustum. The distance follows from the corners of the
 * local bounds of the meshes, not from a bounding sphere, so the content fills the view.
 */

#include "command.h"
#include "../render/render.h"
#include <float.h>
#include <math.h>

/** Part of the frustum the content fills on its tightest axis */
#define BOX_CMD_FIT_FILL (0.9f)

/** Part of the depth of the content the far plane is raised past it */
#define BOX_CMD_FIT_FAR_MARGIN (0.05f)

/** Smallest extent of the content in world units, so a small mesh or a point does not put the
 * camera inside it */
#define BOX_CMD_FIT_MIN_EXTENT (0.5f)

/** The content of a fit: the world space corners of the meshes of a scene */
typedef struct box_cmd_fit_content_t {
    ecs_vec_t corners;      /**< vec3, 8 per mesh */
    vec3 min;               /**< Minimum corner of the combined world box */
    vec3 max;               /**< Maximum corner of the combined world box */
    int32_t count;          /**< Number of entities with a mesh */
} box_cmd_fit_content_t;

/** Check whether an entity is a camera or one of its descendants.
 *
 * @param world The world
 * @param e The entity
 * @param camera The camera
 * @return Whether the entity moves with the camera
 */
static bool box_cmd_fit_held(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t camera)
{
    while (e && e != camera) {
        e = ecs_get_parent(world, e);
    }

    return e == camera;
}

/** Collect the content a camera draws: the meshes of its scene, without the camera and its
 * descendants. Prefabs and disabled entities are not drawn, so they do not count.
 *
 * @param world The world
 * @param camera The camera
 * @param content Receives the content
 */
static void box_cmd_fit_collect(
    ecs_world_t *world,
    ecs_entity_t camera,
    box_cmd_fit_content_t *content)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t scene = box_render_scene_of(world, camera);
    ecs_iter_t it = ecs_each(world, ShapeMesh);
    while (ecs_each_next(&it)) {
        int32_t i, c;
        if (ecs_table_has_id(world, it.table, EcsPrefab) ||
            ecs_table_has_id(world, it.table, EcsDisabled))
        {
            continue;
        }

        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            vec3 corners[8];
            if (box_render_scene_of(world, e) != scene || box_cmd_fit_held(world, e, camera) ||
                !box_cmd_render_corners(world, cache, e, corners))
            {
                continue;
            }

            for (c = 0; c < 8; c ++) {
                glm_vec3_copy(corners[c], *ecs_vec_append_t(NULL, &content->corners, vec3));
                glm_vec3_minv(content->min, corners[c], content->min);
                glm_vec3_maxv(content->max, corners[c], content->max);
            }

            content->count ++;
        }
    }
}

/** Get the aspect ratio of a camera: its own when set, else that of the surface that renders
 * the first view of the camera, else that of the surface shown first.
 *
 * @param world The world
 * @param camera The camera entity
 * @param cam The camera
 * @return Width over height
 */
static float box_cmd_fit_aspect(
    ecs_world_t *world,
    ecs_entity_t camera,
    const Camera *cam)
{
    float result = cam->aspect_ratio;
    if (result <= 0) {
        ecs_entity_t view = box_cmd_render_view(world, camera, 0, false);
        float width = 0, height = 0;
        if (!view || !box_surface_view_size(world, NULL, view, &width, &height) ||
            width <= 0 || height <= 0)
        {
            int32_t w, h;
            box_cmd_render_size(world, &w, &h);
            width = (float)w;
            height = (float)h;
        }

        result = width / height;
    }

    return result;
}

/** Get the axes of a camera from its world transform.
 *
 * The camera looks along its local -Z axis. The right axis comes from world up and the
 * forward axis, or is the X axis when the camera looks straight up or down.
 *
 * @param transform The world transform of the camera, NULL for none
 * @param right Receives the right axis
 * @param up Receives the up axis
 * @param forward Receives the forward axis
 */
static void box_cmd_fit_axes(
    const WorldTransform *transform,
    vec3 right,
    vec3 up,
    vec3 forward)
{
    vec3 world_up = { 0, 1, 0 };
    glm_vec3_copy((vec3){ 0, 0, -1 }, forward);
    if (transform) {
        glm_vec3_negate_to((float*)transform->m[2], forward);
        if (glm_vec3_norm(forward) < FLT_EPSILON) {
            glm_vec3_copy((vec3){ 0, 0, -1 }, forward);
        }

        glm_vec3_normalize(forward);
    }

    glm_vec3_cross(forward, world_up, right);
    if (glm_vec3_norm(right) < 1e-4f) {
        glm_vec3_copy((vec3){ 1, 0, 0 }, right);
    }

    glm_vec3_normalize(right);
    glm_vec3_cross(right, forward, up);
    glm_vec3_normalize(up);
}

/** Write a world position of a camera to its Position, in the space of its parent.
 *
 * @param world The world
 * @param camera The camera
 * @param eye The world position
 */
static void box_cmd_fit_place(
    ecs_world_t *world,
    ecs_entity_t camera,
    const vec3 eye)
{
    ecs_entity_t parent = ecs_get_parent(world, camera);
    const WorldTransform *pt = parent ? ecs_get(world, parent, WorldTransform) : NULL;
    vec3 local;
    glm_vec3_copy((float*)eye, local);
    if (pt) {
        mat4 inv;
        glm_mat4_inv((vec4*)pt->m, inv);
        glm_mat4_mulv3(inv, (float*)eye, 1.0f, local);
    }

    ecs_set(world, camera, Position, { local[0], local[1], local[2] });

    CameraController *cc = ecs_get_mut(world, camera, CameraController);
    if (cc) {
        glm_vec3_zero(cc->velocity);
        glm_vec2_zero(cc->angular_velocity);
        ecs_modified(world, camera, CameraController);
    }
}

/** The fit command: move the active camera or a camera to a tight fit of its scene.
 *
 * The corners of a cube of the minimum extent around the center, along the axes of the
 * camera, join the corners of the content. A far plane that does not reach the farthest corner
 * is raised past it; it is never lowered, and a far plane of 0 (none) stays 0.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The camera, omitted for the active camera
 * @param result Receives the description of the fit
 */
static void box_cmd_fit(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t camera = box_cmd_render_camera(world, argc, argv);
    box_cmd_fit_content_t content = {
        .min = { FLT_MAX, FLT_MAX, FLT_MAX },
        .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX }
    };
    vec3 center, right, up, forward, eye;
    char *path = NULL;
    int32_t i;
    ecs_vec_init_t(NULL, &content.corners, vec3, 0);
    if (!camera) {
        goto done;
    }

    box_cmd_fit_collect(world, camera, &content);
    if (!content.count) {
        ecs_err("no content to fit");
        goto done;
    }

    float aspect = box_cmd_fit_aspect(world, camera, ecs_get(world, camera, Camera));
    Camera *cam = ecs_get_mut(world, camera, Camera);
    float fov = cam->fov > 0 ? cam->fov : BOX_CAMERA_FOV;
    float near_ = cam->near_ > 0 ? cam->near_ : BOX_CAMERA_NEAR;
    float tan_y = tanf(glm_rad(fov) * 0.5f) * BOX_CMD_FIT_FILL;
    float tan_x = tan_y * aspect;
    float distance = -FLT_MAX, extent_x = 0, extent_y = 0, depth = -FLT_MAX;
    glm_vec3_center(content.min, content.max, center);
    box_cmd_fit_axes(ecs_get(world, camera, WorldTransform), right, up, forward);

    for (i = 0; i < 8; i ++) {
        vec3 *corner = ecs_vec_append_t(NULL, &content.corners, vec3);
        vec3 offset = {
            (i & 1) ? BOX_CMD_FIT_MIN_EXTENT : -BOX_CMD_FIT_MIN_EXTENT,
            (i & 2) ? BOX_CMD_FIT_MIN_EXTENT : -BOX_CMD_FIT_MIN_EXTENT,
            (i & 4) ? BOX_CMD_FIT_MIN_EXTENT : -BOX_CMD_FIT_MIN_EXTENT
        };

        glm_vec3_copy(center, *corner);
        glm_vec3_muladds(right, offset[0], *corner);
        glm_vec3_muladds(up, offset[1], *corner);
        glm_vec3_muladds(forward, offset[2], *corner);
    }

    for (i = 0; i < ecs_vec_count(&content.corners); i ++) {
        vec3 offset;
        glm_vec3_sub(*ecs_vec_get_t(&content.corners, vec3, i), center, offset);
        float x = fabsf(glm_vec3_dot(offset, right));
        float y = fabsf(glm_vec3_dot(offset, up));
        float z = glm_vec3_dot(offset, forward);
        float need = near_ - z;
        if (!cam->ortho) {
            need = glm_max(glm_max(x / tan_x, y / tan_y) - z, need);
        }

        distance = glm_max(distance, need);
        depth = glm_max(depth, z);
        extent_x = glm_max(extent_x, x);
        extent_y = glm_max(extent_y, y);
    }

    depth += distance;
    bool modified = false;
    if (cam->ortho) {
        cam->ortho_height = glm_max(extent_y, extent_x / aspect) * 2.0f / BOX_CMD_FIT_FILL;
        cam->ortho_scale = 0;
        modified = true;
    }

    if (cam->far_ > 0 && cam->far_ < depth) {
        cam->far_ = depth * (1.0f + BOX_CMD_FIT_FAR_MARGIN);
        modified = true;
    }

    if (modified) {
        ecs_modified(world, camera, Camera);
    }

    glm_vec3_copy(center, eye);
    glm_vec3_muladds(forward, -distance, eye);
    box_cmd_fit_place(world, camera, eye);

    path = ecs_get_path(world, camera);
    *(char**)result->ptr = flecs_asprintf(
        "fit %s to %d entities, center (%.2f, %.2f, %.2f), distance %.2f",
        path, content.count, (double)center[0], (double)center[1], (double)center[2],
        (double)distance);
done:
    ecs_vec_fini_t(NULL, &content.corners, vec3);
    ecs_os_free(path);
}

void box_cmd_fit_init(
    ecs_world_t *world)
{
    box_command_register(world,
        "Move the active camera or a camera so that it sees the whole scene",
        (ecs_function_desc_t){
            .name = "fit",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "camera", .type = ecs_id(ecs_entity_t), .optional = true,
                  .brief = "Camera to fit (default: the active camera)" }
            },
            .callback = box_cmd_fit
        });
}
