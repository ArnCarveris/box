/**
 * @file transition/transition.c
 * @brief Transition component, timing functions and validation of the entries.
 */

#define BOX_TRANSITION_IMPL
#include <box.h>

/* A runtime map type, so that flecs installs the map hooks that copy and free the entries and
 * the script parser accepts Transition: [Position: {...}] */
ECS_COMPONENT_DECLARE(Transition);

/** Entities with a Transition map that a deferred call put in a command value of a stage, which
 * ecs_get_id does not see until the merge. A second ecs_ensure_id on the stage returns that value
 * without adding a command, so box_transition_remove can ensure it without adding anything. */
typedef struct box_transition_pending_t {
    ecs_os_mutex_t lock;        /**< Guards entities, 0 without threading */
    ecs_map_t entities;         /**< Entity to a mask of the stage ids with a pending map */
} box_transition_pending_t;

/** Singleton that holds the pending maps of a world */
typedef struct TransitionPending {
    box_transition_pending_t *pending; /**< The pending maps */
} TransitionPending;

/** Event, enqueued after a pending map, that forgets it when the stage is flushed */
typedef struct TransitionPendingFlush {
    ecs_entity_t entity;        /**< The entity of the map */
    int32_t stage;              /**< Id of the stage */
} TransitionPendingFlush;

static ECS_COMPONENT_DECLARE(TransitionPending);
static ECS_COMPONENT_DECLARE(TransitionPendingFlush);

/** Stages with an id from this on are not tracked, the remove misses their pending maps */
#define BOX_TRANSITION_PENDING_STAGES (64)

/** Iterations of Newton's method before falling back to bisection */
#define BOX_TRANSITION_NEWTON_STEPS (8)

/** Iterations of bisection, enough for float precision */
#define BOX_TRANSITION_BISECT_STEPS (32)

/** Accepted error of the solved x of a curve */
#define BOX_TRANSITION_EPSILON (1e-6f)

/** One coordinate of a cubic Bezier from (0, 0) to (1, 1).
 *
 * @param p1 Coordinate of the first control point
 * @param p2 Coordinate of the second control point
 * @param s Curve parameter, 0..1
 * @return The coordinate at s
 */
static float box_transition_bezier(
    float p1,
    float p2,
    float s)
{
    float u = 1.0f - s;

    return 3.0f * u * u * s * p1 + 3.0f * u * s * s * p2 + s * s * s;
}

/** Derivative of one coordinate of a cubic Bezier from (0, 0) to (1, 1).
 *
 * @param p1 Coordinate of the first control point
 * @param p2 Coordinate of the second control point
 * @param s Curve parameter, 0..1
 * @return The derivative at s
 */
static float box_transition_bezier_slope(
    float p1,
    float p2,
    float s)
{
    float u = 1.0f - s;

    return 3.0f * u * u * p1 + 6.0f * u * s * (p2 - p1) + 3.0f * s * s * (1.0f - p2);
}

/** Curve parameter where the x of a cubic Bezier equals t, with Newton's method, falling back
 * to bisection when Newton leaves the curve or stalls.
 *
 * @param x1 X of the first control point, 0..1
 * @param x2 X of the second control point, 0..1
 * @param t The x to solve for, 0..1
 * @return The curve parameter
 */
static float box_transition_solve(
    float x1,
    float x2,
    float t)
{
    float s = t;
    bool solved = false;
    int32_t i;
    for (i = 0; i < BOX_TRANSITION_NEWTON_STEPS && !solved; i ++) {
        float err = box_transition_bezier(x1, x2, s) - t;
        float slope = box_transition_bezier_slope(x1, x2, s);
        if (fabsf(err) < BOX_TRANSITION_EPSILON) {
            solved = true;
        } else if (fabsf(slope) < BOX_TRANSITION_EPSILON) {
            break;
        } else {
            s -= err / slope;
        }

        if (s < 0.0f || s > 1.0f) {
            break;
        }
    }

    if (!solved) {
        float lo = 0.0f, hi = 1.0f;
        s = t;
        for (i = 0; i < BOX_TRANSITION_BISECT_STEPS; i ++) {
            float x = box_transition_bezier(x1, x2, s);
            if (fabsf(x - t) < BOX_TRANSITION_EPSILON) {
                break;
            }

            if (x < t) {
                lo = s;
            } else {
                hi = s;
            }

            s = 0.5f * (lo + hi);
        }
    }

    return s;
}

/** Y of a cubic Bezier where its x equals t.
 *
 * @param x1 X of the first control point, 0..1
 * @param y1 Y of the first control point
 * @param x2 X of the second control point, 0..1
 * @param y2 Y of the second control point
 * @param t The x to solve for, 0..1
 * @return The y of the curve at t
 */
static float box_transition_cubic(
    float x1,
    float y1,
    float x2,
    float y2,
    float t)
{
    return box_transition_bezier(y1, y2, box_transition_solve(x1, x2, t));
}

