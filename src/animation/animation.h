/**
 * @file animation/animation.h
 * @brief Internal API of box.animation: resolved tracks and writing sampled values.
 */

#ifndef BOX_ANIMATION_INTERNAL_H
#define BOX_ANIMATION_INTERNAL_H

#include <box.h>

/** Track resolved through reflection when its AnimationTrack is set, so a sample or a write
 * touches no reflection. kind is 0 for an invalid track.
 */
typedef struct AnimationTrackState {
    ecs_entity_t component; /**< Component the track writes */
    ecs_size_t size;        /**< Size of the component */
    int32_t offset;         /**< Offset of the member in the component */
    int32_t kind;           /**< ecs_primitive_kind_t of the member, 0 when invalid */
} AnimationTrackState;

/** Id of the private resolved track component */
extern ECS_COMPONENT_DECLARE(AnimationTrackState);

/** Resolve a track: the offset and kind of its member, and the validity of its curve.
 *
 * @param world The world
 * @param track The track
 * @param out Receives the resolved track, its kind 0 when invalid
 * @return NULL when the track is valid, else the reason, to free with ecs_os_free
 */
char* box_animation_resolve(
    const ecs_world_t *world,
    const AnimationTrack *track,
    AnimationTrackState *out);

/** Store a value into a numeric member. An integer rounds to nearest and clamps to the range
 * of its type, so an overshooting curve keeps a uint8_t channel in 0..255.
 *
 * @param kind The ecs_primitive_kind_t of the member
 * @param ptr The member
 * @param value The value
 */
void box_animation_store(
    int32_t kind,
    void *ptr,
    float value);

/** Write a sampled value into the member of a track on the animated entity or its target.
 *
 * Adds the component when the entity does not have it, and signals the change with
 * ecs_modified. Reads no reflection.
 *
 * @param world The world
 * @param track The track
 * @param entity The animated entity, the target of the track is a path relative to it
 * @param value The value, from box_animation_sample
 * @return False for an invalid or deleted track, a deleted component or a missing target
 */
bool box_animation_write(
    ecs_world_t *world,
    ecs_entity_t track,
    ecs_entity_t entity,
    float value);

#endif
