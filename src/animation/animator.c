/**
 * @file animation/animator.c
 * @brief Animator playback: advances the time of every Animator, resolves the targets of the
 * tracks of its clip and writes the sampled values into their components.
 */

#include "animation.h"
#include <math.h>

ECS_COMPONENT_DECLARE(AnimatorWorld);

/** A track of the clip of an animator, resolved to the entity it writes */
typedef struct animator_binding_t {
    ecs_entity_t track;         /**< The track */
    ecs_entity_t target;        /**< Entity the track writes */
    AnimationTrackState state;  /**< Component, size, offset and kind of the member */
    ecs_ref_t ref;              /**< AnimationTrack of the track */
} animator_binding_t;

/** Private state of an entity with Animator: the resolved tracks of its clip */
typedef struct AnimatorState {
    ecs_entity_t clip;      /**< Clip the bindings were built for */
    uint32_t version;       /**< AnimatorWorld.version of the build */
    float duration;         /**< Duration of the clip at the build */
    float time;             /**< Time of the previous frame */
    bool back;              /**< AnimationPingPong is on its way back */
    bool built;             /**< The bindings were built at least once */
    bool started;           /**< The clip played at least one frame */
    bool dirty;             /**< The hierarchy under the entity changed */
    ecs_vec_t bindings;     /**< animator_binding_t, the valid tracks with a target */
    ecs_vec_t warned;       /**< ecs_entity_t, tracks whose missing target was reported */
} AnimatorState;

static ECS_COMPONENT_DECLARE(AnimatorState);

/** Free the vectors of an animator state.
 *
 * @param s The state
 */
static void box_animator_state_free(
    AnimatorState *s)
{
    ecs_vec_fini_t(NULL, &s->bindings, animator_binding_t);
    ecs_vec_fini_t(NULL, &s->warned, ecs_entity_t);
}

/** Destruct animator states.
 *
 * @param ptr The states
 * @param count Number of states
 * @param ti Type info
 */
static void box_animator_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AnimatorState *s = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_animator_state_free(&s[i]);
    }
}

/** Move animator states, leaving the sources empty.
 *
 * @param dst The destination states
 * @param src The source states
 * @param count Number of states
 * @param ti Type info
 */
static void box_animator_state_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    box_animator_state_dtor(dst, count, ti);
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

/** Copy animator states: a copy starts empty and builds its own bindings.
 *
 * @param dst The destination states
 * @param src The source states
 * @param count Number of states
 * @param ti Type info
 */
static void box_animator_state_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    box_animator_state_dtor(dst, count, ti);
    ecs_os_memset(dst, 0, ti->size * count);
}

/** Construct animators with speed 1 and AnimationLoop.
 *
 * @param ptr The animators
 * @param count Number of animators
 * @param ti Type info
 */
static void box_animator_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Animator *a = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        a[i] = (Animator){ .speed = 1.0f, .mode = AnimationLoop };
    }
}

/** The world of an iterator, never a stage.
 *
 * @param it The iterator
 * @return The world
 */
static ecs_world_t* box_animator_real(
    const ecs_iter_t *it)
{
    return ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
}

/** The animator world, unless the world is being deleted.
 *
 * @param world The world
 * @return The singleton, NULL during ecs_fini
 */
static AnimatorWorld* box_animator_world(
    ecs_world_t *world)
{
    AnimatorWorld *result = NULL;
    world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    if (!ecs_is_fini(world)) {
        result = ecs_singleton_get_mut(world, AnimatorWorld);
    }

    return result;
}

void box_animator_changed(
    ecs_world_t *world)
{
    AnimatorWorld *w = box_animator_world(world);
    if (w) {
        w->version ++;
    }
}

/** Clamp a value to a range.
 *
 * @param value The value
 * @param min The least value
 * @param max The greatest value
 * @return The clamped value
 */
static float box_animator_clamp(
    float value,
    float min,
    float max)
{
    return value < min ? min : (value > max ? max : value);
}

/** Report once that the target of a track of the clip of an animator is missing.
 *
 * @param world The world
 * @param e The animated entity
 * @param s Its state
 * @param track The track
 * @param path The target path of the track
 */
