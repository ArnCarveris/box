/**
 * @file surface/schedule.c
 * @brief Renders a surface at most once per interval and only while what it names is seen.
 */

#include "schedule.h"
#include "../portal/portal.h"

/* Widest aspect ratio a view is checked at: the sight test stays generous, never too narrow */
#define BOX_SURFACE_SCHEDULE_ASPECT (2.0f)

/** Check whether a view sees a sphere: in the frustum of its camera, within range of its eye,
 * and in an area its portal traversal saw when it culls by portals.
 *
 * @param p The packet of the frame
 * @param v The view
 * @param center Center of the sphere in world space
 * @param radius Radius of the sphere
 * @param range Farthest distance of the eye from the center, 0 for any
 * @return Whether the view sees the sphere
 */
static bool box_surface_schedule_view_sees(
    const box_render_packet_t *p,
    const box_render_view_t *v,
    const vec3 center,
    float radius,
    float range)
{
    const Camera *c = &v->camera_data;
    vec3 eye, bounds[2];
    vec4 planes[6];
    mat4 proj, view_proj;
    bool result = false;

    glm_vec3_copy(ECS_CONST_CAST(float*, v->camera_transform[3]), eye);
    if (range > 0 && glm_vec3_distance(eye, ECS_CONST_CAST(float*, center)) > range + radius) {
        goto done;
    }

    box_camera_projection(c, glm_max(c->aspect_ratio, BOX_SURFACE_SCHEDULE_ASPECT), 1.0f, proj);
    glm_mat4_mul(proj, ECS_CONST_CAST(vec4*, v->view_matrix), view_proj);
    glm_frustum_planes(view_proj, planes);

    glm_vec3_subs(ECS_CONST_CAST(float*, center), radius, bounds[0]);
    glm_vec3_adds(ECS_CONST_CAST(float*, center), radius, bounds[1]);
    if (!glm_aabb_frustum(bounds, planes)) {
        goto done;
    }

    if (v->portal_count > 0) {
        const vec4 *block = ecs_vec_get_t(&p->portals, vec4, v->portal_first);

        result = box_portal_block_sees(block, bounds[0], bounds[1]);
    } else {
        result = true;
    }

done:
    return result;
}

/** Check whether a view is one of a texture surface: its parent is the view_root of a surface
 * with a texture.
 *
 * @param world The world
 * @param v The view
 * @return Whether a texture surface renders the view
 */
static bool box_surface_schedule_texture_view(
    const ecs_world_t *world,
    const box_render_view_t *v)
{
    bool result = false;
    ecs_iter_t it = ecs_each(ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world)), Surface);

    while (ecs_each_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        int32_t i;

        for (i = 0; i < it.count && !result; i ++) {
            result = s[i].texture && s[i].view_root && v->parent == s[i].view_root;
        }
    }

    return result;
}

/** Check whether a view of a window, a perspective view that no texture surface renders, sees
 * the sphere of a schedule.
 *
 * @param world The world
 * @param p The packet of the frame
 * @param schedule The schedule
 * @return Whether one of those views sees it
 */
static bool box_surface_schedule_seen(
    const ecs_world_t *world,
    const box_render_packet_t *p,
    const SurfaceSchedule *schedule)
{
    const WorldTransform *transform = ecs_get(world, schedule->seen, WorldTransform);
    int32_t i, count = ecs_vec_count(&p->views);
    bool result = false;
    vec3 center;

    if (!transform) {
        goto done;
    }

    glm_vec3_copy(ECS_CONST_CAST(float*, transform->m[3]), center);

    for (i = 0; i < count && !result; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);

        if (v->target || v->camera_data.ortho || box_surface_schedule_texture_view(world, v)) {
            continue;
        }

        result = box_surface_schedule_view_sees(p, v, center, schedule->radius, schedule->range);
    }

done:
    return result;
}

bool box_surface_schedule_due(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_render_packet_t *p,
    float delta_time,
    float *wait)
{
    const SurfaceSchedule *schedule = ecs_get(world, e, SurfaceSchedule);
    const SurfaceScheduleStatus *current = ecs_get(world, e, SurfaceScheduleStatus);
    SurfaceScheduleStatus status = { 0 };
    bool result = true;

    if (!schedule) {
        goto done;
    }

    if (current) {
        status = *current;
    }

    status.seen = !schedule->seen || box_surface_schedule_seen(world, p, schedule);

    *wait -= delta_time;
    if (status.renders == 0) {
        result = true;
    } else if (schedule->paused || !status.seen) {
        result = false;
    } else {
        result = *wait <= 0;
    }

    if (result) {
        *wait = schedule->interval;
        status.renders ++;
    }

    if (!current || current->seen != status.seen || current->renders != status.renders) {
        ecs_set_ptr(world, e, SurfaceScheduleStatus, &status);
    }

done:
    return result;
}
