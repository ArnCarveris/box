/**
 * @file box/animation.h
 * @brief Keyframe animation: clips of tracks that key one numeric member of a component, and
 * the Animator that plays a clip on an entity.
 *
 * A clip is an entity with AnimationClip whose children have AnimationTrack. An Animator writes
 * the sampled values into the real components on the game thread. See docs/animation.md.
 */

#ifndef BOX_ANIMATION_H
#define BOX_ANIMATION_H

#include "curve.h"

#ifdef BOX_ANIMATION_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** One numeric member of one component of one entity, keyed by a curve. A child of its clip.
 *
 * Rotation is Euler angles: each axis is its own track with no shortest path. An invalid track
 * logs one error naming the clip, the track and the reason when it is set, and is skipped.
 */
ECS_STRUCT(AnimationTrack, {
    ecs_entity_t component; /**< Component the track writes */
    char *member;           /**< Path of a numeric member, such as "y" or "color.r" */
    char *target;           /**< Path relative to the animated entity, NULL for itself */
    Curve curve;            /**< Keys: seconds from the clip start, value in the member unit */
});

/** Keyframe animation: an entity whose children with AnimationTrack are its tracks. */
ECS_STRUCT(AnimationClip, {
    float duration;         /**< Seconds; 0 for the time of the last key of any track */
});

/** How an Animator wraps its time at the ends of its clip */
ECS_ENUM(AnimationMode, {
    AnimationOnce = 0,    /**< Plays to the end and holds the last value */
    AnimationLoop = 1,    /**< Starts over at the end */
    AnimationPingPong = 2 /**< Plays back and forth */
});

/** Plays a clip on its entity and, through the target paths of the tracks, its descendants.
 *
 * Constructed with speed 1 and AnimationLoop. Each instance of a prefab owns its copy, so it
 * keeps its own time. One clip per entity: to play two, give each its own child with Animator.
 * An entity a track writes Position, Rotation or Scale of gets its own DynamicTransform.
 */
ECS_STRUCT(Animator, {
    ecs_entity_t clip;  /**< Entity with AnimationClip */
    float time;         /**< Seconds into the clip, written by the system, may be set to seek */
    float speed;        /**< Multiplier of the frame delta, default 1, may be negative */
    AnimationMode mode; /**< Default AnimationLoop */
    bool paused;        /**< Holds the time; the values are still written once when it changes */
    bool finished;      /**< A clip in AnimationOnce reached its end, written by the system */
});

/** Value of a track at a time, from the evaluator of box.curve. Allocates nothing.
 *
 * Before the first key and after the last the curve holds its first or last value.
 *
 * @param world The world
 * @param track Entity with AnimationTrack
 * @param time Seconds from the start of the clip
 * @param value Receives the value, untouched for an invalid track
 * @return False for an invalid, deleted or keyless track
 */
bool box_animation_sample(
    const ecs_world_t *world,
    ecs_entity_t track,
    float time,
    float *value);

/** Duration of a clip.
 *
 * @param world The world
 * @param clip Entity with AnimationClip
 * @return The duration, or when it is 0 the time of the last key of its valid tracks
 */
float box_animation_clip_duration(
    const ecs_world_t *world,
    ecs_entity_t clip);

/** Register the box.animation module.
 *
 * @param world The world
 */
void BoxAnimationImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
