
#include <box.h>

void transformDynamicChildOf(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth);

void transformDynamicParent(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth);

void transformSingle(
    const WorldTransform *t_parent,
    WorldTransform *t,
    const Position *p,
    const Rotation *r,
    const Scale *s);

const WorldTransform* transformGetParentWorldTransform(
    const ecs_world_t *world,
    ecs_entity_t parent);

void TransformDynamicImport(
    ecs_world_t *world);

void TransformStaticImport(
    ecs_world_t *world);