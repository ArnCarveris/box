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

/** Floats of a table that holds a curve and its area, or a gradient */
#define TABLE (BOX_PARTICLE_GRADIENT_ROWS * BOX_PARTICLE_CURVE_SAMPLES)

/** Bake a curve into row 0 of a table and its accumulated area into row 1, as the extract
 * bakes a spin curve.
 *
 * @param keys The four keys
 * @param table Receives the rows, TABLE floats
 * @return The table
 */
static const float* bake(
    const float *keys,
    float *table)
{
    box_particle_curve_bake(keys, table);
    box_particle_curve_bake_area(table, table + BOX_PARTICLE_CURVE_SAMPLES);

    return table;
}

/** Evaluate a curve from its baked row.
 *
 * @param keys The four keys
 * @param t The normalized age
 * @return The value
 */
static float curve(
    const float *keys,
    float t)
{
    float table[TABLE];

    return box_particle_curve(bake(keys, table), 0, 0, t);
}

/** Get the area under a curve from its baked rows.
 *
 * @param keys The four keys
 * @param t The normalized age
 * @return The area
 */
static float area(
    const float *keys,
    float t)
{
    float table[TABLE];

    return box_particle_curve_area(bake(keys, table), 0, 0, t);
}

/** Get the row and value of a curve the way the extract writes them in the block: no row for a
 * constant curve, else row 0 of a table it is baked into.
 *
 * @param keys The four keys
 * @param table Receives the rows of a curve that is not constant, TABLE floats
 * @param value Receives the value of a constant curve
 * @return The row
 */
static uint32_t row_of(
    const float *keys,
    float *table,
    float *value)
{
    uint32_t result = 0;
    *value = keys[0];
    if (keys[0] == keys[1] && keys[1] == keys[2] && keys[2] == keys[3]) {
        result = BOX_PARTICLE_CURVE_CONSTANT;
    } else {
        bake(keys, table);
    }

    return result;
}

/** Get the degrees a particle turned by its spin, as the draw computes them from the block.
 *
 * @param spin Spin it spawned with
 * @param keys The four keys of the spin curve
 * @param age Seconds it lived
 * @param life Seconds it lives
 * @return The degrees
 */
static float turned(
    float spin,
    const float *keys,
    float age,
    float life)
{
    float table[TABLE], value;
    uint32_t row = row_of(keys, table, &value);

    return box_particle_turned(spin, table, row, value, age, life);
}

/** Get the rotation of a particle, as the draw computes it from the block.
 *
 * @param rotation Rotation it spawned with
 * @param spin Spin it spawned with
 * @param rotation_keys Keys of the rotation curve
 * @param spin_keys Keys of the spin curve
 * @param age Seconds it lived
 * @param life Seconds it lives
 * @return The degrees
 */
static float rotation_of(
    float rotation,
    float spin,
    const float *rotation_keys,
    const float *spin_keys,
    float age,
    float life)
{
    float table[2 * TABLE], rotation_value, spin_value;
    uint32_t rotation_row = row_of(rotation_keys, table, &rotation_value);
    uint32_t spin_row = row_of(spin_keys, table + TABLE, &spin_value);
    if (spin_row != BOX_PARTICLE_CURVE_CONSTANT) {
        spin_row = TABLE / BOX_PARTICLE_CURVE_SAMPLES;
    }

    return box_particle_rotation(rotation, spin, table, rotation_row, rotation_value,
        spin_row, spin_value, age, life);
}

/** Evaluate a gradient from its baked rows.
 *
 * @param keys Four linear colors of four floats
 * @param t The normalized age
 * @param out Receives the color
 */
static void gradient(
    const float *keys,
    float t,
    float *out)
{
    float table[TABLE];
    box_particle_gradient_bake(keys, table);
    box_particle_gradient(table, 0, NULL, t, out);
}

void Curves_constant_curve(void) {
    // particle/curves
    float keys[4] = {2.5f, 2.5f, 2.5f, 2.5f}, table[TABLE], value;
    int32_t i;
    uint32_t row = row_of(keys, table, &value);
    test_uint(row, BOX_PARTICLE_CURVE_CONSTANT);
    for (i = 0; i <= 20; i ++) {
        test_flt(box_particle_curve(NULL, row, value, (float)i / 20.0f), 2.5f);
        test_flt(curve(keys, (float)i / 20.0f), 2.5f);
    }
}

void Curves_curve_at_sampled_ages(void) {
    // particle/curves
    float keys[4] = {0, 3, 1, 4};
    near(curve(keys, 0), 0);
    near(curve(keys, 1.0f / 3.0f), 3);
    near(curve(keys, 2.0f / 3.0f), 1);
    near(curve(keys, 1), 4);
    near(curve(keys, 1.0f / 6.0f), 1.5f);
    near(curve(keys, 0.5f), 2);
    near(curve(keys, 5.0f / 6.0f), 2.5f);
    near(curve(keys, 0.25f), 2.25f);
}

