/**
 * @file command/fit.c
 * @brief The fit command: moves a camera back until it sees the whole scene.
 *
 * The content is every mesh the view of the camera draws: not hidden, not part
 * of a UI tree, in a layer the view shows and not attached to the camera.
 *
 * The camera keeps its direction, centers the bounds of the content and backs
 * off until every corner of every mesh lies inside its frustum.
 */

#include "command_internal.h"
#include "../render/render_internal.h"
#include <math.h>

/** Degrees in one radian, the unit the camera field of view is given in */
#define BOX_FIT_RADIANS (3.14159265f / 180.0f)

/** Vertical field of view in degrees of a camera that does not set one */
#define BOX_FIT_FOV (60.0f)

/** Near plane of a camera that does not set one */
#define BOX_FIT_NEAR (0.1f)

/** Fraction of the frustum the content fills, the rest is margin */
#define BOX_FIT_FILL (0.9f)

/** Smallest extent content is framed with, in world units */
#define BOX_FIT_EXTENT (0.5f)

/** Bounds of the content of a fit */
typedef struct box_fit_bounds_t {
    vec3 min;       /**< Minimum corner */
    vec3 max;       /**< Maximum corner */
    int32_t count;  /**< Entities that contributed to the bounds */
    ecs_vec_t points; /**< vec3 world corners of the meshes */
} box_fit_bounds_t;

/** Find the view that renders a camera, the one that draws into the surface first.
 *
 * @param world The world
 * @param camera The camera entity
 * @return The view entity, 0 when no view renders the camera
 */
static ecs_entity_t box_fit_view(
    ecs_world_t *world,
    ecs_entity_t camera)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        const View *views = ecs_field(&it, View, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            bool surface = !views[i].target;
            if (views[i].camera == camera && (!result || surface)) {
                result = it.entities[i];
            }
        }

    }

    return result;
}

/** Get the width over height of the image a camera renders.
 *
 * @param world The world
 * @param camera The camera
 * @param view The view that renders the camera, 0 for none
 * @return The aspect ratio, 1 when nothing sets it
 */
static float box_fit_aspect(
    ecs_world_t *world,
    const Camera *camera,
    ecs_entity_t view)
{
    float result = 1.0f;
    const View *v = view ? ecs_get(world, view, View) : NULL;
    const RenderTarget *target = v && v->target ?
        ecs_get(world, v->target, RenderTarget) : NULL;
    const Surface *surface = ecs_singleton_get(world, Surface);
    if (camera->aspect_ratio > 0) {
        result = camera->aspect_ratio;
    } else if (target && target->width > 0 && target->height > 0) {
        result = (float)target->width / (float)target->height;
    } else if (surface && surface->actual_width > 0 && surface->actual_height > 0) {
        result = (float)surface->actual_width / (float)surface->actual_height;
    } else if (surface && surface->width > 0 && surface->height > 0) {
        result = (float)surface->width / (float)surface->height;
    }

    return result;
}

/** Test whether an entity is the camera or one of its descendants.
 *
 * @param world The world
 * @param e The entity
 * @param camera The camera
 * @return true when the entity moves with the camera
 */
