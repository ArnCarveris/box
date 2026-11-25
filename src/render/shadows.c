/**
 * @file render/shadows.c
 * @brief Shadow settings of the views of a packet, cascade splits and cascade fitting.
 *
 * The main thread resolves View.shadows and splits the shadowed view depths into cascades;
 * the renderer fits each cascade to its target with box_render_shadow_cascade, because the
 * view frustum follows the aspect of the target. Views with shadows sample the spot tiles too.
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
    out->filter_size |= 1;
    out->max_distance = glm_max(settings->max_distance, 0.0f);
    out->split_lambda = box_shadow_clampf(settings->split_lambda, 0.0f, 1.0f);
    out->fade = box_shadow_clampf(settings->fade, 0.0f, 1.0f);
    out->blend = box_shadow_clampf(settings->blend, 0.0f, BOX_RENDER_SHADOW_BLEND_MAX);
    out->depth_bias = glm_max(settings->depth_bias, 0.0f);
    out->normal_offset = glm_max(settings->normal_offset, 0.0f);
    out->min_caster_texels = glm_max(settings->min_caster_texels, 0.0f);
    for (int32_t i = 0; i < BOX_RENDER_SHADOW_CASCADES; i ++) {
        out->interval[i] = settings->interval[i] > 1 ? settings->interval[i] : 1;
    }

    out->sway_interval = settings->sway_interval > 1 ? settings->sway_interval : 1;
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

/** Compute the center of a cascade in f64 from the camera, snapped to whole texels in light
 * space as box_render_shadow_cascade snaps it in f32, so the snap stays fixed in the local frame
 * however far from its origin the cascade is.
 *
 * @param camera World transform of the camera
 * @param depth View depth of the center of the bounding sphere
 * @param right The x axis of the light space
 * @param up The y axis of the light space
 * @param forward The direction the light travels in
 * @param texel World size of a texel
 * @param extent World half extent of the cascade
 * @param out Receives the light space x and y of the center and the depth of the far plane
 */
static void box_shadow_center(
    mat4 camera,
    float depth,
    const vec3 right,
    const vec3 up,
    const vec3 forward,
    float texel,
    float extent,
    double out[3])
{
    double center[3], x = 0, y = 0, z = 0, step = (double)texel;
    int32_t k;
    for (k = 0; k < 3; k ++) {
        center[k] = (double)camera[3][k] - (double)camera[2][k] * (double)depth;
    }

    for (k = 0; k < 3; k ++) {
        x += (double)right[k] * center[k];
        y += (double)up[k] * center[k];
        z += (double)forward[k] * center[k];
    }

    out[0] = round(x / step) * step;
    out[1] = round(y / step) * step;
    out[2] = round(z / step) * step + (double)extent;
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
    out->extent = extent;
    box_shadow_center(camera, depth, right, up, forward, texel, extent, out->center);
}

void box_render_cascade_relative(
    const box_render_cascade_t *cascade,
    const double delta[3],
    mat4 out)
{
    double t[3];
    int32_t k;
    glm_mat4_copy(ECS_CONST_CAST(vec4*, cascade->matrix), out);
    if (delta[0] == 0.0 && delta[1] == 0.0 && delta[2] == 0.0) {
        goto done;
    }

    t[0] = -cascade->center[0] / (double)cascade->extent;
    t[1] = -cascade->center[1] / (double)cascade->extent;
    t[2] = cascade->center[2] / (double)cascade->depth_range;
    for (k = 0; k < 3; k ++) {
        t[0] += (double)cascade->matrix[k][0] * delta[k];
        t[1] += (double)cascade->matrix[k][1] * delta[k];
        t[2] += (double)cascade->matrix[k][2] * delta[k];
    }

    out[3][0] = (float)t[0];
    out[3][1] = (float)t[1];
    out[3][2] = (float)t[2];
done:
    return;
}

/** What the cascades of a view were fitted to: a change renders every cascade at once */
typedef struct box_shadows_key_t {
    int32_t cascades;       /**< Cascades */
    int32_t resolution;     /**< Edge of a cascade in texels */
    float max_distance;     /**< ShadowSettings.max_distance */
    float split_lambda;     /**< ShadowSettings.split_lambda */
    float min_texels;       /**< ShadowSettings.min_caster_texels */
    float near_;            /**< View depth the first cascade starts at */
    float splits[BOX_RENDER_SHADOW_CASCADES]; /**< View depth each cascade ends at */
    float direction[3];     /**< Direction toward the light */
    Camera camera;          /**< Settings of the camera, its projection */
} box_shadows_key_t;