static void box_animator_warn(
    const ecs_world_t *world,
    ecs_entity_t e,
    AnimatorState *s,
    ecs_entity_t track,
    const char *path)
{
    const ecs_entity_t *warned = ecs_vec_first_t(&s->warned, ecs_entity_t);
    int32_t i, count = ecs_vec_count(&s->warned);
    bool found = false;
    for (i = 0; i < count && !found; i ++) {
        found = warned[i] == track;
    }

    if (!found) {
        *ecs_vec_append_t(NULL, &s->warned, ecs_entity_t) = track;
        char *entity = ecs_get_path(world, e);
        char *name = ecs_get_path(world, track);
        ecs_warn("Animator '%s': target '%s' of track '%s' not found, the track is skipped",
            entity, path, name);
        ecs_os_free(entity);
        ecs_os_free(name);
    }
}

/** Bind a child of the clip of an animator when it is a valid track whose target exists.
 *
 * @param world The world
 * @param e The animated entity
 * @param s Its state
 * @param track The child of the clip
 */
static void box_animator_bind(
    const ecs_world_t *world,
    ecs_entity_t e,
    AnimatorState *s,
    ecs_entity_t track)
{
    const AnimationTrackState *state = ecs_get(world, track, AnimationTrackState);
    const AnimationTrack *t = state && state->kind
        ? ecs_get(world, track, AnimationTrack) : NULL;
    ecs_entity_t target = e;
    if (t && t->target && t->target[0]) {
        target = ecs_lookup_path_w_sep(world, e, t->target, ".", NULL, false);
    }

    if (t && target) {
        animator_binding_t *b = ecs_vec_append_t(NULL, &s->bindings, animator_binding_t);
        b->track = track;
        b->target = target;
        b->state = *state;
        b->ref = ecs_ref_init(world, track, AnimationTrack);
    } else if (t) {
        box_animator_warn(world, e, s, track, t->target);
    }
}

/** Rebuild the bindings of an animator: the target of each track of its clip.
 *
 * @param world The world
 * @param e The animated entity
 * @param a Its animator
 * @param s Its state
 * @param version The current AnimatorWorld.version
 */
static void box_animator_build(
    const ecs_world_t *world,
    ecs_entity_t e,
    const Animator *a,
    AnimatorState *s,
    uint32_t version)
{
    if (s->clip != a->clip) {
        ecs_vec_clear(&s->warned);
        s->back = false;
        s->started = false;
    }

    ecs_vec_clear(&s->bindings);
    s->clip = a->clip;
    s->version = version;
    s->dirty = false;
    s->built = true;
    s->duration = box_animation_clip_duration(world, a->clip);

    ecs_iter_t it = ecs_children(world, a->clip);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_animator_bind(world, e, s, it.entities[i]);
        }
    }
}

/** Advance the time of an animator by a step and wrap it by its mode.
 *
 * @param a The animator
 * @param s Its state, whose ping pong direction is updated
 * @param time The time before the step
 * @param step Seconds to advance, negative to play back
 * @return The time after the step, in 0..duration
 */
static float box_animator_advance(
    const Animator *a,
    AnimatorState *s,
    float time,
    float step)
{
    float d = s->duration;
    float result = 0.0f;
    if (d <= 0.0f) {
        result = 0.0f;
    } else if (a->mode == AnimationOnce) {
        result = box_animator_clamp(time + step, 0.0f, d);
    } else if (a->mode == AnimationPingPong) {
        float period = 2.0f * d;
        float t = box_animator_clamp(time, 0.0f, d);
        float x = fmodf((s->back ? period - t : t) + step, period);
        x = x < 0.0f ? x + period : x;
        x = x >= period ? 0.0f : x;
        s->back = x > d;
        result = s->back ? period - x : x;
    } else {
        float t = fmodf(time + step, d);
        t = t < 0.0f ? t + d : t;
        result = t >= d ? 0.0f : t;
    }

    return result;
}

/** Whether an animator in AnimationOnce reached the end it plays towards.
 *
 * @param a The animator, with its new time
 * @param duration The duration of its clip
 * @return True when the clip in AnimationOnce is at its end
 */
static bool box_animator_finished(
    const Animator *a,
    float duration)
{
    bool result = false;
    if (a->mode == AnimationOnce) {
        result = duration <= 0.0f || (a->speed < 0.0f ? a->time <= 0.0f : a->time >= duration);
    }

    return result;
}

/** Sample every bound track at a time and write the values into the targets.
 *
 * @param world The world
 * @param stage The stage the system writes through
 * @param s The state of the animator
 * @param time The time
 */
