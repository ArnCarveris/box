/**
 * @file collision/collision.c
 * @brief Registers the box.collision components and runs the per frame flush.
 */

#define BOX_COLLISION_IMPL
#include "collision_internal.h"
#include "../render/render_internal.h"

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
    box_index_init(&state->index);
    ecs_map_init(&state->dirty, NULL);
    ecs_map_init(&state->colliders, NULL);
    ecs_vec_init_t(NULL, &state->pass, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->resolve, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->instances, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->debug_hits, ecs_entity_t, 0);

    return state;
}

void box_collision_state_free(
    box_collision_state_t *state)
{
    box_index_fini(&state->index);
    ecs_map_fini(&state->dirty);
    ecs_map_fini(&state->colliders);
    ecs_vec_fini_t(NULL, &state->pass, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->resolve, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->instances, ecs_entity_t);
    ecs_vec_fini_t(NULL, &state->debug_hits, ecs_entity_t);
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

    const CollisionSettings *settings = ecs_singleton_get(world, CollisionSettings);
    if (settings) {
        state->index.margin = settings->margin;
        state->index.enabled = settings->enabled;
        state->index.dynamic = settings->dynamic;
    }

    box_collision_roots_promote(world, state);
    box_collision_shapes_init(world, state);
    box_collision_hidden_init(world, state);
    box_collision_colliders_flush(world, state);
    box_collision_roots_flush(world, state);
    box_index_movers(world, state);
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
    ECS_META_COMPONENT(world, CollisionDebug);
    ECS_META_COMPONENT(world, CollisionSettings);
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

    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f,
        .dynamic = true,
        .enabled = true,
        .debug = DebugNone
    });

    state->index.query = ecs_query(world, {
        .terms = {
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = DynamicTransform, .src.id = EcsSelf|EcsUp,
                .trav = EcsChildOf, .inout = EcsInOutNone },
            { .id = NoCollider, .oper = EcsNot, .inout = EcsInOutNone }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    box_collision_roots_init(world, state);
    box_collision_colliders_init(world, state);

    box_render_lod_roots(world, ecs_id(RootBounds));

    // ecs_system(world, {
    //     .entity = ecs_entity(world, { .name = "BoxCollisionUpdate" }),
    //     .phase = EcsOnStore,
    //     .query.terms = {{ .id = ecs_id(CollisionState) }},
    //     .callback = BoxCollisionUpdate,
    //     .ctx = state,
    //     .immediate = true
    // });
}