/** The cascades of a view in the earlier extracts */
typedef struct box_shadows_history_t {
    box_shadows_key_t key;  /**< What the cascades were fitted to in the last extract */
    int64_t count;          /**< Extracts since every cascade last rendered */
    int64_t seen;           /**< Extract that last resolved the cascades of the view */
    uint32_t rendered;      /**< Cascades the last extract scheduled */
} box_shadows_history_t;

/** State of the shadow extract */
typedef struct box_shadows_t {
    ecs_map_t views;        /**< View entity to box_shadows_history_t */
    int64_t extract;        /**< Extracts run */
    int32_t rendered;       /**< Cascades the extract scheduled */
    int32_t kept;           /**< Cascades the extract kept from an earlier frame */
} box_shadows_t;

/** Check whether two cascade keys are the same.
 *
 * @param a The first key
 * @param b The second key
 * @return Whether every member is equal
 */
static bool box_shadows_key_equal(
    const box_shadows_key_t *a,
    const box_shadows_key_t *b)
{
    int32_t i;
    bool result = a->cascades == b->cascades && a->resolution == b->resolution &&
        a->max_distance == b->max_distance && a->split_lambda == b->split_lambda &&
        a->min_texels == b->min_texels && a->near_ == b->near_ &&
        a->camera.fov == b->camera.fov && a->camera.near_ == b->camera.near_ &&
        a->camera.far_ == b->camera.far_ && a->camera.aspect_ratio == b->camera.aspect_ratio &&
        a->camera.ortho == b->camera.ortho && a->camera.ortho_height == b->camera.ortho_height &&
        a->camera.ortho_scale == b->camera.ortho_scale;
    for (i = 0; i < BOX_RENDER_SHADOW_CASCADES; i ++) {
        result = result && a->splits[i] == b->splits[i];
    }

    for (i = 0; i < 3; i ++) {
        result = result && a->direction[i] == b->direction[i];
    }

    return result;
}

/** Decide which cascades of a view render in this frame.
 *
 * Each cascade renders when the extracts since every cascade last rendered, plus its index,
 * are a multiple of its interval; every cascade renders when the view is new to the extract or
 * what its cascades are fitted to changed.
 *
 * While casters with a WindSway sway in its scene the interval of a cascade is at most
 * ShadowSettings.sway_interval, 1 by default: every frame.
 *
 * @param state The state of the extract
 * @param v The view, with its shadows resolved
 * @param s The resolved settings of the view
 * @param swaying Whether the scene of the view has casters that sway in the wind
 */
static void box_shadows_schedule(
    box_shadows_t *state,
    box_render_view_t *v,
    const ShadowSettings *s,
    bool swaying)
{
    box_render_view_shadows_t *out = &v->shadows;
    box_shadows_history_t *h = ecs_map_get_deref(
        &state->views, box_shadows_history_t, v->view);
    box_shadows_key_t key;
    uint32_t all = (1u << out->cascades) - 1u;
    int32_t i;
    ecs_os_memset_t(&key, 0, box_shadows_key_t);
    key.cascades = out->cascades;
    key.resolution = out->resolution;
    key.max_distance = s->max_distance;
    key.split_lambda = s->split_lambda;
    key.min_texels = out->min_texels;
    key.near_ = out->near_;
    for (i = 0; i < out->cascades; i ++) {
        key.splits[i] = out->splits[i];
        out->reuse = out->reuse || s->interval[i] > 1;
    }

    glm_vec3_copy(out->direction, key.direction);
    key.camera = v->camera_data;
    if (!h) {
        h = ecs_os_calloc_t(box_shadows_history_t);
        ecs_map_insert_ptr(&state->views, v->view, h);
        h->count = -1;
    }

    if (h->count < 0 || !box_shadows_key_equal(&h->key, &key)) {
        h->key = key;
        h->count = 0;
        out->render = all;
    } else {
        h->count ++;
        for (i = 0; i < out->cascades; i ++) {
            int32_t interval = s->interval[i];
            if (swaying && interval > s->sway_interval) {
                interval = s->sway_interval;
            }

            if ((h->count + i) % interval == 0) {
                out->render |= 1u << i;
            }
        }
    }

    h->seen = state->extract;
    h->rendered = out->render;
    for (i = 0; i < out->cascades; i ++) {
        if (out->render & (1u << i)) {
            state->rendered ++;
        } else {
            state->kept ++;
        }
    }
}

/** Resolve the shadows of a view of the packet.
 *
 * @param world The world
 * @param state The state of the extract
 * @param v The view
 */
