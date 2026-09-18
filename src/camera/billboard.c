/**
 * @file camera/billboard.c
 * @brief Rotates Billboard entities so their local +Z faces the first camera.
 */

#include <box.h>
#include "camera_internal.h"

/** Face every billboard towards the camera position.
 *
 * @param it Iterator over billboards
 */
static void BoxBillboard(
    ecs_iter_t *it)
{
    ecs_query_t *cameras = it->ctx;
    ecs_iter_t cit = ecs_query_iter(it->world, cameras);
    if (!ecs_query_next(&cit)) {
        goto done;
    }

    WorldTransform *camera = ecs_field(&cit, WorldTransform, 0);
    vec3 eye = mat4_get_translation(&camera->m);
    ecs_iter_fini(&cit);

    WorldTransform *transform = ecs_field(it, WorldTransform, 0);
    Rotation *rotation = ecs_field(it, Rotation, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        vec3 pos = mat4_get_translation(&transform[i].m);
        vec3 dir = vec3_sub(eye, pos);
        if (vec3_length(dir) > 0) {
            mat4 facing = mat4_facing(dir, vec3_new(0, 1, 0));
            vec3 e = mat4_to_euler_xyz(&facing);
            rotation[i] = (Rotation){ e.x, e.y, e.z };
            ecs_modified(it->world, it->entities[i], Rotation);
        }
    }
done:
    return;
}

void box_billboard_init(
    ecs_world_t *world)
{
    ecs_query_t *cameras = ecs_query(world, {
        .terms = {
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(Camera), .inout = EcsInOutNone }
        }
    });

    ecs_entity_t system = ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxBillboard" }),
        .query.terms = {
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(Rotation), .inout = EcsInOut },
            { .id = Billboard, .inout = EcsInOutNone }
        },
        .callback = BoxBillboard,
        .ctx = cameras
    });
    ecs_add_pair(world, system, EcsDependsOn, EcsPostUpdate);
}
