/**
 * @file transform/lookat.c
 * @brief System that turns a LookAt target into a Rotation.
 *
 * The rotation is the Rx * Ry * Rz decomposition of the matrix that points the
 * local +Z axis at the target with +Y as the world up.
 */

#include <box.h>
#include "transform_internal.h"

/** Point the local +Z axis of an entity at its LookAt target.
 *
 * @param it Iterator over the entities with a target
 */
static void BoxTransformLookAtUpdate(
    ecs_iter_t *it)
{
    const LookAt *l = ecs_field(it, LookAt, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    const WorldTransform *w = ecs_field(it, WorldTransform, 2);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        vec3 pos = mat4_get_translation(&w[i].m);
        vec3 dir = vec3_sub(vec3_new(l[i].x, l[i].y, l[i].z), pos);
        mat4 facing = mat4_facing(dir, vec3_new(0, 1, 0));
        vec3 e = mat4_to_euler_xyz(&facing);
        r[i] = (Rotation){ e.x, e.y, e.z };
        ecs_modified(it->world, it->entities[i], Rotation);
    }
}

void box_transform_lookat_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxTransformLookAtUpdate" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(LookAt), .inout = EcsIn },
            { .id = ecs_id(Rotation), .inout = EcsOut },
            { .id = ecs_id(WorldTransform), .inout = EcsIn }
        },
        .callback = BoxTransformLookAtUpdate
    });
}