static void box_shadows_view(
    ecs_world_t *world,
    box_shadows_t *state,
    box_render_view_t *v)
{
    box_render_view_shadows_t *out = &v->shadows;
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    const Camera *c = &v->camera_data;
    ShadowSettings s;
    ecs_os_memset_t(out, 0, box_render_view_shadows_t);
    if (!view || !view->shadows.enabled || !v->lights.enabled ||
        view->debug.wireframe == WireframeOnly)
    {
        goto done;
    }

    box_render_shadow_settings(&view->shadows, &s);
    out->filter_size = s.filter_size;
    out->depth_bias = s.depth_bias;
    out->normal_offset = s.normal_offset;
    out->tiles = box_render_lights_atlas(box_render_lights_state(world),
        view->lighting.scene ? view->lighting.scene : v->scene, &out->atlas);
    out->spots = out->tiles > 0 && (v->features & BOX_RENDER_FEATURE_PUNCTUAL_SHADOWS);
    if (!v->lights.directional || glm_vec3_norm(v->lights.sun) < 1e-6f) {
        goto done;
    }

    float near_ = glm_max(c->near_, 1e-3f);
    float far_ = c->far_ > near_ || c->ortho ? c->far_ : s.max_distance;
    float distance = glm_min(s.max_distance, far_);
    if (distance <= near_) {
        goto done;
    }

    out->enabled = true;
    out->cascades = s.cascades;
    out->resolution = s.resolution;
    out->fade = s.fade;
    out->blend = s.blend;
    out->min_texels = s.min_caster_texels;
    out->near_ = near_;
    box_render_shadow_splits(near_, distance, s.split_lambda, s.cascades, out->splits);
    glm_vec3_normalize_to(v->lights.sun, out->direction);
    box_shadows_schedule(state, v, &s, box_render_draws_swaying(world, v->scene));
done:
    return;
}

/** Resolve the shadows of every view of the packet, then forget the views it did not resolve
 * cascades for and write the cascade counts of RenderStatus.
 *
 * @param it The iterator of the system, its ctx the state of the extract
 */
static void BoxRenderExtractShadows(
    ecs_iter_t *it)
{
    box_shadows_t *state = it->ctx;
    box_render_packet_t *p = box_render_packet(it->world);
    int32_t i, count = ecs_vec_count(&p->views);
    state->extract ++;
    state->rendered = 0;
    state->kept = 0;
    for (i = 0; i < count; i ++) {
        box_shadows_view(it->world, state, ecs_vec_get_t(&p->views, box_render_view_t, i));
    }

    ecs_map_iter_t mit = ecs_map_iter(&state->views);
    while (ecs_map_next(&mit)) {
        box_shadows_history_t *h = ecs_map_ptr(&mit);
        if (h->seen != state->extract) {
            ecs_map_remove(&state->views, ecs_map_key(&mit));
            ecs_os_free(h);
            mit = ecs_map_iter(&state->views);
        }
    }

    RenderStatus *status = ecs_singleton_get_mut(it->world, RenderStatus);
    if (status) {
        status->shadow_cascades = state->rendered;
        status->shadow_cascades_kept = state->kept;
    }
}

/** Free the state of the shadow extract.
 *
 * @param ctx The state
 */
static void box_shadows_free(
    void *ctx)
{
    box_shadows_t *state = ctx;
    ecs_map_iter_t it = ecs_map_iter(&state->views);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&state->views);
    ecs_os_free(state);
}

uint32_t box_render_shadow_rendered(
    const ecs_world_t *world,
    ecs_entity_t view)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Shadows");
    const ecs_system_t *sys = system ? ecs_system_get(world, system) : NULL;
    const box_shadows_t *state = sys ? sys->ctx : NULL;
    const box_shadows_history_t *h = state
        ? ecs_map_get_deref(&state->views, box_shadows_history_t, view)
        : NULL;

    return h && h->seen == state->extract ? h->rendered : 0;
}

void box_render_shadows_reset(
    ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Shadows");
    const ecs_system_t *sys = system ? ecs_system_get(world, system) : NULL;
    box_shadows_t *state = sys ? sys->ctx : NULL;
    if (!state) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&state->views);
    while (ecs_map_next(&it)) {
        box_shadows_history_t *h = ecs_map_ptr(&it);
        h->count = -1;
    }

done:
    return;
}

void box_render_shadows_init(
    ecs_world_t *world)
{
    box_shadows_t *state = ecs_os_calloc_t(box_shadows_t);
    ecs_map_init(&state->views, NULL);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Shadows" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractShadows,
        .ctx = state,
        .ctx_free = box_shadows_free
    });
}
