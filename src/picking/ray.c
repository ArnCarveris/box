/**
 * @file picking/ray.c
 * @brief Builds the world ray of a screen position and resolves what it hits.
 *
 * The ray of a view is built from the world transform of its camera and from
 * the projection that camera renders with, so a pick follows the image the
 * renderer produced for that view.
 */

#include "picking_internal.h"
#include <stdlib.h>
#include <string.h>

/** Degrees in one radian, the unit the camera field of view is given in */
#define BOX_PICK_RADIANS (3.14159265f / 180.0f)

/** Field of view a camera without one renders with */
#define BOX_PICK_FOV (60.0f)

/** Visible height an orthographic camera without one renders with */
#define BOX_PICK_ORTHO_HEIGHT (10.0f)

/** Order two views so the one that renders last picks first.
 *
 * @param a The first view
 * @param b The second view
 * @return Negative when a picks first, positive when b does
 */
static int box_pick_view_compare(
    const void *a,
    const void *b)
{
    const box_pick_view_t *x = a;
    const box_pick_view_t *y = b;
    int result = (x->priority < y->priority) - (x->priority > y->priority);
    if (!result) {
        result = (x->entity < y->entity) - (x->entity > y->entity);
    }

    return result;
}

/** Find the camera a view picks with.
 *
 * @param world The world
 * @param state The module state
 * @param view The view entity, 0 for the camera of a world without views
 * @return The camera entity, or 0 when there is none
 */
static ecs_entity_t box_pick_camera(
    const ecs_world_t *world,
    PickState *state,
    ecs_entity_t view)
{
    ecs_entity_t result = 0;
    if (view) {
        const View *v = ecs_get(world, view, View);
        result = v ? v->camera : 0;
    } else if (state && state->cameras) {
        ecs_iter_t it = ecs_query_iter(world, state->cameras);
        if (ecs_query_next(&it)) {
            result = it.entities[0];
            ecs_iter_fini(&it);
        }
    }

    if (result && !ecs_has(world, result, WorldTransform)) {
        result = 0;
    }

    return result;
}

/** Width over height of the image a view renders into.
 *
 * @param world The world
 * @param view The view entity, 0 for the surface
 * @return The aspect ratio, 1 when nothing gives a size
 */
static float box_pick_aspect(
    const ecs_world_t *world,
    ecs_entity_t view)
{
    float width = 0, height = 0;
    const View *v = view ? ecs_get(world, view, View) : NULL;
    const RenderTarget *target = v && v->target ?
        ecs_get(world, v->target, RenderTarget) : NULL;
    if (target) {
        width = (float)target->width;
        height = (float)target->height;
    } else {
        const Surface *s = ecs_singleton_get(world, Surface);
        if (s) {
            width = (float)(s->point_width ? s->point_width : s->width);
            height = (float)(s->point_height ? s->point_height : s->height);
        }
    }

    return (width > 0 && height > 0) ? width / height : 1.0f;
}

int32_t box_picking_views(
    const ecs_world_t *world,
    PickState *state,
    box_pick_view_t *out,
    int32_t max)
{
    int32_t count = 0;
    ecs_iter_t it = ecs_query_iter(world, state->views);
    while (ecs_query_next(&it)) {
        const View *view = ecs_field(&it, View, 0);
        int32_t i;
        for (i = 0; i < it.count && count < max; i ++) {
            if (!view[i].enabled || view[i].target) {
                continue;
            }

            if (!box_pick_camera(world, state, it.entities[i])) {
                continue;
            }

            out[count].entity = it.entities[i];
            out[count].priority = view[i].priority;
            count ++;
        }
    }

    if (count > 1) {
        qsort(out, (size_t)count, sizeof(box_pick_view_t), box_pick_view_compare);
    }

    if (!count && box_pick_camera(world, state, 0)) {
        out[0].entity = 0;
        out[0].priority = 0;
        count = 1;
    }

    return count;
}

