/**
 * @file movement/movement.c
 * @brief Integrates Velocity3 and AngularVelocity3 in OnUpdate.
 */

#define BOX_MOVEMENT_IMPL
#include <box.h>

/** Add velocity times delta time to positions.
 *
 * @param it Iterator over moving entities
 */
static void BoxIntegrateVelocity(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    Velocity3 *v = ecs_field(it, Velocity3, 1);
    float dt = it->delta_time;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        p[i].x += v[i].x * dt;
        p[i].y += v[i].y * dt;
        p[i].z += v[i].z * dt;
    }
}

/** Add angular velocity times delta time to rotations.
 *
 * @param it Iterator over spinning entities
 */
static void BoxIntegrateAngularVelocity(
    ecs_iter_t *it)
{
    Rotation *r = ecs_field(it, Rotation, 0);
    AngularVelocity3 *v = ecs_field(it, AngularVelocity3, 1);
    float dt = it->delta_time;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        r[i].x += v[i].x * dt;
        r[i].y += v[i].y * dt;
        r[i].z += v[i].z * dt;
    }
}

void BoxMovementImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMovement);
    ECS_IMPORT(world, BoxTransform);

    ECS_META_COMPONENT(world, Velocity3);
    ECS_META_COMPONENT(world, AngularVelocity3);

    ecs_add_pair(world, ecs_id(Velocity3), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(Velocity3), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(AngularVelocity3), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(AngularVelocity3), EcsWith, DynamicTransform);

    ECS_SYSTEM(world, BoxIntegrateVelocity, EcsOnUpdate, [inout] Position, [in] Velocity3);
    ECS_SYSTEM(world, BoxIntegrateAngularVelocity, EcsOnUpdate,
        [inout] Rotation, [in] AngularVelocity3);
}
