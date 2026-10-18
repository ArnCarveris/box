/**
 * @file Curves.c
 * @brief Tests for the changes of a particle over its life: the curves and the gradient at
 * sampled ages, the area under the spin curve and the ParticleCurves values.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "simulate.h"

/** Tolerance of the float comparisons */
#define EPS (1e-5f)

/** Check that two floats are within EPS of each other, relative to their size.
 *
 * @param a The value
 * @param b The expected value
 */
static void near(
    float a,
    float b)
{
    test_assert(fabsf(a - b) <= EPS * fmaxf(1.0f, fabsf(b)));
}

/** Check that a curve holds the values of the ctor of ParticleCurves.
 *
 * @param c The curve
 * @param value The value of every key
 */
static void check_curve(
    const ParticleCurve *c,
    float value)
{
    test_flt(c->start, value);
    test_flt(c->early, value);
    test_flt(c->late, value);
    test_flt(c->end, value);
}

/** Check that a color is white.
 *
 * @param c The color
 */
static void check_white(
    const rgb *c)
{
    test_int(c->r, 255);
    test_int(c->g, 255);
    test_int(c->b, 255);
}

/** Check the ctor values of ParticleCurves: multipliers of 1, no turns and a white gradient.
 *
 * @param c The curves
 */
static void check_defaults(
    const ParticleCurves *c)
{
    check_curve(&c->size, 1);
    check_curve(&c->opacity, 1);
    check_curve(&c->rotation, 0);
    check_curve(&c->spin, 1);
    check_white(&c->color.start);
    check_white(&c->color.early);
    check_white(&c->color.late);
    check_white(&c->color.end);
}

void Curves_constant_curve(void) {
    // particle/curves
    float keys[4] = {2.5f, 2.5f, 2.5f, 2.5f};
    int32_t i;
    for (i = 0; i <= 20; i ++) {
        test_flt(box_particle_curve(keys, (float)i / 20.0f), 2.5f);
    }
}

void Curves_curve_at_sampled_ages(void) {
    // particle/curves
    float keys[4] = {0, 3, 1, 4};
    near(box_particle_curve(keys, 0), 0);
    near(box_particle_curve(keys, 1.0f / 3.0f), 3);
    near(box_particle_curve(keys, 2.0f / 3.0f), 1);
    near(box_particle_curve(keys, 1), 4);
    near(box_particle_curve(keys, 1.0f / 6.0f), 1.5f);
    near(box_particle_curve(keys, 0.5f), 2);
    near(box_particle_curve(keys, 5.0f / 6.0f), 2.5f);
    near(box_particle_curve(keys, 0.25f), 2.25f);
}

void Curves_curve_clamps_age(void) {
    // particle/curves
    float keys[4] = {1, 2, 3, 4};
    test_flt(box_particle_curve(keys, -1), 1);
    test_flt(box_particle_curve(keys, 2), 4);
    test_flt(box_particle_norm_age(3, 2), 1);
    test_flt(box_particle_norm_age(-1, 2), 0);
    test_flt(box_particle_norm_age(1, 0), 0);
    test_flt(box_particle_norm_age(0.5f, 2), 0.25f);
}

void Curves_gradient_at_sampled_ages(void) {
    // particle/curves
    float keys[4][4] = {
        {1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}
    };
    float c[3];
    box_particle_gradient(keys[0], 0, c);
    near(c[0], 1);
    near(c[1], 0);
    near(c[2], 0);
    box_particle_gradient(keys[0], 1.0f / 3.0f, c);
    near(c[0], 0);
    near(c[1], 1);
    near(c[2], 0);
    box_particle_gradient(keys[0], 0.5f, c);
    near(c[0], 0);
    near(c[1], 0.5f);
    near(c[2], 0.5f);
    box_particle_gradient(keys[0], 1, c);
    near(c[0], 1);
    near(c[1], 1);
    near(c[2], 1);
    box_particle_gradient(keys[0], 5.0f / 6.0f, c);
    near(c[0], 0.5f);
    near(c[1], 0.5f);
    near(c[2], 1);
    test_flt(box_particle_linear(255), 1);
    test_flt(box_particle_linear(0), 0);
    near(box_particle_linear(128), 0.2158605f);
}

