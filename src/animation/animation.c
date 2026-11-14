/**
 * @file animation/animation.c
 * @brief Animation clips and tracks: resolving a track through reflection, sampling its curve
 * and writing the sampled value into a component. Playback is in animator.c.
 */

#define BOX_ANIMATION_IMPL
#include "animation.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Longest segment of a member path */
#define BOX_ANIMATION_SEGMENT (128)

ECS_COMPONENT_DECLARE(AnimationTrackState);

/** Name of a primitive kind that is not a number.
 *
 * @param kind The primitive kind
 * @return The name, NULL for a numeric kind
 */
static const char* box_animation_non_numeric(
    ecs_primitive_kind_t kind)
{
    const char *result = NULL;
    switch (kind) {
    case EcsBool: result = "a bool"; break;
    case EcsChar: result = "a char"; break;
    case EcsByte: result = "a byte"; break;
    case EcsString: result = "a string"; break;
    case EcsEntity: result = "an entity"; break;
    case EcsId: result = "an id"; break;
    default: break;
    }

    return result;
}

/** Find a member of a struct by name.
 *
 * @param type The struct
 * @param name The name
 * @return The member, NULL when the struct has none of that name
 */
static const ecs_member_t* box_animation_find_member(
    const EcsStruct *type,
    const char *name)
{
    const ecs_member_t *members = ecs_vec_first_t(&type->members, ecs_member_t);
    int32_t i, count = ecs_vec_count(&type->members);
    const ecs_member_t *result = NULL;
    for (i = 0; i < count && !result; i ++) {
        if (members[i].name && !strcmp(members[i].name, name)) {
            result = &members[i];
        }
    }

    return result;
}

/** Walk a member path through the reflection of a type to a numeric member.
 *
 * @param world The world
 * @param type The component
 * @param path The member path, segments separated by dots
 * @param out Receives the offset and kind
 * @return NULL when the member is numeric, else the reason, to free with ecs_os_free
 */
static char* box_animation_walk(
    const ecs_world_t *world,
    ecs_entity_t type,
    const char *path,
    AnimationTrackState *out)
{
    char *result = NULL;
    char segment[BOX_ANIMATION_SEGMENT];
    char *type_name = ecs_get_path(world, type);
    const char *cur = path;
    int32_t offset = 0;
    bool first = true;

    while (!result) {
        const char *dot = strchr(cur, '.');
        size_t len = dot ? (size_t)(dot - cur) : strlen(cur);
        const EcsStruct *st = ecs_get(world, type, EcsStruct);
        if (!len || len >= sizeof(segment)) {
            result = flecs_asprintf("member path '%s' is not valid", path);
        } else if (!st) {
            result = first
                ? flecs_asprintf("component %s has no reflection", type_name)
                : flecs_asprintf("'%s' of member '%s' is not a struct", type_name, path);
        } else {
            ecs_os_memcpy(segment, cur, (ecs_size_t)len);
            segment[len] = '\0';
            const ecs_member_t *m = box_animation_find_member(st, segment);
            if (!m) {
                result = flecs_asprintf("'%s' is not a member of %s", segment, type_name);
            } else if (m->count > 1) {
                result = flecs_asprintf("member '%s' of %s is an array", segment, type_name);
            } else {
                offset += m->offset;
                type = m->type;
                ecs_os_free(type_name);
                type_name = ecs_get_path(world, type);
                first = false;
                if (!dot) {
                    break;
                }

                cur = dot + 1;
            }
        }
    }

    if (!result) {
        const EcsPrimitive *prim = ecs_get(world, type, EcsPrimitive);
        const char *non_numeric = prim ? box_animation_non_numeric(prim->kind) : NULL;
        if (prim && !non_numeric) {
            out->offset = offset;
            out->kind = (int32_t)prim->kind;
        } else if (non_numeric) {
            result = flecs_asprintf("member '%s' is %s, not a number", path, non_numeric);
        } else if (ecs_has(world, type, EcsStruct)) {
            result = flecs_asprintf("member '%s' is a struct (%s), not a number",
                path, type_name);
        } else {
            result = flecs_asprintf("member '%s' of type %s is not a number", path, type_name);
        }
    }

    ecs_os_free(type_name);

    return result;
}

