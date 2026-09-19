/**
 * @file collision/collision.c
 * @brief Registers the box.collision components and runs the per frame flush.
 */

#define BOX_COLLISION_IMPL
#include "collision_internal.h"

ECS_COMPONENT_DECLARE(CollisionState);
ECS_TAG_DECLARE(NoCollider);

/** Free the collision state of a world.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_collision_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;

    CollisionState *cs = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (cs[i].state) {
            box_collision_state_free(cs[i].state);
            cs[i].state = NULL;
        }
    }
}

box_collision_state_t* box_collision_state_new(void)
{
    box_collision_state_t *state = ecs_os_calloc_t(box_collision_state_t);
    ecs_map_init(&state->dirty, NULL);
    ecs_map_init(&state->colliders, NULL);
    ecs_vec_init_t(NULL, &state->pass, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->resolve, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->instances, ecs_entity_t, 0);

    return state;
}

void box_collision_state_free(
    box_collision_state_t *state)
{
    ecs_map_fini(&state->dirty);
    ecs_map_fini(&state->colliders);
    ecs_vec_fini_t(NULL, &state->pass, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->resolve, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->instances, ecs_entity_t);
    ecs_os_free(state);
}

/** Resolve the colliders and the root bounds that were marked this frame.
 *
 * @param it The iterator of the system
 */
static void BoxCollisionUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    box_collision_state_t *state = it->ctx;

    box_collision_shapes_init(world, state);
    box_collision_colliders_flush(world, state);
    box_collision_roots_flush(world, state);
}

void BoxCollisionImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCollision);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);

    ECS_META_COMPONENT(world, RootBounds);
    ECS_META_COMPONENT(world, ColliderKind);
    ECS_META_COMPONENT(world, SphereCollider);
    ECS_META_COMPONENT(world, BoxCollider);
    ECS_META_COMPONENT(world, CapsuleCollider);
    ECS_META_COMPONENT(world, Collider);
    ECS_TAG_DEFINE(world, NoCollider);
    ECS_COMPONENT_DEFINE(world, CollisionState);

    ecs_set_hooks(world, CollisionState, {
        .ctor = flecs_default_ctor,
        .dtor = box_collision_state_dtor
    });
    ecs_add_pair(world, ecs_id(CollisionState), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(RootBounds), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Collider), EcsOnInstantiate, EcsDontInherit);

    box_collision_state_t *state = box_collision_state_new();
    CollisionState *cs = ecs_singleton_ensure(world, CollisionState);
    cs->state = state;
    ecs_singleton_modified(world, CollisionState);

    box_collision_roots_init(world, state);
    box_collision_colliders_init(world, state);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxCollisionUpdate" }),
        .phase = EcsOnStore,
        .query.terms = {{ .id = ecs_id(CollisionState) }},
        .callback = BoxCollisionUpdate,
        .ctx = state,
        .immediate = true
    });
}
