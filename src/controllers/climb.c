/**
 * @file controllers/climb.c
 * @brief Climbable shapes within reach of a body, the ladders a first person controller climbs.
 */

#include "controllers.h"
#include <math.h>

/** How far below the bottom of a Climbable shape the feet may be and still take hold */
#define BOX_CLIMB_BELOW (0.3f)

/** How far above the top of a Climbable shape the feet may be and still hold on */
#define BOX_CLIMB_ABOVE (0.2f)

/** Test whether a shape is Climbable, by itself or by an ancestor.
 *
 * @param world The world
 * @param e The shape
 * @return true when the shape or an ancestor has Climbable, also through IsA
 */
static bool box_climb_climbable(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t p = e;
    bool result = false;
    while (p && !result) {
        result = ecs_has(world, p, Climbable);
        p = ecs_get_parent(world, p);
    }

    return result;
}

/** Get the world bounds of a collider.
 *
 * @param c The collider
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
static void box_climb_bounds(
    const box_collider_t *c,
    vec3 min,
    vec3 max)
{
    int32_t j, k;
    for (j = 0; j < 3; j ++) {
        float ext = 0;
        for (k = 0; k < 3; k ++) {
            ext += fabsf(c->axis[k][j]) * c->half[k];
        }

        min[j] = c->center[j] - ext;
        max[j] = c->center[j] + ext;
    }
}

bool box_controller_ladder(
    ecs_world_t *world,
    const box_controller_body_t *body,
    const Position *feet,
    box_controller_ladder_t *out)
{
    float reach = body->radius + BOX_CONTROLLER_REACH;
    vec3 min = { feet->x - reach, feet->y - BOX_CLIMB_ABOVE, feet->z - reach };
    vec3 max = { feet->x + reach, feet->y + BOX_CLIMB_BELOW, feet->z + reach };
    ecs_vec_t shapes;
    int32_t i, count;
    bool result = false;
    ecs_vec_init_t(NULL, &shapes, ecs_entity_t, 0);
    count = box_controller_near(world, body->scene, min, max, &shapes);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&shapes, ecs_entity_t, i);
        box_collider_t c;
        vec3 lo, hi;
        if (e == body->entity || !box_climb_climbable(world, e) ||
            !box_collider_resolve(world, e, &c))
        {
            continue;
        }

        box_climb_bounds(&c, lo, hi);
        if (feet->x < lo[0] - reach || feet->x > hi[0] + reach ||
            feet->z < lo[2] - reach || feet->z > hi[2] + reach ||
            feet->y < lo[1] - BOX_CLIMB_BELOW || feet->y > hi[1] + BOX_CLIMB_ABOVE)
        {
            continue;
        }

        if (!result || hi[1] > out->top) {
            out->shape = e;
            out->top = hi[1];
        }

        result = true;
    }

    ecs_vec_fini_t(NULL, &shapes, ecs_entity_t);

    return result;
}