float box_transition_ease(
    TimingFunction fn,
    float t)
{
    float result = t;
    if (!(t > 0.0f)) {
        result = 0.0f;
    } else if (t >= 1.0f) {
        result = 1.0f;
    } else if (fn == EaseIn) {
        result = box_transition_cubic(0.42f, 0.0f, 1.0f, 1.0f, t);
    } else if (fn == EaseOut) {
        result = box_transition_cubic(0.0f, 0.0f, 0.58f, 1.0f, t);
    } else if (fn == EaseInOut) {
        result = box_transition_cubic(0.42f, 0.0f, 0.58f, 1.0f, t);
    }

    return result;
}

bool box_transition_supported(
    const ecs_world_t *world,
    ecs_entity_t component)
{
    (void)world;

    return component != 0 && (
        component == ecs_id(Position) ||
        component == ecs_id(Rotation) ||
        component == ecs_id(Scale) ||
        component == ecs_id(Rgb) ||
        component == ecs_id(Opacity) ||
        component == ecs_id(AlphaCutoff) ||
        component == ecs_id(Tint) ||
        component == ecs_id(PbrMaterial) ||
        component == ecs_id(Emissive) ||
        component == ecs_id(TextureTransform) ||
        component == ecs_id(Transmission));
}

bool box_transition_valid(
    const ecs_world_t *world,
    ecs_entity_t component,
    const TransitionSpec *spec)
{
    return spec != NULL && box_transition_supported(world, component) &&
        spec->duration >= 0.0f && spec->delay >= 0.0f;
}

/** Get the pending maps of a world.
 *
 * @param world The world or a stage
 * @return The pending maps, NULL when the world is being deleted
 */
static box_transition_pending_t* box_transition_pending_get(
    const ecs_world_t *world)
{
    const TransitionPending *p = ecs_id(TransitionPending) && !ecs_is_fini(world)
        ? ecs_singleton_get(world, TransitionPending)
        : NULL;

    return p ? p->pending : NULL;
}

/** Whether ecs_ensure_id of Transition on a stage would put the map in a command value, so the
 * entity does not own Transition in the merged state.
 *
 * @param world The world or a stage
 * @param entity The entity
 * @return Whether the call is deferred and the entity does not own Transition
 */
static bool box_transition_deferred(
    const ecs_world_t *world,
    ecs_entity_t entity)
{
    return ecs_is_stage(world) && !ecs_owns_id(world, entity, ecs_id(Transition));
}

/** Remember that the stage holds a pending map of the entity, until the stage is flushed.
 *
 * @param world The stage
 * @param entity The entity
 */
static void box_transition_pending_add(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    box_transition_pending_t *p = box_transition_pending_get(world);
    int32_t stage = ecs_stage_get_id(world);
    bool added = false;
    if (!p || stage < 0 || stage >= BOX_TRANSITION_PENDING_STAGES) {
        goto done;
    }

    uint64_t bit = 1ull << stage;
    if (p->lock) {
        ecs_os_mutex_lock(p->lock);
    }

    uint64_t *mask = ecs_map_ensure(&p->entities, entity);
    added = !(*mask & bit);
    *mask |= bit;
    if (p->lock) {
        ecs_os_mutex_unlock(p->lock);
    }

    if (added) {
        ecs_enqueue(world, &(ecs_event_desc_t){
            .event = ecs_id(TransitionPendingFlush),
            .ids = &(ecs_type_t){
                .array = (ecs_id_t[]){ ecs_id(TransitionPending) }, .count = 1 },
            .entity = ecs_id(TransitionPending),
            .const_param = &(TransitionPendingFlush){ .entity = entity, .stage = stage }
        });
    }

done:
    return;
}

/** Whether the stage holds a pending map of the entity.
 *
 * @param world The stage
 * @param entity The entity
 * @return Whether a deferred call on the stage put the map of the entity in a command value
 */
static bool box_transition_pending_has(
    const ecs_world_t *world,
    ecs_entity_t entity)
{
    box_transition_pending_t *p = box_transition_pending_get(world);
    int32_t stage = ecs_stage_get_id(world);
    bool result = false;
    if (!p || stage < 0 || stage >= BOX_TRANSITION_PENDING_STAGES) {
        goto done;
    }

    if (p->lock) {
        ecs_os_mutex_lock(p->lock);
    }

    uint64_t *mask = ecs_map_get(&p->entities, entity);
    result = mask && (*mask & (1ull << stage));
    if (p->lock) {
        ecs_os_mutex_unlock(p->lock);
    }

done:
    return result;
}

/** Forget a pending map once its stage is flushed and the map is applied.
 *
 * @param it Iterator of the TransitionPendingFlush event, param is the event
 */
static void BoxTransitionPendingFlush(
    ecs_iter_t *it)
{
    box_transition_pending_t *p = box_transition_pending_get(it->world);
    const TransitionPendingFlush *flush = it->param;
    if (!p || !flush) {
        goto done;
    }

    if (p->lock) {
        ecs_os_mutex_lock(p->lock);
    }

    uint64_t *mask = ecs_map_get(&p->entities, flush->entity);
    if (mask) {
        *mask &= ~(1ull << flush->stage);
        if (!*mask) {
            ecs_map_remove(&p->entities, flush->entity);
        }
    }

    if (p->lock) {
        ecs_os_mutex_unlock(p->lock);
    }

done:
    return;
}