void Curves_curve_clamps_age(void) {
    // particle/curves
    float keys[4] = {1, 2, 3, 4};
    test_flt(curve(keys, -1), 1);
    test_flt(curve(keys, 2), 4);
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
    float c[3], white[3] = {1, 1, 1};
    gradient(keys[0], 0, c);
    near(c[0], 1);
    near(c[1], 0);
    near(c[2], 0);
    gradient(keys[0], 1.0f / 3.0f, c);
    near(c[0], 0);
    near(c[1], 1);
    near(c[2], 0);
    gradient(keys[0], 0.5f, c);
    near(c[0], 0);
    near(c[1], 0.5f);
    near(c[2], 0.5f);
    gradient(keys[0], 1, c);
    near(c[0], 1);
    near(c[1], 1);
    near(c[2], 1);
    gradient(keys[0], 5.0f / 6.0f, c);
    near(c[0], 0.5f);
    near(c[1], 0.5f);
    near(c[2], 1);
    box_particle_gradient(NULL, BOX_PARTICLE_CURVE_CONSTANT, white, 0.5f, c);
    test_flt(c[0], 1);
    test_flt(c[1], 1);
    test_flt(c[2], 1);
    test_flt(box_particle_linear(255), 1);
    test_flt(box_particle_linear(0), 0);
    near(box_particle_linear(128), 0.2158605f);
}

void Curves_spin_area_constant(void) {
    // particle/curves
    float ones[4] = {1, 1, 1, 1}, halves[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    test_flt(turned(90, ones, 0.75f, 2), 67.5f);
    test_flt(turned(90, halves, 1, 2), 45);
    near(area(ones, 1), 1);
    near(area(ones, 0.4f), 0.4f);
    near(area(halves, 1), 0.5f);
    test_flt(area(ones, 0), 0);
}

void Curves_spin_area_ramp(void) {
    // particle/curves
    float ramp[4] = {0, 1.0f / 3.0f, 2.0f / 3.0f, 1};
    float steps[4] = {0, 3, 3, 0};
    int32_t i;
    for (i = 0; i <= 12; i ++) {
        float t = (float)i / 12.0f;
        near(area(ramp, t), 0.5f * t * t);
    }

    near(area(steps, 1.0f / 3.0f), 0.5f);
    near(area(steps, 2.0f / 3.0f), 1.5f);
    near(area(steps, 1), 2);
    near(turned(100, ramp, 1, 2), 25);
    near(turned(100, ramp, 2, 2), 200 * 0.5f);
}

void Curves_spin_area_not_spin_times_age(void) {
    // particle/curves
    float down[4] = {1, 2.0f / 3.0f, 1.0f / 3.0f, 0};
    float life = 3, age = 3;
    float t = turned(60, down, age, life);
    float naive = 60 * curve(down, 1) * age;
    near(t, 90);
    test_flt(naive, 0);
    near(turned(60, down, 1.5f, life), 60 * 3 * (0.5f - 0.125f));
}

void Curves_rotation_adds_turns(void) {
    // particle/curves
    float none[4] = {0, 0, 0, 0}, ones[4] = {1, 1, 1, 1};
    float quarter[4] = {0.25f, 0.25f, 0.25f, 0.25f}, spin_up[4] = {0, 0.5f, 1, 1.5f};
    test_flt(rotation_of(30, 0, none, ones, 1, 2), 30);
    test_flt(rotation_of(30, 10, none, ones, 1, 2), 40);
    test_flt(rotation_of(30, 0, quarter, ones, 1, 2), 120);
    near(rotation_of(0, 0, spin_up, ones, 1, 2), 270);
    near(rotation_of(10, 20, quarter, ones, 0.5f, 2), 10 + 90 + 10);
}

void Curves_row_holds_keys(void) {
    // particle/curves
    float keys[4] = {0.25f, 3.1f, -1.7f, 4.3f}, row[BOX_PARTICLE_CURVE_SAMPLES];
    float colors[4][4] = {
        {0.1f, 0.2f, 0.3f, 1}, {0.4f, 0.5f, 0.6f, 1}, {0.7f, 0.8f, 0.9f, 1}, {1, 0, 0.5f, 1}
    };
    float g[TABLE];
    int32_t i, c;
    test_int(BOX_PARTICLE_CURVE_SAMPLES, 64);
    box_particle_curve_bake(keys, row);
    test_assert(row[0] == keys[0]);
    test_assert(row[21] == keys[1]);
    test_assert(row[42] == keys[2]);
    test_assert(row[63] == keys[3]);
    near(row[7], keys[0] + (keys[1] - keys[0]) / 3.0f);
    near(row[49], keys[2] + (keys[3] - keys[2]) / 3.0f);

    box_particle_gradient_bake(colors[0], g);
    for (i = 0; i < 4; i ++) {
        for (c = 0; c < 3; c ++) {
            test_assert(g[i * 21 * 4 + c] == colors[i][c]);
        }

        test_flt(g[i * 21 * 4 + 3], 1);
    }
}

void Curves_area_row_accumulates(void) {
    // particle/curves
    float keys[4] = {0, 3, 3, 0}, table[TABLE];
    const float *v = bake(keys, table), *a = table + BOX_PARTICLE_CURVE_SAMPLES;
    int32_t i;
    test_flt(a[0], 0);
    for (i = 1; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        near(a[i], a[i - 1] + 0.5f * (v[i - 1] + v[i]) / 63.0f);
    }

    near(a[21], 0.5f);
    near(a[42], 1.5f);
    near(a[63], 2);
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
