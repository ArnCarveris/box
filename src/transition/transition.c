/**
 * @file transition/transition.c
 * @brief Transition component, timing functions and validation of the entries.
 */

#define BOX_TRANSITION_IMPL
#include <box.h>

/* A runtime map type, so that flecs installs the map hooks that copy and free the entries and
 * the script parser accepts Transition: [Position: {...}] */
ECS_COMPONENT_DECLARE(Transition);

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
        component == ecs_id(TextureTransform));
}

bool box_transition_valid(
    const ecs_world_t *world,
    ecs_entity_t component,
    const TransitionSpec *spec)
{
    return spec != NULL && box_transition_supported(world, component) &&
        spec->duration >= 0.0f && spec->delay >= 0.0f;
}

void box_transition_set(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    const TransitionSpec *spec)
{
    ecs_assert(spec != NULL, ECS_INVALID_PARAMETER, NULL);
    Transition *map = ecs_ensure_id(world, entity, ecs_id(Transition), sizeof(Transition));
    ecs_map_init_if(map, NULL);
    TransitionSpec *value = ecs_map_ensure_alloc_t(map, TransitionSpec, component);
    *value = *spec;
    ecs_modified_id(world, entity, ecs_id(Transition));
}

void box_transition_remove(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    if (!box_transition_get(world, entity, component)) {
        goto done;
    }

    Transition *map = ecs_ensure_id(world, entity, ecs_id(Transition), sizeof(Transition));
    ecs_map_init_if(map, NULL);
    ecs_os_free(ecs_map_remove_ptr(map, component));
    ecs_modified_id(world, entity, ecs_id(Transition));
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

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxTransitionValidate" }),
        .query.terms = {{ .id = ecs_id(Transition), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = BoxTransitionValidate
    });
}