static bool box_fit_on_camera(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t camera)
{
    bool result = false;
    ecs_entity_t cur = e;
    while (cur && !result) {
        result = cur == camera;
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Test whether the view of a fit draws an entity.
 *
 * @param world The world
 * @param state The registry
 * @param view The view, 0 for a camera that no view renders
 * @param e The entity
 * @return true when the view draws the entity
 */
static bool box_fit_shown(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t view,
    ecs_entity_t e)
{
    bool result = !box_hierarchy_ancestor_has(world, e, Hidden);
    if (!result || !state->layer_used) {
        goto done;
    }

    uint32_t mask = box_render_entity_layers(world, state, e);
    if (mask & state->ui_mask) {
        result = false;
        goto done;
    }

    const View *v = view ? ecs_get(world, view, View) : NULL;
    if (!v) {
        goto done;
    }

    uint32_t view_mask = box_render_view_layers(state, v->layers);
    bool strict = v->strict_layers && view_mask != BOX_VIEW_LAYER_ALL;
    result = mask ? (mask & view_mask) != 0 : !strict;
done:
    return result;
}

/** Grow the bounds of a fit by the world bounds of one mesh.
 *
 * @param bounds The bounds
 * @param mesh The render mesh
 * @param transform The world transform of the mesh
 */
static void box_fit_grow(
    box_fit_bounds_t *bounds,
    const RenderMesh *mesh,
    const WorldTransform *transform)
{
    vec3 lo = vec3_mul(mesh->bounds_min, mesh->scale);
    vec3 hi = vec3_mul(mesh->bounds_max, mesh->scale);
    int32_t corner;
    for (corner = 0; corner < 8; corner ++) {
        vec3 c = vec3_new(
            corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z);
        vec3 p = mat4_transform_point(&transform->m, c);
        bool first = !bounds->count && !corner;
        bounds->min = first ? p : vec3_min(bounds->min, p);
        bounds->max = first ? p : vec3_max(bounds->max, p);
        ecs_vec_append_t(NULL, &bounds->points, vec3)[0] = p;
    }

    bounds->count ++;
}

/** Collect the world bounds of the meshes the view of a camera draws.
 *
 * @param world The world
 * @param camera The camera
 * @param view The view that renders the camera, 0 for none
 * @param bounds Receives the bounds
 */
static void box_fit_collect(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t view,
    box_fit_bounds_t *bounds)
{
    RenderState *state = box_render_state(world);
    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(RenderMesh), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        }
    });

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const RenderMesh *mesh = ecs_field(&it, RenderMesh, 0);
        const WorldTransform *transform = ecs_field(&it, WorldTransform, 1);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (box_fit_on_camera(world, e, camera) ||
                !box_fit_shown(world, state, view, e))
            {
                continue;
            }

            box_fit_grow(bounds, &mesh[i], &transform[i]);
        }

    }

    ecs_query_fini(q);
}

/** Get the world position and the unit direction a camera looks in.
 *
 * A camera with a LookAt looks at its target, any other looks along the +Z
 * axis of its world transform.
 *
 * @param world The world
 * @param camera The camera
 * @param eye Receives the world position
 * @return The direction
 */
static vec3 box_fit_forward(
    ecs_world_t *world,
    ecs_entity_t camera,
    vec3 *eye)
{
    const WorldTransform *transform = ecs_get(world, camera, WorldTransform);
    const Position *p = ecs_get(world, camera, Position);
    const LookAt *look = ecs_get(world, camera, LookAt);
    eye[0] = transform ? mat4_get_translation(&transform->m) :
        p ? vec3_new(p->x, p->y, p->z) : vec3_new(0, 0, 0);
    vec3 result = vec3_new(0, 0, 1);
    if (look) {
        result = vec3_sub(vec3_new(look->x, look->y, look->z), eye[0]);
    } else if (transform) {
        result = mat4_transform_dir(&transform->m, vec3_new(0, 0, 1));
    }

    if (vec3_length(result) <= 0.0f) {
        result = vec3_new(0, 0, 1);
    }

    return vec3_normalize(result);
}

/** Compute the distance from the center of the content at which a camera sees
 * every corner of its meshes, and the height an orthographic camera needs.
 *
 * @param camera The camera
 * @param aspect Width over height of its image
 * @param forward The unit direction it looks in
 * @param bounds The bounds of the content
 * @param height Receives the visible height an orthographic camera needs
 * @return The distance
 */
static float box_fit_distance(
    const Camera *camera,
    float aspect,
    vec3 forward,
    const box_fit_bounds_t *bounds,
    float *height)
{
    vec3 up = vec3_new(0, 1, 0);
    vec3 right = vec3_cross(up, forward);
    if (vec3_length(right) < 1e-4f) {
        right = vec3_new(1, 0, 0);
    }

    right = vec3_normalize(right);
    up = vec3_normalize(vec3_cross(forward, right));
    float fov = (camera->fov > 0 ? camera->fov : BOX_FIT_FOV) * BOX_FIT_RADIANS;
    float near = camera->near_ > 0 ? camera->near_ : BOX_FIT_NEAR;
    float tan_y = tanf(fov * 0.5f) * BOX_FIT_FILL;
    float tan_x = tanf(fov * 0.5f) * aspect * BOX_FIT_FILL;
    vec3 center = vec3_scale(vec3_add(bounds->min, bounds->max), 0.5f);
    const vec3 *points = ecs_vec_first_t(&bounds->points, vec3);
    int32_t i, count = ecs_vec_count(&bounds->points);
    float result = 0.0f, ortho = 0.0f;
    for (i = 0; i < count + 8; i ++) {
        vec3 offset = i < count ? vec3_sub(points[i], center) : vec3_new(
            i & 1 ? BOX_FIT_EXTENT : -BOX_FIT_EXTENT,
            i & 2 ? BOX_FIT_EXTENT : -BOX_FIT_EXTENT,
            i & 4 ? BOX_FIT_EXTENT : -BOX_FIT_EXTENT);
        float x = fabsf(vec3_dot(offset, right));
        float y = fabsf(vec3_dot(offset, up));
        float z = vec3_dot(offset, forward);
        float need = camera->orthographic ? near - z :
            fmaxf(fmaxf(x / tan_x, y / tan_y) - z, near - z);
        result = fmaxf(result, need);
        ortho = fmaxf(ortho, fmaxf(y, x / aspect) * 2.0f / BOX_FIT_FILL);
    }

    height[0] = ortho;

    return result;
}

