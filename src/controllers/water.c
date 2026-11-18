/**
 * @file controllers/water.c
 * @brief The surface of the water a body is in: shapes with the Water material.
 */

#include "controllers.h"
#include "../spatial/spatial.h"

/** A ray up from the feet to the sky, which a solid shape blocks */
typedef struct box_water_ray_t {
    const ecs_world_t *world;          /**< The world */
    const box_controller_body_t *body; /**< The body */
    vec3 origin;                       /**< Origin of the ray */
} box_water_ray_t;

/** Accept a solid shape the ray up to the sky hits.
 *
 * @param ctx The ray
 * @param e The shape
 * @param distance Distance at which the ray enters the bounds, moved to the shape
 * @return true when the ray hits a solid shape that is not the body's
 */
static bool box_water_filter(
    void *ctx,
    ecs_entity_t e,
    float *distance)
{
    const box_water_ray_t *r = ctx;
    vec3 up = { 0, 1, 0 };
    ecs_entity_t p = e;
    bool own = false;
    while (p && !own) {
        own = p == r->body->entity;
        p = ecs_get_parent(r->world, p);
    }

    return !own && box_collider_solid(r->world, e) &&
        box_collider_cast(r->world, e, r->origin, up, FLT_MAX, distance, NULL);
}

/** Find the highest water surface a ray down onto a body's feet enters.
 *
 * @param world The world
 * @param body The body
 * @param feet The bottom of the capsule
 * @param reach How far above the feet the surface may lie
 * @param below How far below the feet the surface may lie
 * @param level Receives the height of the surface
 * @return true when a Water shape lies within reach above or below the feet
 */
static bool box_water_surface(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet,
    float reach,
    float below,
    float *level)
{
    vec3 min = { feet->x, feet->y - below, feet->z };
    vec3 max = { feet->x, feet->y + reach, feet->z };
    vec3 origin = { feet->x, feet->y + reach, feet->z };
    vec3 down = { 0, -1, 0 };
    ecs_vec_t shapes;
    int32_t i, count;
    bool result = false;
    ecs_vec_init_t(NULL, &shapes, ecs_entity_t, 0);
    count = box_controller_near(world, body->scene, min, max, &shapes);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&shapes, ecs_entity_t, i);
        float t = 0;
        if (box_collider_solid(world, e) ||
            !box_collider_cast(world, e, origin, down, reach + below, &t, NULL))
        {
            continue;
        }

        if (!result || origin[1] - t > *level) {
            *level = origin[1] - t;
        }

        result = true;
    }

    ecs_vec_fini_t(NULL, &shapes, ecs_entity_t);

    return result;
}

bool box_controller_water(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet,
    float reach,
    float below,
    float *level)
{
    float surface = 0;
    box_water_ray_t ctx = {
        .world = world,
        .body = body,
        .origin = { feet->x, feet->y + body->radius, feet->z }
    };
    box_spatial_ray_t ray = {
        .scene = body->scene,
        .origin = { feet->x, feet->y + body->radius, feet->z },
        .dir = { 0, 1, 0 },
        .filter = box_water_filter,
        .ctx = &ctx,
        .shapes = true
    };
    box_spatial_hit_t hit;
    bool result = box_water_surface(world, body, feet, reach, below, &surface) &&
        surface > feet->y - below;
    if (result) {
        result = !box_spatial_ray(world, &ray, &hit);
    }

    if (result) {
        *level = surface;
    }

    return result;
}
