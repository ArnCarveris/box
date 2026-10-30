/**
 * @file box/transition.h
 * @brief Transition component: how changes of a component animate on screen.
 *
 * Transition maps a component to a TransitionSpec. The component holds a new value at once for
 * every game thread reader; only the rendered pixels move to it along the spec. Inherited by
 * instances. See docs/transitions.md, and Transition below for the rules of its entries.
 */

#ifndef BOX_TRANSITION_H
#define BOX_TRANSITION_H

#include "types.h"

#ifdef BOX_TRANSITION_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Curve a transition follows from its start to its target value, default Linear.
 *
 * EaseIn, EaseOut and EaseInOut are the CSS cubic Beziers of the same names.
 */
ECS_ENUM(TimingFunction, {
    Linear = 0,    /**< Constant speed */
    EaseIn = 1,    /**< Starts slow, cubic Bezier (0.42, 0, 1, 1) */
    EaseOut = 2,   /**< Ends slow, cubic Bezier (0, 0, 0.58, 1) */
    EaseInOut = 3  /**< Starts and ends slow, cubic Bezier (0.42, 0, 0.58, 1) */
});

/** How changes of one component animate. Omitted fields are 0 and Linear. */
ECS_STRUCT(TransitionSpec, {
    float duration;                 /**< Seconds from start to target, 0 to switch at once */
    float delay;                    /**< Seconds the rendered value holds still before moving */
    TimingFunction timing_function; /**< Curve of the progress */
});

/** Map from a component id to the TransitionSpec of its changes, inherited by instances; an
 * instance that sets its own replaces the whole map. In script: Transition: [Position: {...}].
 * A key that is not transitionable or a negative duration or delay logs an error and is ignored.
 *
 * The storage is a flecs map (ecs_map_t) keyed by the component entity, whose values are
 * allocated TransitionSpecs: read one with ecs_map_get_deref(map, TransitionSpec, component),
 * write one from C with box_transition_set() and remove one with box_transition_remove().
 */
typedef ecs_map_t Transition;

/** Id of the Transition map type */
extern ECS_COMPONENT_DECLARE(Transition);

/** Progress along a timing function.
 *
 * Pure, with no world access and no allocation, so the render thread can call it.
 *
 * @param fn The timing function
 * @param t Linear progress, clamped to 0..1
 * @return The eased progress, exactly 0 at 0 and 1 at 1
 */
float box_transition_ease(
    TimingFunction fn,
    float t);

/** Whether changes of a component can transition.
 *
 * Position, Rotation, Scale, Rgb, Opacity, AlphaCutoff, Tint, PbrMaterial, Emissive and
 * TextureTransform can. Every other id cannot.
 *
 * @param world The world
 * @param component The component id
 * @return True when the component is transitionable
 */
bool box_transition_supported(
    const ecs_world_t *world,
    ecs_entity_t component);

/** Whether an entry of a Transition is used: the component is transitionable and the duration
 * and delay are not negative. Entries that are not are ignored.
 *
 * @param world The world
 * @param component The key of the entry
 * @param spec The value of the entry
 * @return True when the entry is valid
 */
bool box_transition_valid(
    const ecs_world_t *world,
    ecs_entity_t component,
    const TransitionSpec *spec);

/** Set the transition of one component of an entity, adding Transition when missing.
 *
 * An entity that inherits Transition gets its own copy first, so the other entries stay. The
 * entry replaces an existing one for the component, and OnSet is emitted.
 *
 * @param world The world
 * @param entity The entity
 * @param component The component the entry applies to
 * @param spec The transition of the component
 */
void box_transition_set(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    const TransitionSpec *spec);

/** Remove the transition of one component of an entity.
 *
 * An inherited map is copied first, so the prefab is unchanged. The TransitionSpec is freed and
 * OnSet is emitted. Removing the last entry leaves Transition with an empty map; it does not
 * remove the component. Works deferred, like box_transition_set().
 *
 * Without an entry for the component, owned or inherited, nothing happens: no Transition is
 * added, no map is copied and no OnSet is emitted. Deferred, the entry is looked up in the merged
 * state, so an entry set on the same stage before the merge is not seen.
 *
 * @param world The world
 * @param entity The entity
 * @param component The component of the entry
 */
void box_transition_remove(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component);

/** Get the transition of one component of an entity, owned or inherited.
 *
 * @param world The world
 * @param entity The entity
 * @param component The component of the entry
 * @return The entry, NULL when the entity has no entry for the component
 */
const TransitionSpec* box_transition_get(
    const ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component);

/** Register the box.transition module.
 *
 * @param world The world
 */
void BoxTransitionImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