char* box_animation_resolve(
    const ecs_world_t *world,
    const AnimationTrack *track,
    AnimationTrackState *out)
{
    char *result = NULL;
    const EcsComponent *c = NULL;
    ecs_os_zeromem(out);
    out->component = track->component;

    if (!track->component) {
        result = flecs_asprintf("no component");
    } else if (!ecs_is_alive(world, track->component)) {
        result = flecs_asprintf("component #%u is deleted", (uint32_t)track->component);
    } else if (!(c = ecs_get(world, track->component, EcsComponent)) || !c->size) {
        char *name = ecs_get_path(world, track->component);
        result = flecs_asprintf("'%s' is not a component with data", name);
        ecs_os_free(name);
    } else if (!track->member || !track->member[0]) {
        result = flecs_asprintf("no member");
    } else {
        out->size = c->size;
        result = box_animation_walk(world, track->component, track->member, out);
    }

    if (!result && !ecs_vec_count(&track->curve.keys)) {
        result = flecs_asprintf("curve has no keys");
    }

    if (!result) {
        char *curve = box_curve_validate(&track->curve);
        if (curve) {
            result = flecs_asprintf("curve: %s", curve);
            ecs_os_free(curve);
        }
    }

    if (result) {
        out->kind = 0;
    }

    return result;
}

/** Round a value to the nearest integer and clamp it to a range.
 *
 * @param value The value
 * @param min The least value
 * @param max The greatest value
 * @return The rounded and clamped value, 0 for NaN
 */
static double box_animation_round(
    float value,
    double min,
    double max)
{
    double result = isnan(value) ? 0.0 : round((double)value);
    if (result < min) {
        result = min;
    } else if (result > max) {
        result = max;
    }

    return result;
}

/** Store a value into a 64 bit unsigned member, saturating where a double cannot convert.
 *
 * @param ptr The member
 * @param value The value
 */
static void box_animation_store_u64(
    void *ptr,
    float value)
{
    double v = box_animation_round(value, 0.0, 18446744073709551615.0);
    *(uint64_t*)ptr = v >= 18446744073709551615.0 ? UINT64_MAX : (uint64_t)v;
}

/** Store a value into a 64 bit signed member, saturating where a double cannot convert.
 *
 * @param ptr The member
 * @param value The value
 */
static void box_animation_store_i64(
    void *ptr,
    float value)
{
    double v = box_animation_round(value, -9223372036854775808.0, 9223372036854775807.0);
    *(int64_t*)ptr = v >= 9223372036854775807.0 ? INT64_MAX : (int64_t)v;
}

void box_animation_store(
    int32_t kind,
    void *ptr,
    float value)
{
    switch (kind) {
    case EcsF32:
        *(float*)ptr = value;
        break;
    case EcsF64:
        *(double*)ptr = (double)value;
        break;
    case EcsU8:
        *(uint8_t*)ptr = (uint8_t)box_animation_round(value, 0.0, UINT8_MAX);
        break;
    case EcsU16:
        *(uint16_t*)ptr = (uint16_t)box_animation_round(value, 0.0, UINT16_MAX);
        break;
    case EcsU32:
        *(uint32_t*)ptr = (uint32_t)box_animation_round(value, 0.0, UINT32_MAX);
        break;
    case EcsI8:
        *(int8_t*)ptr = (int8_t)box_animation_round(value, INT8_MIN, INT8_MAX);
        break;
    case EcsI16:
        *(int16_t*)ptr = (int16_t)box_animation_round(value, INT16_MIN, INT16_MAX);
        break;
    case EcsI32:
        *(int32_t*)ptr = (int32_t)box_animation_round(value, INT32_MIN, INT32_MAX);
        break;
    case EcsU64:
        box_animation_store_u64(ptr, value);
        break;
    case EcsI64:
        box_animation_store_i64(ptr, value);
        break;
    case EcsUPtr:
        if (sizeof(uintptr_t) == 8) {
            box_animation_store_u64(ptr, value);
        } else {
            *(uintptr_t*)ptr = (uintptr_t)box_animation_round(value, 0.0, UINT32_MAX);
        }

        break;
    case EcsIPtr:
        if (sizeof(intptr_t) == 8) {
            box_animation_store_i64(ptr, value);
        } else {
            *(intptr_t*)ptr = (intptr_t)box_animation_round(value, INT32_MIN, INT32_MAX);
        }

        break;
    default:
        break;
    }
}

