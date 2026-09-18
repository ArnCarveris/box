/**
 * @file transform/transform.c
 * @brief Registers the box.transform components, tags and relationships.
 */

#define BOX_TRANSFORM_IMPL
#include <box.h>
#include "transform_internal.h"

ECS_TAG_DECLARE(DynamicTransform);
ECS_TAG_DECLARE(ManualTransform);
ECS_TAG_DECLARE(RigidSubtree);

/** Initialize a scale to one.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti The type info
 */
static void box_scale_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;

    Scale *s = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        s[i].x = 1.0f;
        s[i].y = 1.0f;
        s[i].z = 1.0f;
    }
}

mat4 box_transform_compose(
    const Position *p,
    const Rotation *r,
    const Scale *s,
    const Orientation *o)
{
    vec3 t = vec3_new(0, 0, 0);
    vec3 scale = vec3_new(1, 1, 1);
    mat4 rot = mat4_identity();
    if (p) {
        t = vec3_new(p->x, p->y, p->z);
    }

    if (s) {
        scale = vec3_new(s->x, s->y, s->z);
    }

    if (o) {
        rot = mat4_rotation_yxz(o->pitch, o->yaw, o->roll);
    } else if (r) {
        rot = mat4_rotation_xyz(vec3_new(r->x, r->y, r->z));
    }

    return mat4_trs(t, &rot, scale);
}

void BoxTransformImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTransform);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Position);
    ECS_META_COMPONENT(world, Rotation);
    ECS_META_COMPONENT(world, Scale);
    ECS_META_COMPONENT(world, Orientation);
    ECS_META_COMPONENT(world, LookAt);
    ECS_META_COMPONENT(world, WorldTransform);
    ECS_META_COMPONENT(world, AABB);

    ECS_TAG_DEFINE(world, DynamicTransform);
    ECS_TAG_DEFINE(world, ManualTransform);
    ECS_TAG_DEFINE(world, RigidSubtree);

    ecs_set_hooks(world, Scale, { .ctor = box_scale_ctor });

    ecs_add_pair(world, ecs_id(WorldTransform), EcsWith, ecs_id(AABB));
    ecs_add_pair(world, ecs_id(Position), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Rotation), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Rotation), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(Scale), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Scale), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(Orientation), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Orientation), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(LookAt), EcsWith, ecs_id(Rotation));

    box_transform_static_init(world);
    box_transform_lookat_init(world);
    box_transform_dynamic_init(world);
}