/** Place a camera at a world position looking at a world point.
 *
 * The position is converted to the space of the parent of the camera. A camera
 * without a LookAt keeps its rotation, which already looks at the point.
 *
 * @param world The world
 * @param camera The camera
 * @param eye The world position
 * @param center The world point it looks at
 */
static void box_fit_place(
    ecs_world_t *world,
    ecs_entity_t camera,
    vec3 eye,
    vec3 center)
{
    vec3 local = eye;
    ecs_entity_t parent = ecs_get_parent(world, camera);
    const WorldTransform *pt = parent ? ecs_get(world, parent, WorldTransform) : NULL;
    if (pt) {
        mat4 inverse = mat4_inverse(&pt->m);
        local = mat4_transform_point(&inverse, eye);
    }

    CameraController *cc = ecs_get_mut(world, camera, CameraController);
    if (cc) {
        cc->velocity = vec3_new(0, 0, 0);
        cc->angular_velocity = (vec2){0, 0};
    }

    ecs_set(world, camera, Position, {local.x, local.y, local.z});
    if (ecs_has(world, camera, LookAt)) {
        ecs_set(world, camera, LookAt, {center.x, center.y, center.z});
    }

}

/** Fit a camera to the content its view draws.
 *
 * @param world The world
 * @param camera The camera
 * @return The command output, the caller frees it
 */
static char* box_fit_camera(
    ecs_world_t *world,
    ecs_entity_t camera)
{
    char *result = NULL;
    ecs_entity_t view = box_fit_view(world, camera);
    box_fit_bounds_t bounds = {0};
    ecs_vec_init_t(NULL, &bounds.points, vec3, 0);
    box_fit_collect(world, camera, view, &bounds);
    if (!bounds.count) {
        result = ecs_os_strdup(BOX_COMMAND_ERROR_PREFIX "no content to fit");
        goto done;
    }

    const Camera *c = ecs_get(world, camera, Camera);
    vec3 eye;
    vec3 forward = box_fit_forward(world, camera, &eye);
    float height = 0.0f;
    float aspect = box_fit_aspect(world, c, view);
    float distance = box_fit_distance(c, aspect, forward, &bounds, &height);
    vec3 center = vec3_scale(vec3_add(bounds.min, bounds.max), 0.5f);
    eye = vec3_sub(center, vec3_scale(forward, distance));
    if (c->orthographic) {
        Camera *mut = ecs_get_mut(world, camera, Camera);
        mut->ortho_height = height;
        ecs_modified(world, camera, Camera);
    }

    box_fit_place(world, camera, eye, center);
    char *path = box_path_str(world, camera);
    result = flecs_asprintf(
        "fit %s to %d entities, center (%.2f, %.2f, %.2f), distance %.2f\n",
        path, bounds.count, (double)center.x, (double)center.y, (double)center.z,
        (double)distance);
    ecs_os_free(path);
done:
    ecs_vec_fini_t(NULL, &bounds.points, vec3);

    return result;
}

/** The fit command: fit the active or the named camera to the scene.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_fit_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *arg = *(char**)argv[1].ptr;
    ecs_entity_t camera = 0;
    char *out = NULL;
    char *error = box_capture_camera(world, session, arg, &camera);
    if (error) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "%s", error);
        ecs_os_free(error);
        goto done;
    }

    out = box_fit_camera(world, camera);
done:
    *(char**)result->ptr = out;
}

void box_fit_cmd_init(
    ecs_world_t *world)
{
    box_command_param_t params[] = {{
        .name = "camera", .type = ecs_id(ecs_string_t), .optional = true
    }};

    box_command_register(world, "fit",
        "Move the active or the named camera so it sees the whole scene",
        params, 1, box_fit_command);
}
