/**
 * @file render/lights_cull.c
 * @brief Culls the point and spot lights that no view of their scene can see before they are
 * collected: a light lights nothing past its range, so its range sphere outside the frustum of
 * every view, or smaller than BOX_LIGHTS_CULL_PIXELS on all of them, changes no pixel.
 *
 * Reflection probes, planar reflections and bakes render the lights of a scene from elsewhere,
 * so a world with probes or planar reflections and a scene that bakes keep every light.
 */

#include "lights.h"
#include <math.h>

/** Projected diameter in pixels below which the range sphere of a light is culled */
#define BOX_LIGHTS_CULL_PIXELS (2.0f)

/** Aspect ratio a camera that follows its surface is assumed to have at most */
#define BOX_LIGHTS_CULL_ASPECT (4.0f)

/** Get the largest height in pixels a view renders at, of every surface.
 *
 * @param world The world
 * @return The height, 0 when no surface knows its size
 */
static int32_t box_lights_cull_height(
    const ecs_world_t *world)
{
    int32_t result = 0, i;
    if (!ecs_id(SurfaceStatus)) {
        goto done;
    }

    ecs_iter_t it = ecs_each_id(world, ecs_id(SurfaceStatus));
    while (ecs_each_next(&it)) {
        const SurfaceStatus *status = ecs_field(&it, SurfaceStatus, 0);
        for (i = 0; i < it.count; i ++) {
            result = glm_max(result, status[i].render_height);
        }
    }
done:
    return result;
}

/** Write the cull view of a view: the planes of its frustum, the position of its camera and its
 * pixels per unit.
 *
 * @param v The view
 * @param height Largest height in pixels a view renders at, 0 for unknown
 * @param out Receives the cull view, its scene set by the caller
 */
static void box_lights_cull_view(
    const box_render_view_t *v,
    int32_t height,
    box_light_cull_view_t *out)
{
    const Camera *c = &v->camera_data;
    bool sides = !c->ortho || c->ortho_scale <= 0;
    float aspect = c->aspect_ratio > 0 ? c->aspect_ratio : BOX_LIGHTS_CULL_ASPECT;
    mat4 proj, m;
    int32_t i, j;
    box_camera_projection(c, aspect, 1.0f, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), m);
    out->plane_count = 0;
    for (i = sides ? 0 : 4; i < 6; i ++) {
        vec4 plane;
        for (j = 0; j < 4; j ++) {
            if (i < 4) {
                plane[j] = m[j][3] + ((i & 1) ? -1.0f : 1.0f) * m[j][i / 2];
            } else {
                plane[j] = i == 4 ? m[j][3] - m[j][2] : m[j][2];
            }
        }

        float len = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
        if (len > 1e-12f) {
            glm_vec4_scale(plane, 1.0f / len, out->planes[out->plane_count ++]);
        }
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), out->eye);
    out->pixels = 0.0f;
    if (!c->ortho && height > 0 && c->fov > 0) {
        out->pixels = (float)height / (2.0f * tanf(glm_rad(c->fov) * 0.5f));
    }
}

void box_lights_cull_views(
    ecs_world_t *world,
    box_render_lights_t *l,
    const box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&p->views), height = 0;
    ecs_vec_clear(&l->cull_views);
    l->cull = !(ecs_id(ReflectionProbe) && ecs_count_id(world, ecs_id(ReflectionProbe))) &&
        !(ecs_id(PlanarReflection) && ecs_count_id(world, ecs_id(PlanarReflection)));
    if (l->cull && count) {
        height = box_lights_cull_height(world);
    }

    for (i = 0; l->cull && i < count; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        const View *view = v->view && ecs_is_alive(world, v->view)
            ? ecs_get(world, v->view, View)
            : NULL;
        box_light_cull_view_t *out = ecs_vec_append_t(NULL, &l->cull_views,
            box_light_cull_view_t);
        out->scene = view && view->lighting.scene ? view->lighting.scene : v->scene;
        box_lights_cull_view(v, height, out);
    }
}

/** Check whether a cull view can see a sphere.
 *
 * @param cv The cull view
 * @param center Center of the sphere
 * @param radius Radius of the sphere
 * @return Whether the sphere may be in its frustum and is not too small
 */
static bool box_lights_cull_sees(
    const box_light_cull_view_t *cv,
    const float *center,
    float radius)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < cv->plane_count && result; i ++) {
        const float *plane = cv->planes[i];
        result = plane[0] * center[0] + plane[1] * center[1] + plane[2] * center[2] +
            plane[3] >= -radius;
    }

    if (result && cv->pixels > 0) {
        float dist = glm_vec3_distance(ECS_CONST_CAST(float*, cv->eye),
            ECS_CONST_CAST(float*, center));
        result = dist <= radius ||
            2.0f * radius * cv->pixels >= BOX_LIGHTS_CULL_PIXELS * (dist - radius);
    }

    return result;
}

bool box_lights_culled(
    box_render_lights_t *l,
    const box_light_scene_t *s,
    const WorldTransform *transform,
    float range)
{
    bool viewed = false, seen = false;
    int32_t i, count = ecs_vec_count(&l->cull_views);
    if (!l->cull || s->baking || !transform) {
        goto done;
    }

    vec3 center;
    box_dvec3_rel(transform->position, s->origin, center);
    float radius = range * box_transform_world_scale(transform);
    for (i = 0; i < count && !seen; i ++) {
        const box_light_cull_view_t *cv = ecs_vec_get_t(
            &l->cull_views, box_light_cull_view_t, i);
        if (cv->scene == s->scene) {
            viewed = true;
            seen = box_lights_cull_sees(cv, center, radius);
        }
    }
done:
    return viewed && !seen;
}