/** Resolved state of a live track that is valid and whose component is alive.
 *
 * @param world The world
 * @param track The track
 * @return The state, NULL for a deleted or invalid track or a deleted component
 */
static const AnimationTrackState* box_animation_state(
    const ecs_world_t *world,
    ecs_entity_t track)
{
    const AnimationTrackState *result = NULL;
    if (track && ecs_is_alive(world, track)) {
        result = ecs_get(world, track, AnimationTrackState);
        if (result && (!result->kind || !ecs_is_alive(world, result->component))) {
            result = NULL;
        }
    }

    return result;
}

bool box_animation_sample(
    const ecs_world_t *world,
    ecs_entity_t track,
    float time,
    float *value)
{
    bool result = false;
    if (box_animation_state(world, track)) {
        const AnimationTrack *t = ecs_get(world, track, AnimationTrack);
        if (t && ecs_vec_count(&t->curve.keys)) {
            *value = box_curve_value(&t->curve, time, 0.0f);
            result = true;
        }
    }

    return result;
}

/** The child of an instance that was spawned from a child of its prefab.
 *
 * @param world The world
 * @param parent The instance
 * @param base_child The child of the prefab
 * @return The child of the instance with an IsA pair to the prefab child, 0 when none
 */
static ecs_entity_t box_animation_spawned_child(
    const ecs_world_t *world,
    ecs_entity_t parent,
    ecs_entity_t base_child)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count && !result; i ++) {
            result = ecs_has_pair(world, it.entities[i], EcsIsA, base_child) ? it.entities[i] : 0;
        }
    }

    return result;
}

/** The child of an instance spawned from a child of its prefab. Such a child has no name of
 * its own, only an IsA pair to the prefab child.
 *
 * @param world The world
 * @param parent The instance
 * @param name The name of the child in the prefab
 * @param length The length of the name
 * @return The child, 0 when the instance has none of that name
 */
static ecs_entity_t box_animation_instance_child(
    const ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    ecs_size_t length)
{
    ecs_entity_t result = 0;
    ecs_entity_t base;
    int32_t i;
    for (i = 0; !result && (base = ecs_get_target(world, parent, EcsIsA, i)); i ++) {
        ecs_entity_t base_child = box_animation_child(world, base, name, length);
        result = base_child ? box_animation_spawned_child(world, parent, base_child) : 0;
    }

    return result;
}

ecs_entity_t box_animation_child(
    const ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    ecs_size_t length)
{
    char buf[256];
    ecs_entity_t result = 0;
    if (length > 0 && length < (ecs_size_t)sizeof(buf)) {
        ecs_os_memcpy(buf, name, length);
        buf[length] = '\0';
        result = ecs_lookup_child(world, parent, buf);
        result = result ? result : box_animation_instance_child(world, parent, name, length);
    }

    return result;
}

ecs_entity_t box_animation_lookup(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *path)
{
    ecs_entity_t result = entity;
    const char *cur = path;
    while (result && cur && cur[0]) {
        const char *dot = strchr(cur, '.');
        ecs_size_t length = dot ? (ecs_size_t)(dot - cur) : ecs_os_strlen(cur);
        result = box_animation_child(world, result, cur, length);
        cur = dot ? dot + 1 : NULL;
    }

    return result;
}