static void box_animator_write(
    const ecs_world_t *world,
    ecs_world_t *stage,
    AnimatorState *s,
    float time)
{
    animator_binding_t *b = ecs_vec_first_t(&s->bindings, animator_binding_t);
    int32_t i, count = ecs_vec_count(&s->bindings);
    for (i = 0; i < count; i ++) {
        const AnimationTrack *t = ecs_ref_get(world, &b[i].ref, AnimationTrack);
        if (t && ecs_vec_count(&t->curve.keys) && ecs_is_alive(world, b[i].target) &&
            ecs_is_alive(world, b[i].state.component))
        {
            float value = box_curve_value(&t->curve, time, 0.0f);
            void *ptr = ecs_ensure_id(stage, b[i].target, b[i].state.component,
                (size_t)b[i].state.size);
            box_animation_store(b[i].state.kind, ECS_OFFSET(ptr, b[i].state.offset), value);
            ecs_modified_id(stage, b[i].target, b[i].state.component);
        }
    }
}

/** Play one frame of an animator.
 *
 * @param world The world
 * @param stage The stage the system writes through
 * @param e The animated entity
 * @param a Its animator
 * @param s Its state
 * @param version The current AnimatorWorld.version
 * @param dt The delta time of the frame
 */
static void box_animator_play(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    Animator *a,
    AnimatorState *s,
    uint32_t version,
    float dt)
{
    ecs_entity_t clip = a->clip;
    if (clip && ecs_is_alive(world, clip) && ecs_has(world, clip, AnimationClip)) {
        bool rebuild = !s->built || s->clip != clip || s->version != version || s->dirty;
        if (rebuild) {
            box_animator_build(world, e, a, s, version);
        }

        float time = a->time;
        bool write = rebuild || !s->started || time != s->time;
        if (!s->started && a->speed < 0.0f && time <= 0.0f) {
            time = s->duration;
        }

        if (!a->paused) {
            time = box_animator_advance(a, s, time, dt * a->speed);
        }

        write = write || time != s->time;
        a->time = time;
        a->finished = box_animator_finished(a, s->duration);
        if (write) {
            box_animator_write(world, stage, s, time);
        }

        s->time = time;
        s->started = true;
    }
}

/** Play every animator for one frame.
 *
 * @param it The iterator
 */
static void BoxAnimatorPlay(
    ecs_iter_t *it)
{
    Animator *a = ecs_field(it, Animator, 0);
    AnimatorState *s = ecs_field(it, AnimatorState, 1);
    const AnimatorWorld *w = ecs_singleton_get(box_animator_real(it), AnimatorWorld);
    uint32_t version = w ? w->version : 0;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (s) {
            box_animator_play(box_animator_real(it), it->world, it->entities[i], &a[i], &s[i],
                version, it->delta_time);
        } else {
            ecs_add(it->world, it->entities[i], AnimatorState);
        }
    }
}

/** Mark an entity and its ancestors with Animator for a rebuild of their targets.
 *
 * @param world The world
 * @param e The entity whose children changed
 */
static void box_animator_mark(
    ecs_world_t *world,
    ecs_entity_t e)
{
    while (e && ecs_is_alive(world, e)) {
        AnimatorState *s = ecs_get_mut(world, e, AnimatorState);
        if (s) {
            s->dirty = true;
        }

        e = ecs_get_parent(world, e);
    }
}

/** Whether the world has animators whose targets a hierarchy change may move.
 *
 * @param world The world
 * @return True when an entity has the state of an Animator
 */
static bool box_animator_any(
    ecs_world_t *world)
{
    const AnimatorWorld *w = box_animator_world(world);

    return w && w->count > 0;
}

/** Mark the animators above a parent whose children changed through ChildOf.
 *
 * @param it The iterator
 */
static void BoxAnimatorOnChildOf(
    ecs_iter_t *it)
{
    if (box_animator_any(box_animator_real(it))) {
        ecs_id_t pair = ecs_field_id(it, 0);
        box_animator_mark(box_animator_real(it), ecs_pair_second(box_animator_real(it), pair));
    }
}

/** Mark the animators above a parent whose children changed through EcsParent.
 *
 * @param it The iterator
 */
static void BoxAnimatorOnParent(
    ecs_iter_t *it)
{
    if (box_animator_any(box_animator_real(it))) {
        const EcsParent *p = ecs_field(it, EcsParent, 0);
        int32_t i;
        for (i = 0; i < it->count; i ++) {
            box_animator_mark(box_animator_real(it), p[i].value);
        }
    }
}

