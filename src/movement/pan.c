/**
 * @file movement/pan.c
 * @brief Pans the Rotation of Pan entities about Y on a sine of the world time.
 */

#include <math.h>
#include "pan.h"

/** Set the yaw of every sweeping entity to its angle at the total world time.
 *
 * @param it Iterator over sweeping entities
 */
static void BoxPanSystem(
    ecs_iter_t *it)
{
    Rotation *r = ecs_field(it, Rotation, 0);
    const Pan *s = ecs_field(it, Pan, 1);
    double time = ecs_get_world_info(it->world)->world_time_total;

    for (int32_t i = 0; i < it->count; i ++) {
        double angle = time * (double)s[i].speed + (double)s[i].phase;

        r[i].y = s[i].center + s[i].amplitude * (float)sin(angle);
    }
}

void box_movement_pan_import(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "PanYaw" }),
        .query.terms = {
            { .id = ecs_id(Rotation), .inout = EcsInOut },
            { .id = ecs_id(Pan), .inout = EcsIn }
        },
        .phase = EcsOnUpdate,
        .callback = BoxPanSystem
    });
}
