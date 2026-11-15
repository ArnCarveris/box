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
    uint32_t version;       /**< AnimatorWorld.version of the set, so a key edit is seen */
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

/** Look up a child by name. A child of an instance spawned from its prefab has no name of its
 * own; it is found through the IsA pair to the prefab child of that name.
 *
 * @param world The world
 * @param parent The parent
 * @param name The name, not terminated
 * @param length The length of the name
 * @return The child, 0 when there is none
 */
ecs_entity_t box_animation_child(
    const ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    ecs_size_t length);

/** Look up the target of a track: a path of names separated by dots, relative to an entity.
 * Each name is looked up with box_animation_child, so a path reaches the children of an
 * instance.
 *
 * @param world The world
 * @param entity The animated entity
 * @param path The path
 * @return The target, 0 when a name is missing
 */
ecs_entity_t box_animation_lookup(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *path);

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

/** Whether a numeric member already holds a value, compared in the kind of the member after
 * the rounding and clamping box_animation_store applies. A kind it does not store never equals.
 *
 * @param kind The ecs_primitive_kind_t of the member
 * @param ptr The member
 * @param value The value
 * @return True when storing the value would leave the member unchanged
 */
bool box_animation_equals(
    int32_t kind,
    const void *ptr,
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

/** World state of the animators, a singleton on its component */
typedef struct AnimatorWorld {
    uint32_t version;   /**< Bumped when a track or a clip changes, so animators rebuild */
    int32_t count;      /**< Entities with the private state of an Animator */
} AnimatorWorld;

/** Id of the private animator world singleton */
extern ECS_COMPONENT_DECLARE(AnimatorWorld);

/** Signal that a track or a clip changed, so that every animator rebuilds its tracks.
 *
 * @param world The world
 * @return The new AnimatorWorld.version, 0 during ecs_fini
 */
uint32_t box_animator_changed(
    ecs_world_t *world);

/** Register Animator, its private state, the observers that keep it current and AnimatorPlay.
 *
 * @param world The world
 */
void box_animator_import(
    ecs_world_t *world);

#endif
