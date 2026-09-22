/**
 * @file render/extract_shadow.c
 * @brief Cascade splits and light matrices of the shadow map of one view.
 *
 * A cascade covers a slice of the camera frustum, fitted with the bounding
 * sphere of the slice so its size never changes when the camera turns, and its
 * light matrix is snapped to texel increments so static content does not
 * shimmer when the camera moves.
 */

#include "render_internal.h"

/** Up axis of the light space of a sun that points nearly straight down */
#define BOX_SHADOW_POLE (0.99f)

const ShadowSettings* box_render_shadow_settings(
    ecs_world_t *world,
    ecs_entity_t e,
    ShadowSettings *out)
{
    const View *view = e ? ecs_get(world, e, View) : NULL;
    const ShadowSettings *set = view ? &view->shadows : NULL;
    ShadowSettings s = {
        .enabled = true,
        .cascades = BOX_SHADOW_CASCADES,
        .resolution = BOX_SHADOW_RESOLUTION,
        .max_distance = BOX_SHADOW_DISTANCE,
        .split_lambda = BOX_SHADOW_LAMBDA,
        .depth_bias = BOX_SHADOW_BIAS,
        .normal_offset = BOX_SHADOW_NORMAL,
        .filter_size = BOX_SHADOW_FILTER,
        .fade = BOX_SHADOW_FADE
    };

    if (set) {
        s = *set;
        s.cascades = set->cascades > 0 ? set->cascades : BOX_SHADOW_CASCADES;
        s.resolution = set->resolution > 0 ? set->resolution : BOX_SHADOW_RESOLUTION;
        s.max_distance = set->max_distance > 0 ? set->max_distance : BOX_SHADOW_DISTANCE;
        s.filter_size = set->filter_size > 0 ? set->filter_size : BOX_SHADOW_FILTER;
    }

    if (s.cascades > BOX_SHADOW_CASCADE_MAX) {
        s.cascades = BOX_SHADOW_CASCADE_MAX;
    }

    if (s.filter_size > BOX_SHADOW_FILTER_MAX) {
        s.filter_size = BOX_SHADOW_FILTER_MAX;
    }

    s.split_lambda = s.split_lambda < 0 ? 0 : (s.split_lambda > 1 ? 1 : s.split_lambda);
    s.fade = s.fade < 0 ? 0 : (s.fade > 1 ? 1 : s.fade);
    s.depth_bias = s.depth_bias < 0 ? 0 : s.depth_bias;
    s.normal_offset = s.normal_offset < 0 ? 0 : s.normal_offset;
    *out = s;

    return out;
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
        float p = (float)(i + 1) / (float)cascades;
        float uniform = near_ + (distance - near_) * p;
        float logarithmic = near_ * powf(distance / near_, p);
        out[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
}

/** Half width and half height of the camera frustum at a view depth.
 *
 * @param v The view
 * @param depth The view depth
 * @param out Half width and half height
 */
static void box_shadow_extent(
    const box_view_t *v,
    float depth,
    vec2 *out)
{
    float scale = v->proj[11] != 0.0f ? depth : 1.0f;
    out->x = scale / v->proj[0];
    out->y = scale / v->proj[5];
}

void box_render_shadow_corners(
    const box_view_t *v,
    float near_,
    float far_,
    vec3 *out)
{
    mat4 view;
    memcpy(view.v, v->view, sizeof(view.v));
    mat4 inv = mat4_inverse(&view);
    int32_t i;
    for (i = 0; i < 8; i ++) {
        float depth = i < 4 ? near_ : far_;
        vec2 half;
        box_shadow_extent(v, depth, &half);
        vec3 p = vec3_new((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, depth);
        out[i] = mat4_transform_point(&inv, p);
    }
}

/** Fit the bounding sphere of a camera frustum slice, in view space.
 *
 * The center sits on the view axis where the near and the far corners are
 * equally far away, so the sphere only depends on the depths of the slice and
 * keeps its radius while the camera turns.
 *
 * @param v The view
 * @param near_ View depth the slice starts at
 * @param far_ View depth the slice ends at
 * @param radius The radius of the sphere
 * @return The view space center of the sphere
 */
static vec3 box_shadow_sphere(
    const box_view_t *v,
    float near_,
    float far_,
    float *radius)
{
    vec2 lo;
    vec2 hi;
    box_shadow_extent(v, near_, &lo);
    box_shadow_extent(v, far_, &hi);
    float a = lo.x * lo.x + lo.y * lo.y;
    float b = hi.x * hi.x + hi.y * hi.y;
    float z = (b + far_ * far_ - a - near_ * near_) / (2.0f * (far_ - near_));
    if (z > far_) {
        z = far_;
    }

    if (z < near_) {
        z = near_;
    }

    float to_far = b + (far_ - z) * (far_ - z);
    float to_near = a + (near_ - z) * (near_ - z);
    *radius = sqrtf(to_far > to_near ? to_far : to_near);

    return vec3_new(0, 0, z);
}

/** Build the rotation that takes world space into the space of the sun.
 *
 * @param v The view, whose sun direction is the forward axis
 * @return The rotation
 */
static mat4 box_shadow_light_space(
    const box_view_t *v)
{
    vec3 dir = vec3_normalize(vec3_new(v->sun_dir[0], v->sun_dir[1], v->sun_dir[2]));
    vec3 up = fabsf(dir.y) > BOX_SHADOW_POLE ? vec3_new(0, 0, 1) : vec3_new(0, 1, 0);

    return mat4_look_at(vec3_new(0, 0, 0), dir, up);
}

/** Fill one cascade of a view with its light matrix, bias and bounds.
 *
 * @param v The view to fill
 * @param s The shadow settings of the view
 * @param light World to light space rotation
 * @param index Index of the cascade
 * @param near_ View depth the cascade starts at
 * @param far_ View depth the cascade ends at
 */
static void box_shadow_cascade(
    box_view_t *v,
    const ShadowSettings *s,
    const mat4 *light,
    int32_t index,
    float near_,
    float far_)
{
    float radius = 0;
    vec3 center_view = box_shadow_sphere(v, near_, far_, &radius);
    mat4 view;
    memcpy(view.v, v->view, sizeof(view.v));
    mat4 inv = mat4_inverse(&view);
    vec3 center = mat4_transform_point(&inv, center_view);

    float texel = 2.0f * radius / (float)s->resolution;
    vec3 local = mat4_transform_point(light, center);
    local.x = floorf(local.x / texel) * texel;
    local.y = floorf(local.y / texel) * texel;
    local.z = floorf(local.z / texel) * texel;

    float back = radius * (1.0f + BOX_SHADOW_EXTRUDE);
    float range = back + radius;
    mat4 offset = mat4_translation(vec3_new(-local.x, -local.y, -(local.z - back)));
    mat4 light_view = mat4_mul(&offset, light);
    mat4 proj = mat4_orthographic(2.0f * radius, 1.0f, 0.0f, range);
    mat4 m = mat4_mul(&proj, &light_view);
    memcpy(v->shadow_matrix + index * 16, m.v, sizeof(m.v));

    mat4 inv_light = mat4_inverse(light);
    vec3 snapped = mat4_transform_point(&inv_light, local);
    v->shadow_bounds[index * 4 + 0] = snapped.x;
    v->shadow_bounds[index * 4 + 1] = snapped.y;
    v->shadow_bounds[index * 4 + 2] = snapped.z;
    v->shadow_bounds[index * 4 + 3] = radius;
    v->shadow_split[index] = far_;
    v->shadow_offset[index] = s->normal_offset * texel;
    v->shadow_bias[index] = s->depth_bias * texel / range;
}

void box_render_extract_shadows(
    ecs_world_t *world,
    ecs_entity_t e,
    bool sun,
    box_view_t *v)
{
    v->shadow_cascades = 0;
    if (!v->shadows || !sun) {
        goto done;
    }

    ShadowSettings s;
    box_render_shadow_settings(world, e, &s);
    float near_ = v->cluster.near_;
    float distance = v->cluster.far_ < s.max_distance ? v->cluster.far_ : s.max_distance;
    if (distance <= near_) {
        goto done;
    }

    float splits[BOX_SHADOW_CASCADE_MAX];
    box_render_shadow_splits(near_, distance, s.split_lambda, s.cascades, splits);
    mat4 light = box_shadow_light_space(v);

    int32_t i;
    for (i = 0; i < s.cascades; i ++) {
        box_shadow_cascade(v, &s, &light, i, i ? splits[i - 1] : near_, splits[i]);
    }

    v->shadow_cascades = (uint32_t)s.cascades;
    v->shadow_resolution = (uint32_t)s.resolution;
    v->shadow_filter = (uint32_t)s.filter_size;
    v->shadow_distance = distance;
    v->shadow_fade = s.fade;
    v->shadow_texel = 1.0f / (float)s.resolution;
done:
    return;
}
