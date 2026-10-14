/**
 * @file render/shadows.c
 * @brief Shadow settings of the views of a packet, cascade splits and cascade fitting.
 *
 * The main thread resolves View.shadows and splits the shadowed view depths into cascades;
 * the renderer fits each cascade to its target with box_render_shadow_cascade, because the
 * view frustum follows the aspect of the target.
 */

#include "render.h"

/** Smallest edge of a cascade in texels */
#define BOX_SHADOW_RESOLUTION_MIN (16)

/** Largest edge of a cascade in texels, the 2D texture limit webgpu guarantees */
#define BOX_SHADOW_RESOLUTION_MAX (8192)

/** Above this vertical component the light space takes +Z as its up axis */
#define BOX_SHADOW_POLE (0.99f)

/** Clamp a float to a range.
 *
 * @param value The value
 * @param lo The lower bound
 * @param hi The upper bound
 * @return The clamped value
 */
static float box_shadow_clampf(
    float value,
    float lo,
    float hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

/** Clamp an integer to a range.
 *
 * @param value The value
 * @param lo The lower bound
 * @param hi The upper bound
 * @return The clamped value
 */
static int32_t box_shadow_count(
    int32_t value,
    int32_t lo,
    int32_t hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

void box_render_shadow_settings(
    const ShadowSettings *settings,
    ShadowSettings *out)
{
    *out = *settings;
    out->cascades = box_shadow_count(settings->cascades, 1, BOX_RENDER_SHADOW_CASCADES);
    out->resolution = box_shadow_count(
        settings->resolution, BOX_SHADOW_RESOLUTION_MIN, BOX_SHADOW_RESOLUTION_MAX);
    out->filter_size = box_shadow_count(settings->filter_size, 1, BOX_RENDER_SHADOW_FILTER_MAX);
    out->max_distance = glm_max(settings->max_distance, 0.0f);
    out->split_lambda = box_shadow_clampf(settings->split_lambda, 0.0f, 1.0f);
    out->fade = box_shadow_clampf(settings->fade, 0.0f, 1.0f);
    out->depth_bias = glm_max(settings->depth_bias, 0.0f);
    out->normal_offset = glm_max(settings->normal_offset, 0.0f);
}

void box_render_shadow_splits(
    float near_,
    float distance,
    float lambda,
    int32_t cascades,
    float *out)
{
    int32_t i;
    for (i = 0; i < cascades; i ++) {
        float t = (float)(i + 1) / (float)cascades;
        float uniform = near_ + (distance - near_) * t;
        float logarithmic = near_ * powf(distance / near_, t);
        out[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }

    if (cascades > 0) {
        out[cascades - 1] = distance;
    }
}

/** Get the half width and half height of the view frustum at a view depth.
 *
 * @param proj Projection of the view
 * @param depth The view depth
 * @param out Receives the half extents
 */
static void box_shadow_extent(
    mat4 proj,
    float depth,
    vec2 out)
{
    float scale = proj[2][3] != 0.0f ? depth : 1.0f;
    out[0] = proj[0][0] != 0.0f ? scale / proj[0][0] : scale;
    out[1] = proj[1][1] != 0.0f ? scale / proj[1][1] : scale;
}

/** Fit the bounding sphere of a slice of the view frustum, in view space.
 *
 * The center lies on the view axis where the near and far corners are equally far away, so
 * the radius only depends on the projection and the depths and stays the same as the camera
 * turns or moves.
 *
 * @param proj Projection of the view
 * @param near_ View depth the slice starts at
 * @param far_ View depth the slice ends at
 * @param radius Receives the radius
 * @return View depth of the center
 */
static float box_shadow_sphere(
    mat4 proj,
    float near_,
    float far_,
    float *radius)
{
    vec2 lo, hi;
    box_shadow_extent(proj, near_, lo);
    box_shadow_extent(proj, far_, hi);
    float a = lo[0] * lo[0] + lo[1] * lo[1];
    float b = hi[0] * hi[0] + hi[1] * hi[1];
    float z = (b + far_ * far_ - a - near_ * near_) / (2.0f * glm_max(far_ - near_, 1e-6f));
    z = box_shadow_clampf(z, near_, far_);
    float to_far = b + (far_ - z) * (far_ - z);
    float to_near = a + (near_ - z) * (near_ - z);
    *radius = sqrtf(glm_max(to_far, to_near));

    return z;
}

/** Build the axes of the light space of a light direction.
 *
 * @param direction Direction toward the light
 * @param right Receives the x axis
 * @param up Receives the y axis
 * @param forward Receives the direction the light travels in
 */
static void box_shadow_axes(
    const float *direction,
    vec3 right,
    vec3 up,
    vec3 forward)
{
    vec3 pole = {0, 1, 0};
    glm_vec3_copy(ECS_CONST_CAST(float*, direction), forward);
    glm_vec3_negate(forward);
    if (glm_vec3_norm(forward) < 1e-6f) {
        glm_vec3_copy((vec3){0, -1, 0}, forward);
    }

    glm_vec3_normalize(forward);
    if (fabsf(forward[1]) > BOX_SHADOW_POLE) {
        glm_vec3_copy((vec3){0, 0, 1}, pole);
    }

    glm_vec3_crossn(forward, pole, right);
    glm_vec3_cross(right, forward, up);
}

void box_render_shadow_cascade(
    mat4 camera,
    mat4 proj,
    float near_,
    float far_,
    const float *direction,
    int32_t resolution,
    float distance,
    box_render_cascade_t *out)
{
    vec3 right, up, forward, center;
    float radius = 0;
    float depth = box_shadow_sphere(proj, near_, far_, &radius);
    float res = (float)(resolution > 2 ? resolution : 2);
    float extent = radius * res / (res - 2.0f);
    float texel = 2.0f * extent / res;
    glm_mat4_mulv3(camera, (vec3){0, 0, -depth}, 1.0f, center);
    box_shadow_axes(direction, right, up, forward);

    float cx = roundf(glm_vec3_dot(right, center) / texel) * texel;
    float cy = roundf(glm_vec3_dot(up, center) / texel) * texel;
    float cz = roundf(glm_vec3_dot(forward, center) / texel) * texel;
    float back = glm_max(distance, extent);
    float d_near = cz - extent - back;
    float d_far = cz + extent;
    float range = d_far - d_near;

    glm_mat4_zero(out->matrix);
    out->matrix[0][0] = right[0] / extent;
    out->matrix[1][0] = right[1] / extent;
    out->matrix[2][0] = right[2] / extent;
    out->matrix[3][0] = -cx / extent;
    out->matrix[0][1] = up[0] / extent;
    out->matrix[1][1] = up[1] / extent;
    out->matrix[2][1] = up[2] / extent;
    out->matrix[3][1] = -cy / extent;
    out->matrix[0][2] = -forward[0] / range;
    out->matrix[1][2] = -forward[1] / range;
    out->matrix[2][2] = -forward[2] / range;
    out->matrix[3][2] = d_far / range;
    out->matrix[3][3] = 1.0f;
    out->texel = texel;
    out->depth_range = range;
}

/** Resolve the shadows of a view of the packet.
 *
 * @param world The world
 * @param v The view
 */
static void box_shadows_view(
    ecs_world_t *world,
    box_render_view_t *v)
{
    box_render_view_shadows_t *out = &v->shadows;
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    const Camera *c = &v->camera_data;
    ShadowSettings s;
    ecs_os_memset_t(out, 0, box_render_view_shadows_t);
    if (!view || !view->shadows.enabled || !v->lights.enabled || !v->lights.directional ||
        glm_vec3_norm(v->lights.sun) < 1e-6f)
    {
        goto done;
    }

    box_render_shadow_settings(&view->shadows, &s);
    float near_ = glm_max(c->near_, 1e-3f);
    float far_ = c->far_ > near_ || c->ortho ? c->far_ : s.max_distance;
    float distance = glm_min(s.max_distance, far_);
    if (distance <= near_) {
        goto done;
    }

    out->enabled = true;
    out->cascades = s.cascades;
    out->resolution = s.resolution;
    out->filter_size = s.filter_size;
    out->depth_bias = s.depth_bias;
    out->normal_offset = s.normal_offset;
    out->fade = s.fade;
    out->near_ = near_;
    box_render_shadow_splits(near_, distance, s.split_lambda, s.cascades, out->splits);
    glm_vec3_normalize_to(v->lights.sun, out->direction);
done:
    return;
}

/** Resolve the shadows of every view of the packet.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractShadows(
    ecs_iter_t *it)
{
    box_render_packet_t *p = box_render_packet(it->world);
    int32_t i, count = ecs_vec_count(&p->views);
    for (i = 0; i < count; i ++) {
        box_shadows_view(it->world, ecs_vec_get_t(&p->views, box_render_view_t, i));
    }
}

void box_render_shadows_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractShadows" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractShadows
    });
}
