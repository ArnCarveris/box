/**
 * @file box/transition.h
 * @brief Transition components: the duration, delay and timing of a displayed value.
 *
 * The box.transition module never changes the value an author assigned. A
 * Transition maps a component, or one of its member entities, to the
 * TransitionParams the engine interpolates that value with, and instances
 * inherit the map from their prefab. Assigning a component that the map covers
 * starts a transition from the value currently displayed toward the new one,
 * so the world keeps the authored value while a view shows the value in
 * between. Adding (TransitionValue, Component) to an entity asks the engine to
 * keep the interpolated value of that component there for scripts to read.
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

/** Number of floats a single transitioned value is made of */
#define BOX_TRANSITION_MAX (4)

/** Curve the progress of a transition follows between its start and its end */
ECS_ENUM(TimingFunction, {
    Linear = 0,   /**< Constant rate from start to end */
    Ease = 1,     /**< Slow start and a slower end, the default */
    EaseIn = 2,   /**< Slow start, full rate at the end */
    EaseOut = 3,  /**< Full rate at the start, slow end */
    EaseInOut = 4 /**< Symmetric slow start and slow end */
});

/** How long a value takes to reach the one that was assigned to it */
ECS_STRUCT(TransitionParams, {
    float duration;                 /**< Seconds the value takes to reach the target */
    float delay;                    /**< Seconds the value holds still before it moves */
    TimingFunction timing_function; /**< Curve the progress follows */
});

/** Interpolated value of a component, kept by the engine as (TransitionValue, Component) */
ECS_STRUCT(TransitionValue, {
    float value[4]; /**< Floats of the component, the unused ones are zero */
});

/** Map from a component or member entity to its parameters, inherited by instances.
 *
 * The component is the map itself, so a script writes it as
 * `Transition: [Rgb: {duration: 0.2}, Position.x: {duration: 0.4}]`.
 */
typedef ecs_map_t Transition;

extern ECS_COMPONENT_DECLARE(Transition);

/** Added to a component entity to let transitions interpolate it.
 *
 * The module reads the reflection of the component and interpolates every
 * float and byte member it finds, up to BOX_TRANSITION_MAX of them.
 */
extern ECS_TAG_DECLARE(Transitionable);

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
