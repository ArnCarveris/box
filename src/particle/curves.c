/**
 * @file particle/curves.c
 * @brief Checks of the keyed curves of ParticleCurves, ParticleMotion, ParticleLight and
 * ParticleTimeline and their validate hooks.
 */

#include "curves.h"
#include <math.h>

/** Check one list of keys of a ParticleCurve: a valid Curve whose times are from 0 to 1.
 *
 * @param kind The kind of the curve
 * @param keys The keys
 * @return NULL when it is valid, else the first problem naming the key
 */
static char* box_particle_curve_keys_check(
    CurveKind kind,
    const ecs_vec_t *keys)
{
    Curve view = { .kind = kind, .keys = *keys };
    const CurveKey *k = ecs_vec_first_t(keys, CurveKey);
    int32_t i, count = ecs_vec_count(keys);
    char *result = box_curve_validate(&view);
    for (i = 0; i < count && !result; i ++) {
        if (k[i].time < 0.0f || k[i].time > 1.0f) {
            result = flecs_asprintf("key %d: time %g is outside of 0 to 1",
                (int)i, (double)k[i].time);
        }
    }

    return result;
}

/** Name the second list in the problem of its keys.
 *
 * @param problem The problem of max_keys, NULL for none, freed
 * @return The problem starting with max_keys, NULL for none
 */
static char* box_particle_curve_max_problem(
    char *problem)
{
    char *result = NULL;
    if (problem) {
        result = flecs_asprintf("max_keys %s", problem);
        ecs_os_free(problem);
    }

    return result;
}

char* box_particle_curve_check(
    const ParticleCurve *curve)
{
    char *result = box_particle_curve_keys_check(curve->kind, &curve->keys);
    if (!result && ecs_vec_count(&curve->max_keys)) {
        result = box_particle_curve_max_problem(
            box_particle_curve_keys_check(curve->kind, &curve->max_keys));
    }

    return result;
}

/** Check a ParticleCurve of an owner without a random draw: a valid ParticleCurve without
 * max_keys.
 *
 * @param curve The curve
 * @param owner What has no random draw, for the problem
 * @return NULL when it is valid, else the first problem
 */
static char* box_particle_single_curve_check(
    const ParticleCurve *curve,
    const char *owner)
{
    char *result = NULL;
    if (ecs_vec_count(&curve->max_keys)) {
        result = flecs_asprintf(
            "max_keys is not allowed, %s has no random draw to blend two curves by", owner);
    } else {
        result = box_particle_curve_check(curve);
    }

    return result;
}

char* box_particle_light_curve_check(
    const ParticleCurve *curve)
{
    return box_particle_single_curve_check(curve, "a light");
}

char* box_particle_timeline_curve_check(
    const ParticleCurve *curve)
{
    return box_particle_single_curve_check(curve, "the playback clock");
}

/** Check one list of keys of a ParticleGradient: the times are finite, increase strictly and
 * are from 0 to 1.
 *
 * @param keys The keys
 * @return NULL when it is valid, else the first problem naming the key
 */
static char* box_particle_gradient_keys_check(
    const ecs_vec_t *keys)
{
    const ParticleGradientKey *k = ecs_vec_first_t(keys, ParticleGradientKey);
    int32_t i, count = ecs_vec_count(keys);
    char *result = NULL;
    for (i = 0; i < count && !result; i ++) {
        float time = k[i].time;
        if (!isfinite(time)) {
            result = flecs_asprintf("key %d: time is not finite", (int)i);
        } else if (i > 0 && !(time > k[i - 1].time)) {
            result = flecs_asprintf("key %d: time %g is not after time %g of key %d",
                (int)i, (double)time, (double)k[i - 1].time, (int)(i - 1));
        } else if (time < 0.0f || time > 1.0f) {
            result = flecs_asprintf("key %d: time %g is outside of 0 to 1", (int)i, (double)time);
        }
    }

    return result;
}

char* box_particle_gradient_check(
    const ParticleGradient *gradient)
{
    char *result = NULL;
    if (gradient->kind < CurveLinear || gradient->kind > CurveBezier) {
        result = flecs_asprintf(
            "kind %d is not CurveLinear, CurveCatmullRom or CurveBezier", (int)gradient->kind);
        goto done;
    }

    result = box_particle_gradient_keys_check(&gradient->keys);
    if (!result && ecs_vec_count(&gradient->max_keys)) {
        result = box_particle_curve_max_problem(
            box_particle_gradient_keys_check(&gradient->max_keys));
    }

done:
    return result;
}

bool box_particle_curve_valid(
    const ParticleCurve *curve)
{
    char *problem = box_particle_curve_check(curve);
    bool result = problem == NULL;
    ecs_os_free(problem);

    return result;
}