void Curves_spin_area_constant(void) {
    // particle/curves
    float ones[4] = {1, 1, 1, 1}, halves[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    test_flt(box_particle_turned(90, ones, 0.75f, 2), 67.5f);
    test_flt(box_particle_turned(90, halves, 1, 2), 45);
    near(box_particle_curve_area(ones, 1), 1);
    near(box_particle_curve_area(ones, 0.4f), 0.4f);
    near(box_particle_curve_area(halves, 1), 0.5f);
    test_flt(box_particle_curve_area(ones, 0), 0);
}

void Curves_spin_area_ramp(void) {
    // particle/curves
    float ramp[4] = {0, 1.0f / 3.0f, 2.0f / 3.0f, 1};
    float steps[4] = {0, 3, 3, 0};
    int32_t i;
    for (i = 0; i <= 12; i ++) {
        float t = (float)i / 12.0f;
        near(box_particle_curve_area(ramp, t), 0.5f * t * t);
    }

    near(box_particle_curve_area(steps, 1.0f / 3.0f), 0.5f);
    near(box_particle_curve_area(steps, 2.0f / 3.0f), 1.5f);
    near(box_particle_curve_area(steps, 1), 2);
    near(box_particle_turned(100, ramp, 1, 2), 25);
    near(box_particle_turned(100, ramp, 2, 2), 200 * 0.5f);
}

void Curves_spin_area_not_spin_times_age(void) {
    // particle/curves
    float down[4] = {1, 2.0f / 3.0f, 1.0f / 3.0f, 0};
    float life = 3, age = 3;
    float turned = box_particle_turned(60, down, age, life);
    float naive = 60 * box_particle_curve(down, 1) * age;
    near(turned, 90);
    test_flt(naive, 0);
    near(box_particle_turned(60, down, 1.5f, life), 60 * 3 * (0.5f - 0.125f));
}

void Curves_rotation_adds_turns(void) {
    // particle/curves
    float none[4] = {0, 0, 0, 0}, ones[4] = {1, 1, 1, 1};
    float quarter[4] = {0.25f, 0.25f, 0.25f, 0.25f}, spin_up[4] = {0, 0.5f, 1, 1.5f};
    test_flt(box_particle_rotation(30, 0, none, ones, 1, 2), 30);
    test_flt(box_particle_rotation(30, 10, none, ones, 1, 2), 40);
    test_flt(box_particle_rotation(30, 0, quarter, ones, 1, 2), 120);
    near(box_particle_rotation(0, 0, spin_up, ones, 1, 2), 270);
    near(box_particle_rotation(10, 20, quarter, ones, 0.5f, 2), 10 + 90 + 10);
}

void Curves_c_defaults(void) {
    // particle/curves
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleCurves);
    const ParticleCurves *c = ecs_get(world, e, ParticleCurves);
    test_assert(c != NULL);
    check_defaults(c);

    ecs_fini(world);
}

void Curves_script_defaults(void) {
    // particle/curves
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "curves",
        "using box.particle\n"
        "plain { ParticleCurves: {} }\n"
        "custom { ParticleCurves: {size: {1, 2, 3, 4}, rotation: {0, 0.5, 1, 2}, "
        "color: {start: {255, 0, 0}, end: {0, 0, 255}}, spin: {2, 2, 0, 0}} }\n", NULL) == 0);

    const ParticleCurves *c = ecs_get(world, ecs_lookup(world, "plain"), ParticleCurves);
    test_assert(c != NULL);
    check_defaults(c);

    c = ecs_get(world, ecs_lookup(world, "custom"), ParticleCurves);
    test_assert(c != NULL);
    test_flt(c->size.start, 1);
    test_flt(c->size.end, 4);
    check_curve(&c->opacity, 1);
    test_flt(c->rotation.late, 1);
    test_flt(c->rotation.end, 2);
    test_int(c->color.start.r, 255);
    test_int(c->color.start.b, 0);
    check_white(&c->color.early);
    check_white(&c->color.late);
    test_int(c->color.end.r, 0);
    test_int(c->color.end.b, 255);
    test_flt(c->spin.early, 2);
    test_flt(c->spin.late, 0);

    ecs_fini(world);
}