bool box_picking_ray(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec2 cursor,
    vec3 *origin,
    vec3 *dir)
{
    PickState *state = box_picking_state(world);
    ecs_entity_t camera = state ? box_pick_camera(world, state, view) : 0;
    bool result = false;
    if (!camera) {
        goto done;
    }

    const WorldTransform *t = ecs_get(world, camera, WorldTransform);
    const Camera *c = ecs_get(world, camera, Camera);
    vec3 right = vec3_normalize(mat4_transform_dir(&t->m, vec3_new(1, 0, 0)));
    vec3 up = vec3_normalize(mat4_transform_dir(&t->m, vec3_new(0, 1, 0)));
    vec3 forward = vec3_normalize(mat4_transform_dir(&t->m, vec3_new(0, 0, 1)));
    vec3 eye = mat4_get_translation(&t->m);
    float aspect = c && c->aspect_ratio > 0 ?
        c->aspect_ratio : box_pick_aspect(world, view);

    if (c && c->orthographic) {
        float h = c->ortho_height > 0 ? c->ortho_height : BOX_PICK_ORTHO_HEIGHT;
        eye = vec3_add(eye, vec3_scale(right, cursor.x * h * aspect * 0.5f));
        eye = vec3_add(eye, vec3_scale(up, cursor.y * h * 0.5f));
        *origin = eye;
        *dir = forward;
    } else {
        float fov = c && c->fov > 0 ? c->fov : BOX_PICK_FOV;
        float tan_half = tanf(fov * BOX_PICK_RADIANS * 0.5f);
        vec3 v = vec3_add(forward, vec3_scale(right, cursor.x * aspect * tan_half));
        v = vec3_add(v, vec3_scale(up, cursor.y * tan_half));
        *origin = eye;
        *dir = vec3_normalize(v);
    }

    result = true;
done:
    return result;
}

vec3 box_picking_normal(
    const Collider *c,
    vec3 point)
{
    vec3 d = vec3_sub(point, c->center);
    vec3 local = vec3_new(
        vec3_dot(d, c->axis_x), vec3_dot(d, c->axis_y), vec3_dot(d, c->axis_z));
    vec3 result = vec3_new(0, 1, 0);

    if (c->kind == ColliderSphere) {
        result = vec3_normalize(d);
    } else if (c->kind == ColliderCapsule) {
        float y = fmaxf(fminf(local.y, c->half.y), -c->half.y);
        vec3 axis = vec3_scale(c->axis_y, y);
        result = vec3_normalize(vec3_sub(d, axis));
    } else {
        float half[3] = { c->half.x, c->half.y, c->half.z };
        float value[3] = { local.x, local.y, local.z };
        vec3 axes[3] = { c->axis_x, c->axis_y, c->axis_z };
        float best = -1.0f;
        int32_t i, face = 1;
        for (i = 0; i < 3; i ++) {
            float ratio = half[i] > 0 ? fabsf(value[i]) / half[i] : 0;
            if (ratio > best) {
                best = ratio;
                face = i;
            }
        }

        result = vec3_scale(axes[face], value[face] < 0 ? -1.0f : 1.0f);
    }

    if (!(vec3_length(result) > 0)) {
        result = vec3_new(0, 1, 0);
    }

    return result;
}

ecs_entity_t box_picking_target(
    const ecs_world_t *world,
    ecs_entity_t collider)
{
    ecs_entity_t result = 0;
    ecs_entity_t e = collider;
    while (e && ecs_is_alive(world, e)) {
        if (ecs_has_id(world, e, Pickable) || ecs_has(world, e, RootBounds)) {
            result = e;
            break;
        }

        e = ecs_get_parent(world, e);
    }

    if (!result) {
        result = collider;
    }

    return result;
}

bool box_picking_pick(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec2 cursor,
    PickHit *out)
{
    vec3 origin, dir;
    bool result = false;
    memset(out, 0, sizeof(PickHit));
    out->cursor = cursor;
    out->view = view;
    if (!box_picking_ray(world, view, cursor, &origin, &dir)) {
        goto done;
    }

    const PickSettings *settings = ecs_singleton_get(world, PickSettings);
    float max_dist = settings && settings->max_distance > 0 ?
        settings->max_distance : BOX_PICK_DISTANCE;
    box_ray_hit_t hit;
    if (!box_collision_ray(world, origin, dir, max_dist, &hit)) {
        goto done;
    }

    const Collider *c = ecs_get(world, hit.entity, Collider);
    out->collider = hit.entity;
    out->entity = box_picking_target(world, hit.entity);
    out->point = hit.point;
    out->distance = hit.distance;
    out->normal = c ? box_picking_normal(c, hit.point) : vec3_scale(dir, -1.0f);
    result = true;
done:
    return result;
}