bool box_animation_write(
    ecs_world_t *world,
    ecs_entity_t track,
    ecs_entity_t entity,
    float value)
{
    bool result = false;
    const AnimationTrackState *state = box_animation_state(world, track);
    const AnimationTrack *t = state ? ecs_get(world, track, AnimationTrack) : NULL;
    ecs_entity_t target = entity;
    if (t && t->target && t->target[0]) {
        target = box_animation_lookup(world, entity, t->target);
    }

    if (t && target && ecs_is_alive(world, target)) {
        AnimationTrackState s = *state;
        void *ptr = ecs_ensure_id(world, target, s.component, (size_t)s.size);
        box_animation_store(s.kind, ECS_OFFSET(ptr, s.offset), value);
        ecs_modified_id(world, target, s.component);
        result = true;
    }

    return result;
}

float box_animation_clip_duration(
    const ecs_world_t *world,
    ecs_entity_t clip)
{
    const AnimationClip *c = ecs_get(world, clip, AnimationClip);
    float result = c && c->duration > 0.0f ? c->duration : 0.0f;
    if (result == 0.0f) {
        ecs_iter_t it = ecs_children(world, clip);
        while (ecs_children_next(&it)) {
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                ecs_entity_t e = it.entities[i];
                const AnimationTrack *t = box_animation_state(world, e)
                    ? ecs_get(world, e, AnimationTrack) : NULL;
                int32_t count = t ? ecs_vec_count(&t->curve.keys) : 0;
                if (count) {
                    float last = ecs_vec_get_t(&t->curve.keys, CurveKey, count - 1)->time;
                    result = last > result ? last : result;
                }
            }
        }
    }

    return result;
}

/** Log the error of an invalid track, naming its clip and the track.
 *
 * @param world The world
 * @param track The track
 * @param reason The reason
 */
static void box_animation_report(
    const ecs_world_t *world,
    ecs_entity_t track,
    const char *reason)
{
    ecs_entity_t clip = ecs_get_parent(world, track);
    char *clip_path = clip ? ecs_get_path(world, clip) : NULL;
    const char *name = ecs_get_name(world, track);
    char id[32];
    if (!name) {
        snprintf(id, sizeof(id), "#%u", (uint32_t)track);
        name = id;
    }

    ecs_err("AnimationTrack '%s' of clip '%s': %s",
        name, clip_path ? clip_path : "(none)", reason);
    ecs_os_free(clip_path);
}

/** Resolve tracks when their AnimationTrack is set, and log the invalid ones. Each resolved
 * track carries the new AnimatorWorld.version, so an animator sees a key edit of its own tracks.
 *
 * @param it The iterator
 */
static void BoxAnimationTrackResolve(
    ecs_iter_t *it)
{
    const AnimationTrack *tracks = ecs_field(it, AnimationTrack, 0);
    uint32_t version = box_animator_changed(it->world);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        AnimationTrackState state;
        char *reason = box_animation_resolve(it->world, &tracks[i], &state);
        if (reason) {
            box_animation_report(it->world, it->entities[i], reason);
            ecs_os_free(reason);
        }

        state.version = version;
        ecs_set_ptr(it->stage, it->entities[i], AnimationTrackState, &state);
    }
}

/** Remove the resolved state of tracks whose AnimationTrack is removed.
 *
 * @param it The iterator
 */
static void BoxAnimationTrackRemove(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], AnimationTrackState);
    }

    box_animator_changed(it->world);
}

void BoxAnimationImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxAnimation);
    ECS_IMPORT(world, BoxCurve);

    ecs_set_name_prefix(world, "Animation");
    ECS_META_COMPONENT(world, AnimationTrack);
    box_owned_hooks(world, AnimationTrack,
        BOX_OWNED_STR(AnimationTrack, member), BOX_OWNED_STR(AnimationTrack, target),
        BOX_OWNED_CURVE(AnimationTrack, curve));
    ECS_META_COMPONENT(world, AnimationClip);
    ECS_COMPONENT_DEFINE(world, AnimationTrackState);
    ECS_META_COMPONENT(world, AnimationMode);
    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, Animator);
    box_animator_import(world);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxAnimationTrackResolve" }),
        .query.terms = {{ .id = ecs_id(AnimationTrack), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = BoxAnimationTrackResolve
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxAnimationTrackRemove" }),
        .query.terms = {{ .id = ecs_id(AnimationTrack), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnRemove },
        .callback = BoxAnimationTrackRemove
    });
}
