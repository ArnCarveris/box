/**
 * @file transition/transition.c
 * @brief Registers the box.transition module, its components and its hooks.
 *
 * Transition is a map component, which flecs has no macro for, so the type is
 * created from a map descriptor and the component handle is assigned by hand.
 * The private state owns a vector of records and carries its own lifecycle
 * hooks, and a Tint is displayed through the ActualTint the material module
 * composes, so that component is registered under the key the author writes.
 */

#define BOX_TRANSITION_IMPL
#include "transition_internal.h"

ECS_COMPONENT_DECLARE(Transition);
ECS_COMPONENT_DECLARE(TransitionType);
ECS_COMPONENT_DECLARE(TransitionState);
ECS_TAG_DECLARE(Transitionable);

/** State constructor: an entity without records in flight.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transition_state_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TransitionState *state = ptr;
    int32_t i;

    memset(ptr, 0, (size_t)count * sizeof(TransitionState));
    for (i = 0; i < count; i ++) {
        ecs_vec_init_t(NULL, &state[i].records, box_transition_t, 0);
    }
}

/** State destructor: release the records.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transition_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TransitionState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &state[i].records, box_transition_t);
    }
}

/** Deep copy the records of a state.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to copy
 * @param count Number of components
 * @param ti Type info
 */
static void box_transition_state_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TransitionState *dst = dst_ptr;
    const TransitionState *src = src_ptr;
    int32_t i;

    box_transition_state_dtor(dst_ptr, count, ti);
    for (i = 0; i < count; i ++) {
        int32_t records = ecs_vec_count(&src[i].records);

        dst[i] = src[i];
        ecs_vec_init_t(NULL, &dst[i].records, box_transition_t, records);
        ecs_vec_set_count_t(NULL, &dst[i].records, box_transition_t, records);
        if (records) {
            memcpy(ecs_vec_first(&dst[i].records), ecs_vec_first(&src[i].records),
                (size_t)records * sizeof(box_transition_t));
        }
    }
}

/** Hand the records of a state over, leaving the source empty.
 *
 * @param dst_ptr The components to overwrite
 * @param src_ptr The components to empty
 * @param count Number of components
 * @param ti Type info
 */
static void box_transition_state_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    box_transition_state_dtor(dst_ptr, count, ti);
    memcpy(dst_ptr, src_ptr, (size_t)count * sizeof(TransitionState));
    memset(src_ptr, 0, (size_t)count * sizeof(TransitionState));
}

void BoxTransitionImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTransition);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxMaterial);

    ECS_META_COMPONENT(world, TimingFunction);
    ECS_META_COMPONENT(world, TransitionParams);
    ECS_META_COMPONENT(world, TransitionValue);
    ECS_COMPONENT_DEFINE(world, TransitionType);
    ECS_COMPONENT_DEFINE(world, TransitionState);
    ECS_TAG_DEFINE(world, Transitionable);

    ecs_id(Transition) = ecs_map_type_init(world, &(ecs_map_desc_t){
        .entity = ecs_entity(world, { .name = "Transition" }),
        .key_type = ecs_id(ecs_entity_t),
        .type = ecs_id(TransitionParams)
    });

    ecs_set_hooks(world, TransitionType, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, TransitionState, {
        .ctor = box_transition_state_ctor,
        .dtor = box_transition_state_dtor,
        .copy = box_transition_state_copy,
        .move = box_transition_state_move
    });

    ecs_add_pair(world, ecs_id(Transition), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(TransitionValue), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(TransitionType), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(TransitionState), EcsOnInstantiate, EcsDontInherit);

    box_transition_types_init(world);
    box_transition_records_init(world);

    ecs_add_id(world, ecs_id(Position), Transitionable);
    ecs_add_id(world, ecs_id(Rotation), Transitionable);
    ecs_add_id(world, ecs_id(Scale), Transitionable);
    ecs_add_id(world, ecs_id(Rgb), Transitionable);
    ecs_add_id(world, ecs_id(Opacity), Transitionable);
    ecs_add_id(world, ecs_id(Emissive), Transitionable);
    box_transition_add_type(world, world, ecs_id(ActualTint), ecs_id(Tint));
}
