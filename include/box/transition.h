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
 * EaseIn, EaseOut and EaseInOut are the CSS cubic Beziers of the same names; CubicBezier is a
 * CSS cubic-bezier() with the handles of TransitionSpec.bezier.
 */
ECS_ENUM(TimingFunction, {
    Linear = 0,     /**< Constant speed */
    EaseIn = 1,     /**< Starts slow, cubic Bezier (0.42, 0, 1, 1) */
    EaseOut = 2,    /**< Ends slow, cubic Bezier (0, 0, 0.58, 1) */
    EaseInOut = 3,  /**< Starts and ends slow, cubic Bezier (0.42, 0, 0.58, 1) */
    CubicBezier = 4 /**< Cubic Bezier with the handles of TransitionSpec.bezier */
});

/** Handles of a CSS cubic-bezier(x1, y1, x2, y2), a curve from (0, 0) to (1, 1) of progress in
 * x to eased progress in y. In script {0.2, 0, 0, 1}.
 *
 * x1 and x2 must lie in 0..1, as in CSS. y1 and y2 may be outside, the eased progress then
 * leaves 0..1 and the value overshoots its start or target.
 */
ECS_STRUCT(TransitionBezier, {
    float x1;   /**< X of the first handle, 0..1 */
    float y1;   /**< Y of the first handle */
    float x2;   /**< X of the second handle, 0..1 */
    float y2;   /**< Y of the second handle */
});

/** How changes of one component animate. Omitted fields are 0, Linear and false.
 *
 * on_add and on_remove opt in to animating the component in and out from its neutral value,
 * the value that renders like not having it (box_transition_neutral_supported). Without them
 * adding and removing the component snaps.
 */
ECS_STRUCT(TransitionSpec, {
    float duration;                 /**< Seconds from start to target, 0 to switch at once */
    float delay;                    /**< Seconds the rendered value holds still before moving */
    TimingFunction timing_function; /**< Curve of the progress */
    TransitionBezier bezier;        /**< Handles of CubicBezier, ignored by the others */
    bool on_add;                    /**< Adding the component to an entity without a value,
                                     *   owned or inherited, animates from its neutral value */
    bool on_remove;                 /**< Removing the component, when no inherited value
                                     *   remains, animates to its neutral value, then the entity
                                     *   renders without it */
});

/** Map from a component id to the TransitionSpec of its changes, inherited by instances that
 * do not set their own. In script: Transition: [Position: {...}]. An invalid entry
 * (box_transition_valid) logs an error and is ignored.
 *
 * A new value animates; adding and removing the component animate only with on_add and on_remove.
 *
 * A flecs map (ecs_map_t) keyed by the component entity, whose values are allocated
 * TransitionSpecs: read one with ecs_map_get_deref(map, TransitionSpec, component), write one
 * with box_transition_set() and remove one with box_transition_remove().
 */
typedef ecs_map_t Transition;

/** Id of the Transition map type */
extern ECS_COMPONENT_DECLARE(Transition);

/** Progress along a preset timing function. CubicBezier has no handles here and is Linear,
 * use box_transition_ease_bezier for it.
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

/** Progress along a timing function with the handles of CubicBezier.
 *
 * The presets ignore the handles and give box_transition_ease. Pure, with no world access and
 * no allocation, so the render thread can call it.
 *
 * @param fn The timing function
 * @param bezier The handles of CubicBezier, x1 and x2 in 0..1; NULL is Linear for CubicBezier
 * @param t Linear progress, clamped to 0..1
 * @return The eased progress, exactly 0 at 0 and 1 at 1, outside 0..1 in between when a y
 *         handle is
 */
float box_transition_ease_bezier(
    TimingFunction fn,
    const TransitionBezier *bezier,
    float t);

/** Whether the handles of a CubicBezier are valid: finite, with x1 and x2 in 0..1.
 *
 * @param bezier The handles
 * @return True when they are valid
 */
bool box_transition_bezier_valid(
    const TransitionBezier *bezier);

/** Whether changes of a component can transition.
 *
 * Position, Rotation, Scale, Rgb, Opacity, AlphaCutoff, Tint, PbrMaterial, Emissive,
 * TextureTransform, Transmission, ClearCoat, SpecularAnisotropy, Sheen, DiffuseTransmission
 * and BoxShadow can. Every other id cannot.
 *
 * @param world The world
 * @param component The component id
 * @return True when the component is transitionable
 */
bool box_transition_supported(
    const ecs_world_t *world,
    ecs_entity_t component);

/** Whether adding or removing a component can animate from or to its neutral value, the value
 * that renders like not having it.
 *
 * Rotation, Scale, Opacity, Tint, Emissive, PbrMaterial, TextureTransform, ClearCoat,
 * SpecularAnisotropy, Sheen and DiffuseTransmission can in both directions, BoxShadow only when
 * added: a removed BoxShadow frees its shadow at once.
 *
 * Position, Rgb, AlphaCutoff, Transmission and the components that cannot transition have no
 * neutral value: on_add or on_remove on one of them logs an error and is ignored.
 *
 * @param world The world
 * @param component The component id
 * @param remove Whether the component is removed, else added
 * @return True when the change can animate
 */
bool box_transition_neutral_supported(
    const ecs_world_t *world,
    ecs_entity_t component,
    bool remove);

/** Whether an entry of a Transition is used: the component is transitionable, the duration
 * and delay are not negative, and the handles of a CubicBezier are valid
 * (box_transition_bezier_valid). Entries that are not are ignored.
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
 * An inherited map is copied first, so the prefab is unchanged, the spec is freed and OnSet is
 * emitted; the last entry leaves an empty map. Without an entry, owned, inherited or pending,
 * nothing happens: no Transition is added, no map is copied and no OnSet is emitted.
 *
 * On a stage (it->stage, ecs_get_stage()) it is deferred and also removes an entry set on that
 * stage before the merge; on the world (also it->world) it runs at once. Limit: set, ecs_remove()
 * of Transition, then this remove on one stage leaves an empty Transition after the merge.
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