/** Free the pending maps of a TransitionPending singleton.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transition_pending_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TransitionPending *p = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (p[i].pending) {
            ecs_map_fini(&p[i].pending->entities);
            if (p[i].pending->lock) {
                ecs_os_mutex_free(p[i].pending->lock);
            }

            ecs_os_free(p[i].pending);
            p[i].pending = NULL;
        }
    }
}

/** Move a TransitionPending singleton.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_transition_pending_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

void box_transition_set(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    const TransitionSpec *spec)
{
    ecs_assert(spec != NULL, ECS_INVALID_PARAMETER, NULL);
    bool deferred = box_transition_deferred(world, entity);
    Transition *map = ecs_ensure_id(world, entity, ecs_id(Transition), sizeof(Transition));
    ecs_map_init_if(map, NULL);
    TransitionSpec *value = ecs_map_ensure_alloc_t(map, TransitionSpec, component);
    *value = *spec;
    ecs_modified_id(world, entity, ecs_id(Transition));
    if (deferred) {
        box_transition_pending_add(world, entity);
    }
}

void box_transition_remove(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    bool deferred = box_transition_deferred(world, entity);
    bool pending = deferred && box_transition_pending_has(world, entity);
    if (!pending && !box_transition_get(world, entity, component)) {
        goto done;
    }

    Transition *map = ecs_ensure_id(world, entity, ecs_id(Transition), sizeof(Transition));
    ecs_map_init_if(map, NULL);
    if (deferred && !pending) {
        box_transition_pending_add(world, entity);
    }

    void *removed = ecs_map_remove_ptr(map, component);
    if (removed) {
        ecs_os_free(removed);
        ecs_modified_id(world, entity, ecs_id(Transition));
    }

done:
    return;
}

const TransitionSpec* box_transition_get(
    const ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    const Transition *map = ecs_get_id(world, entity, ecs_id(Transition));
    const TransitionSpec *result = NULL;
    if (map && ecs_map_is_init(map)) {
        result = ecs_map_get_deref(map, TransitionSpec, component);
    }

    return result;
}

/** Log an error for every entry of a set Transition that is ignored.
 *
 * @param it Iterator over entities that own the Transition that was set
 */
static void BoxTransitionValidate(
    ecs_iter_t *it)
{
    Transition *maps = ecs_field(it, Transition, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        Transition *map = &maps[i];
        if (!ecs_map_is_init(map)) {
            continue;
        }

        ecs_map_iter_t mit = ecs_map_iter(map);
        while (ecs_map_next(&mit)) {
            ecs_entity_t component = ecs_map_key(&mit);
            const TransitionSpec *spec = ecs_map_ptr(&mit);
            if (box_transition_valid(it->world, component, spec)) {
                continue;
            }

            char *entity = ecs_get_path(it->world, it->entities[i]);
            char *name = ecs_is_valid(it->world, component)
                ? ecs_get_path(it->world, component) : NULL;
            if (!box_transition_supported(it->world, component)) {
                ecs_err("Transition of '%s': component '%s' is not transitionable, ignored",
                    entity, name ? name : "<invalid>");
            } else {
                ecs_err("Transition of '%s': component '%s' has a negative duration or delay "
                    "(%g, %g), ignored", entity, name, (double)spec->duration,
                    (double)spec->delay);
            }

            ecs_os_free(name);
            ecs_os_free(entity);
        }
    }
}

void BoxTransitionImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTransition);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxMaterial);

    ECS_META_COMPONENT(world, TimingFunction);
    ECS_META_COMPONENT(world, TransitionSpec);

    ecs_id(Transition) = ecs_map_type(world, {
        .entity = ecs_entity(world, { .name = "Transition" }),
        .key_type = ecs_id(ecs_entity_t),
        .type = ecs_id(TransitionSpec)
    });
    ecs_add_pair(world, ecs_id(Transition), EcsOnInstantiate, EcsInherit);

    ECS_COMPONENT_DEFINE(world, TransitionPendingFlush);
    ECS_COMPONENT_DEFINE(world, TransitionPending);
    ecs_set_hooks(world, TransitionPending, {
        .ctor = flecs_default_ctor,
        .dtor = box_transition_pending_dtor,
        .move = box_transition_pending_move
    });

    box_transition_pending_t *pending = ecs_os_calloc_t(box_transition_pending_t);
    ecs_map_init(&pending->entities, NULL);
    pending->lock = ecs_os_has_threading() ? ecs_os_mutex_new() : 0;
    ecs_singleton_ensure(world, TransitionPending)->pending = pending;

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxTransitionPendingFlush" }),
        .query.terms = {{ .id = ecs_id(TransitionPending), .src.id = EcsSelf }},
        .events = { ecs_id(TransitionPendingFlush) },
        .callback = BoxTransitionPendingFlush
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxTransitionValidate" }),
        .query.terms = {{ .id = ecs_id(Transition), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = BoxTransitionValidate
    });
}
