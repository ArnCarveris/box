/**
 * @file spline/follow.c
 * @brief Moves the entities that hold a FollowPath along their curve in OnUpdate.
 *
 * A follower keeps its travelled distance, so a speed change applies from there.
 * The distance is folded by mode before sampling: loop wraps, ping pong reflects
 * and turns around, once clamps to the curve ends.
 */

#include "spline_internal.h"
#include <math.h>

/** Fold the travelled distance of a follower into the curve.
 *
 * @param path The follower
 * @param length Arc length of the curve
 * @param forward Receives false when a ping pong travels back
 * @return The distance along the curve to sample
 */
static float box_follow_fold(
    const FollowPath *path,
    float length,
    bool *forward)
{
    float result = path->offset + path->travelled;
    *forward = true;
    if (path->mode == PathPingPong) {
        float period = length * 2;
        float folded = result - floorf(result / period) * period;

        if (folded > length) {
            folded = period - folded;
            *forward = false;
        }

        result = folded;
    } else if (path->mode == PathLoop) {
        result -= floorf(result / length) * length;
    } else {
        result = fminf(fmaxf(result, 0.0f), length);
    }

    return result;
}

/** Advance one follower and write the pose it reaches.
 *
 * @param world The world
 * @param path The follower
 * @param position The position to write
 * @param rotation The rotation to write, NULL when the entity holds none
 * @param dt Seconds since the previous frame
 */
static void box_follow_step(
    const ecs_world_t *world,
    FollowPath *path,
    Position *position,
    Rotation *rotation,
    float dt)
{
    const SplineTable *table = NULL;
    const Spline *curve = box_spline_get(world, path->spline, &table);
    float length = table ? table->length : 0;
    box_spline_sample_t sample;
    bool forward = true;
    if (!curve || length <= 0) {
        goto done;
    }

    path->travelled += path->speed * dt;

    float distance = box_follow_fold(path, length, &forward);
    box_spline_eval(curve, box_spline_table_param(table, distance), &sample);
    position->x = sample.position.x;
    position->y = sample.position.y;
    position->z = sample.position.z;

    if (path->orient && rotation) {
        vec3 heading = forward ? sample.tangent : vec3_scale(sample.tangent, -1);
        mat4 facing = mat4_facing(heading, sample.normal);
        vec3 euler = mat4_to_euler_xyz(&facing);

        rotation->x = euler.x;
        rotation->y = euler.y;
        rotation->z = euler.z;
    }

done:
    return;
}

/** Move every follower of the frame along its curve.
 *
 * @param it Iterator over the followers
 */
static void BoxFollowPath(
    ecs_iter_t *it)
{
    FollowPath *path = ecs_field(it, FollowPath, 0);
    Position *position = ecs_field(it, Position, 1);
    Rotation *rotation = ecs_field(it, Rotation, 2);
    float dt = it->delta_time;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_follow_step(it->world, &path[i], &position[i],
            rotation ? &rotation[i] : NULL, dt);
    }
}

void box_spline_follow_init(
    ecs_world_t *world)
{
    ECS_SYSTEM(world, BoxFollowPath, EcsOnUpdate,
        [inout] FollowPath, [out] Position, [out] ?Rotation);
}