bool box_particle_light_curve_valid(
    const ParticleCurve *curve)
{
    char *problem = box_particle_light_curve_check(curve);
    bool result = problem == NULL;
    ecs_os_free(problem);

    return result;
}

bool box_particle_timeline_curve_valid(
    const ParticleCurve *curve)
{
    char *problem = box_particle_timeline_curve_check(curve);
    bool result = problem == NULL;
    ecs_os_free(problem);

    return result;
}

bool box_particle_gradient_valid(
    const ParticleGradient *gradient)
{
    char *problem = box_particle_gradient_check(gradient);
    bool result = problem == NULL;
    ecs_os_free(problem);

    return result;
}

/** Log the problem of a curve of a component and free it.
 *
 * @param world The world
 * @param entity The entity with the component
 * @param component Name of the component
 * @param field Name of the field that holds the curve
 * @param problem The problem, NULL to log nothing
 */
static void box_particle_curves_report(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *component,
    const char *field,
    char *problem)
{
    if (problem) {
        char *path = ecs_get_path(world, entity);
        ecs_err("invalid %s on %s: %s %s, drawn as the default of %s", component, path, field,
            problem, field);
        ecs_os_free(path);
        ecs_os_free(problem);
    }
}

/** Validate hook for ParticleCurves.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_particle_curves_validate(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const ParticleCurves *c = ptr;
    box_particle_curves_report(world, entity, "ParticleCurves", "size",
        box_particle_curve_check(&c->size));
    box_particle_curves_report(world, entity, "ParticleCurves", "opacity",
        box_particle_curve_check(&c->opacity));
    box_particle_curves_report(world, entity, "ParticleCurves", "rotation",
        box_particle_curve_check(&c->rotation));
    box_particle_curves_report(world, entity, "ParticleCurves", "color",
        box_particle_gradient_check(&c->color));
    box_particle_curves_report(world, entity, "ParticleCurves", "spin",
        box_particle_curve_check(&c->spin));

    return true;
}

/** Validate hook for ParticleMotion.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_particle_motion_validate(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const ParticleMotion *m = ptr;
    const ParticleCurve *curves[8] = {
        &m->velocity_x, &m->velocity_y, &m->velocity_z,
        &m->force_x, &m->force_y, &m->force_z, &m->speed, &m->drag
    };
    static const char *fields[8] = {
        "velocity_x", "velocity_y", "velocity_z", "force_x", "force_y", "force_z", "speed",
        "drag"
    };
    int32_t i;
    for (i = 0; i < 8; i ++) {
        box_particle_curves_report(world, entity, "ParticleMotion", fields[i],
            box_particle_curve_check(curves[i]));
    }

    return true;
}

/** Check a curve that spans period seconds of the playback clock: a curve with keys needs a
 * period above 0, as period 0 means no curve.
 *
 * @param curve The curve
 * @param period The period of its component
 * @param check The check of the curve itself
 * @return NULL when it is valid, else the first problem
 */
static char* box_particle_clock_curve_check(
    const ParticleCurve *curve,
    float period,
    char* (*check)(const ParticleCurve*))
{
    char *result = check(curve);
    if (!result && ecs_vec_count(&curve->keys) && !(period > 0.0f)) {
        result = flecs_asprintf("has keys but period is %g, set the seconds it spans with a "
            "period above 0", (double)period);
    }

    return result;
}

/** Validate hook for ParticleLight.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_particle_light_validate(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const ParticleLight *light = ptr;
    box_particle_curves_report(world, entity, "ParticleLight", "curve",
        box_particle_clock_curve_check(&light->curve, light->period,
            box_particle_light_curve_check));

    return true;
}

/** Validate hook for ParticleTimeline.
 *
 * @param world The world
 * @param entity The entity being validated
 * @param ptr Pointer to the component value
 * @return Always true; problems are only logged
 */
static bool box_particle_timeline_validate(
    ecs_world_t *world,
    ecs_entity_t entity,
    void *ptr)
{
    const ParticleTimeline *timeline = ptr;
    box_particle_curves_report(world, entity, "ParticleTimeline", "rate",
        box_particle_clock_curve_check(&timeline->rate, timeline->period,
            box_particle_timeline_curve_check));
    box_particle_curves_report(world, entity, "ParticleTimeline", "speed",
        box_particle_clock_curve_check(&timeline->speed, timeline->period,
            box_particle_timeline_curve_check));

    return true;
}

void box_particle_curves_import(
    ecs_world_t *world)
{
    ecs_set_hooks(world, ParticleCurves, { .on_validate = box_particle_curves_validate });
    ecs_set_hooks(world, ParticleMotion, { .on_validate = box_particle_motion_validate });
    ecs_set_hooks(world, ParticleLight, { .on_validate = box_particle_light_validate });
    ecs_set_hooks(world, ParticleTimeline, { .on_validate = box_particle_timeline_validate });
}
