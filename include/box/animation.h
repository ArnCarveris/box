/**
 * @file box/animation.h
 * @brief Keyframe animation clips: tracks that key one numeric member of a component.
 *
 * A clip is an entity with AnimationClip whose children have AnimationTrack. Sampling a track
 * evaluates its box.curve Curve; a sampled value is written into the real component on the game
 * thread, so any reflected component animates. See docs/animation.md.
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
