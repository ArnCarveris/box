/**
 * @file transform/transform.c
 * @brief Registers transform module.
 */

#define BOX_TRANSFORM_IMPL
#include "transform.h"

ECS_TAG_DECLARE(DynamicTransform);
ECS_TAG_DECLARE(ManualTransform);

ECS_CTOR(Scale, ptr, {
    ptr->x = 1;
    ptr->y = 1;
    ptr->z = 1;
})

ECS_CTOR(WorldTransformVersion, ptr, {
    ptr->version = 0;
})

void transformSingle(
    const WorldTransform *t_parent,
    WorldTransform *t,
    const Position *p,
    const Rotation *r,
    const Scale *s)
{
    ecs_assert(p != NULL, ECS_INTERNAL_ERROR, "missing position");

    mat4 local;
    vec4 *m = t_parent ? local : t->m;

    if (r) {
        glm_euler_yxz((vec3){ r->x, r->y, r->z }, m);
    } else {
        glm_mat4_identity(m);
    }

    if (s) {
        glm_vec4_scale(m[0], s->x, m[0]);
        glm_vec4_scale(m[1], s->y, m[1]);
        glm_vec4_scale(m[2], s->z, m[2]);
    }

    m[3][0] = p->x;
    m[3][1] = p->y;
    m[3][2] = p->z;

    if (t_parent) {
        glm_mul((vec4*)t_parent->m, local, t->m);
    }
}

const WorldTransform* transformGetParentWorldTransform(
    const ecs_world_t *world,
    ecs_entity_t parent)
{
    const WorldTransform *t = ecs_get(world, parent, WorldTransform);
    while (!t && parent) {
        parent = ecs_get_parent(world, parent);
        if (parent) {
            t = ecs_get(world, parent, WorldTransform);
        }
    }

    return t;
}

bool transformDeleting(
    const ecs_iter_t *it)
{
    return it->event == EcsOnRemove && (!it->other_table ||
        (!ecs_table_get_type(it->other_table)->count && it->table &&
            ecs_table_has_flags(it->table, EcsTableMarkedForDelete)));
}

void BoxTransformImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTransform);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Position);
    ECS_META_COMPONENT(world, Rotation);
    ECS_META_COMPONENT(world, Scale);
    ECS_META_COMPONENT(world, WorldTransform);
    ECS_META_COMPONENT(world, WorldTransformVersion);

    ECS_TAG_DEFINE(world, DynamicTransform);
    ECS_TAG_DEFINE(world, ManualTransform);

    ecs_add_pair(world, ecs_id(WorldTransform), EcsWith, ecs_id(WorldTransformVersion));
    ecs_add_pair(world, ecs_id(Position), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Rotation), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Rotation), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(Scale), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, ecs_id(Scale), EcsWith, ecs_id(Position));

    ecs_set_hooks(world, Scale, { .ctor = ecs_ctor(Scale) });
    ecs_set_hooks(world, WorldTransformVersion, {
        .ctor = ecs_ctor(WorldTransformVersion)
    });

    TransformDynamicImport(world);
    TransformStaticImport(world);
}