/** Mark the animators above an entity whose name changed.
 *
 * @param it The iterator
 */
static void BoxAnimatorOnName(
    ecs_iter_t *it)
{
    ecs_world_t *world = box_animator_real(it);
    if (box_animator_any(world)) {
        int32_t i;
        for (i = 0; i < it->count; i ++) {
            box_animator_mark(world, ecs_get_parent(world, it->entities[i]));
        }
    }
}

/** Signal that a clip changed, so that its duration is read again.
 *
 * @param it The iterator
 */
static void BoxAnimatorOnClip(
    ecs_iter_t *it)
{
    box_animator_changed(it->world);
}

/** Add the private state to an instance that gained Animator without it, remove it with
 * Animator. Every other entity gains it with Animator through With.
 *
 * @param it The iterator
 */
static void BoxAnimatorOnAnimator(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (it->event == EcsOnAdd) {
            if (!ecs_has(box_animator_real(it), it->entities[i], AnimatorState)) {
                ecs_add(it->world, it->entities[i], AnimatorState);
            }
        } else {
            ecs_remove(it->world, it->entities[i], AnimatorState);
        }
    }
}

/** Count the entities with the private state of an Animator.
 *
 * @param it The iterator
 */
static void BoxAnimatorOnState(
    ecs_iter_t *it)
{
    AnimatorWorld *w = box_animator_world(box_animator_real(it));
    if (w) {
        w->count += it->event == EcsOnAdd ? it->count : -it->count;
    }
}

/** Create an observer of the animators.
 *
 * @param world The world
 * @param name The name of the observer
 * @param id The observed id
 * @param event The first event
 * @param event2 The second event, 0 for none
 * @param callback The callback
 */
static void box_animator_observer(
    ecs_world_t *world,
    const char *name,
    ecs_id_t id,
    ecs_entity_t event,
    ecs_entity_t event2,
    ecs_iter_action_t callback)
{
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = name }),
        .query.terms = {{ .id = id, .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { event, event2 },
        .callback = callback
    });
}

void box_animator_import(
    ecs_world_t *world)
{
    ecs_set_hooks(world, Animator, { .ctor = box_animator_ctor });
    ecs_add_pair(world, ecs_id(Animator), EcsOnInstantiate, EcsCopy);

    ECS_COMPONENT_DEFINE(world, AnimatorWorld);
    ecs_singleton_set(world, AnimatorWorld, { 0 });
    ECS_COMPONENT_DEFINE(world, AnimatorState);
    ecs_set_hooks(world, AnimatorState, {
        .ctor = flecs_default_ctor,
        .dtor = box_animator_state_dtor,
        .move = box_animator_state_move,
        .copy = box_animator_state_copy
    });
    ecs_add_pair(world, ecs_id(AnimatorState), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Animator), EcsWith, ecs_id(AnimatorState));

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxAnimatorOnAnimator" }),
        .query.terms = {{ .id = ecs_id(Animator), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxAnimatorOnAnimator
    });

    box_animator_observer(world, "BoxAnimatorOnState", ecs_id(AnimatorState),
        EcsOnAdd, EcsOnRemove, BoxAnimatorOnState);
    box_animator_observer(world, "BoxAnimatorOnClip", ecs_id(AnimationClip),
        EcsOnSet, EcsOnRemove, BoxAnimatorOnClip);
    box_animator_observer(world, "BoxAnimatorOnChildOf", ecs_pair(EcsChildOf, EcsWildcard),
        EcsOnAdd, EcsOnRemove, BoxAnimatorOnChildOf);
    box_animator_observer(world, "BoxAnimatorOnParent", ecs_id(EcsParent),
        EcsOnSet, EcsOnRemove, BoxAnimatorOnParent);
    box_animator_observer(world, "BoxAnimatorOnName", ecs_pair(ecs_id(EcsIdentifier), EcsName),
        EcsOnSet, 0, BoxAnimatorOnName);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AnimatorPlay" }),
        .query.terms = {
            { .id = ecs_id(Animator), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(AnimatorState), .src.id = EcsSelf, .inout = EcsInOut,
              .oper = EcsOptional },
            { .id = EcsWildcard, .src.id = EcsIsEntity, .inout = EcsOut }
        },
        .phase = EcsOnUpdate,
        .callback = BoxAnimatorPlay
    });
}
