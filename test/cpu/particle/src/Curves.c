/**
 * @file Curves.c
 * @brief Tests for the changes of a particle over its life: the curves and the gradient at
 * sampled ages, the area under the spin curve and the ParticleCurves values, with the four
 * evenly spaced keys of the old format written as keys at 0, 1/3, 2/3 and 1.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "simulate.h"

/** Tolerance of the float comparisons */
#define EPS (1e-5f)

/** A linear ParticleCurve with keys at 0, 1/3, 2/3 and 1, as the migration writes four keys */
#define FOUR(a, b, c, d) ((ParticleCurve){ CurveLinear, BOX_CURVE_KEYS( \
    {0, (a)}, {1.0f / 3.0f, (b)}, {2.0f / 3.0f, (c)}, {1, (d)}) })

/** A linear ParticleGradient with the four colors at 0, 1/3, 2/3 and 1 */
#define GRADIENT4(a, b, c, d) ((ParticleGradient){ CurveLinear, BOX_VEC_VIEW( \
    ParticleGradientKey, {0, a}, {1.0f / 3.0f, b}, {2.0f / 3.0f, c}, {1, d}) })

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

/** Check that a curve has no keys, the default of its field.
 *
 * @param c The curve
 */
static void check_empty(
    const ParticleCurve *c)
{
    test_int(c->kind, CurveLinear);
    test_int(ecs_vec_count(&c->keys), 0);
}

/** Check the values of ParticleCurves that change nothing: no keys in any curve.
 *
 * @param c The curves
 */
static void check_defaults(
    const ParticleCurves *c)
{
    check_empty(&c->size);
    check_empty(&c->opacity);
    check_empty(&c->rotation);
    check_empty(&c->spin);
    test_int(c->color.kind, CurveLinear);
    test_int(ecs_vec_count(&c->color.keys), 0);
}

/** Check a key of a curve.
 *
 * @param c The curve
 * @param i Index of the key
 * @param time The expected time
 * @param value The expected value
 */
static void check_key(
    const ParticleCurve *c,
    int32_t i,
    float time,
    float value)
{
    test_assert(i < ecs_vec_count(&c->keys));
    const CurveKey *k = ecs_vec_get_t(&c->keys, CurveKey, i);
    test_flt(k->time, time);
    test_flt(k->value, value);
}

/** Check a key of a gradient.
 *
 * @param g The gradient
 * @param i Index of the key
 * @param time The expected time
 * @param r The expected red
 * @param gr The expected green
 * @param b The expected blue
 */
static void check_color_key(
    const ParticleGradient *g,
    int32_t i,
    float time,
    uint8_t r,
    uint8_t gr,
    uint8_t b)
{
    test_assert(i < ecs_vec_count(&g->keys));
    const ParticleGradientKey *k = ecs_vec_get_t(&g->keys, ParticleGradientKey, i);
    test_flt(k->time, time);
    test_int(k->color.r, r);
    test_int(k->color.g, gr);
    test_int(k->color.b, b);
}

/** Floats of a table that holds a curve and its area, or a gradient */
#define TABLE (BOX_PARTICLE_GRADIENT_ROWS * BOX_PARTICLE_CURVE_SAMPLES)

/** Bake a curve into row 0 of a table and its accumulated area into row 1, as the extract
 * bakes a spin curve.
 *
 * @param c The curve
 * @param table Receives the rows, TABLE floats
 * @return The table
 */
static const float* bake(
    const ParticleCurve *c,
    float *table)
{
    box_particle_curve_bake(c, 1, table);
    box_particle_curve_bake_area(table, table + BOX_PARTICLE_CURVE_SAMPLES);

    return table;
}

/** Evaluate a curve from its baked row.
 *
 * @param c The curve
 * @param t The normalized age
 * @return The value
 */
static float curve(
    const ParticleCurve *c,
    float t)
{
    float table[TABLE];

    return box_particle_curve(bake(c, table), 0, 0, t);
}

/** Get the area under a curve from its baked rows.
 *
 * @param c The curve
 * @param t The normalized age
 * @return The area
 */
static float area(
    const ParticleCurve *c,
    float t)
{
    float table[TABLE];

    return box_particle_curve_area(bake(c, table), 0, 0, t);
}

/** Get the row and value of a curve the way the extract writes them in the block: no row for a
 * curve whose samples are all equal, else row 0 of a table it is baked into.
 *
 * @param c The curve
 * @param table Receives the rows of a curve that is not constant, TABLE floats
 * @param value Receives the value of a constant curve
 * @return The row
 */
static uint32_t row_of(
    const ParticleCurve *c,
    float *table,
    float *value)
{
    uint32_t result = BOX_PARTICLE_CURVE_CONSTANT;
    int32_t i;
    bake(c, table);
    *value = table[0];
    for (i = 1; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        if (table[i] != table[0]) {
            result = 0;
        }
    }

    return result;
}

/** Get the degrees a particle turned by its spin, as the draw computes them from the block.
 *
 * @param spin Spin it spawned with
 * @param c The spin curve
 * @param age Seconds it lived
 * @param life Seconds it lives
 * @return The degrees
 */
static float turned(
    float spin,
    const ParticleCurve *c,
    float age,
    float life)
{
    float table[TABLE], value;
    uint32_t row = row_of(c, table, &value);

    return box_particle_turned(spin, table, row, BOX_PARTICLE_CURVE_CONSTANT, value, age, life,
        0);
}

/** Get the rotation of a particle, as the draw computes it from the block.
 *
 * @param rotation Rotation it spawned with
 * @param spin Spin it spawned with
 * @param rotation_curve The rotation curve
 * @param spin_curve The spin curve
 * @param age Seconds it lived
 * @param life Seconds it lives
 * @return The degrees
 */
static float rotation_of(
    float rotation,
    float spin,
    const ParticleCurve *rotation_curve,
    const ParticleCurve *spin_curve,
    float age,
    float life)
{
    float table[2 * TABLE], rotation_value, spin_value;
    uint32_t rotation_row = row_of(rotation_curve, table, &rotation_value);
    uint32_t spin_row = row_of(spin_curve, table + TABLE, &spin_value);
    if (spin_row != BOX_PARTICLE_CURVE_CONSTANT) {
        spin_row = TABLE / BOX_PARTICLE_CURVE_SAMPLES;
    }

    return box_particle_rotation(rotation, spin, table, rotation_row,
        BOX_PARTICLE_CURVE_CONSTANT, rotation_value, 0, spin_row, BOX_PARTICLE_CURVE_CONSTANT,
        spin_value, 0, age, life);
}

/** Evaluate a gradient from its baked rows.
 *
 * @param g The gradient
 * @param t The normalized age
 * @param out Receives the color
 */
static void gradient(
    const ParticleGradient *g,
    float t,
    float *out)
{
    float table[TABLE];
    box_particle_gradient_bake(g, table);
    box_particle_gradient(table, 0, NULL, t, out);
}

void Curves_constant_curve(void) {
    // particle/curves
    ParticleCurve one = { CurveLinear, BOX_CURVE_KEYS({0, 2.5f}) };
    ParticleCurve four = FOUR(2.5f, 2.5f, 2.5f, 2.5f);
    float table[TABLE], value;
    int32_t i;
    uint32_t row = row_of(&one, table, &value);
    test_uint(row, BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(value, 2.5f);
    test_uint(row_of(&four, table, &value), BOX_PARTICLE_CURVE_CONSTANT);
    for (i = 0; i <= 20; i ++) {
        test_flt(box_particle_curve(NULL, row, value, (float)i / 20.0f), 2.5f);
        test_flt(curve(&one, (float)i / 20.0f), 2.5f);
        test_flt(curve(&four, (float)i / 20.0f), 2.5f);
    }
}

void Curves_curve_at_sampled_ages(void) {
    // particle/curves
    ParticleCurve keys = FOUR(0, 3, 1, 4);
    near(curve(&keys, 0), 0);
    near(curve(&keys, 1.0f / 3.0f), 3);
    near(curve(&keys, 2.0f / 3.0f), 1);
    near(curve(&keys, 1), 4);
    near(curve(&keys, 1.0f / 6.0f), 1.5f);
    near(curve(&keys, 0.5f), 2);
    near(curve(&keys, 5.0f / 6.0f), 2.5f);
    near(curve(&keys, 0.25f), 2.25f);
}

void Curves_curve_clamps_age(void) {
    // particle/curves
    ParticleCurve keys = FOUR(1, 2, 3, 4);
    test_flt(curve(&keys, -1), 1);
    test_flt(curve(&keys, 2), 4);
    test_flt(box_particle_norm_age(3, 2), 1);
    test_flt(box_particle_norm_age(-1, 2), 0);
    test_flt(box_particle_norm_age(1, 0), 0);
    test_flt(box_particle_norm_age(0.5f, 2), 0.25f);
}

void Curves_gradient_at_sampled_ages(void) {
    // particle/curves
    rgb red = {255, 0, 0}, green = {0, 255, 0}, blue = {0, 0, 255}, white8 = {255, 255, 255};
    ParticleGradient keys = GRADIENT4(red, green, blue, white8);
    float c[3], white[3] = {1, 1, 1};
    gradient(&keys, 0, c);
    near(c[0], 1);
    near(c[1], 0);
    near(c[2], 0);
    gradient(&keys, 1.0f / 3.0f, c);
    near(c[0], 0);
    near(c[1], 1);
    near(c[2], 0);
    gradient(&keys, 0.5f, c);
    near(c[0], 0);
    near(c[1], 0.5f);
    near(c[2], 0.5f);
    gradient(&keys, 1, c);
    near(c[0], 1);
    near(c[1], 1);
    near(c[2], 1);
    gradient(&keys, 5.0f / 6.0f, c);
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
    ParticleCurve ones = FOUR(1, 1, 1, 1), halves = FOUR(0.5f, 0.5f, 0.5f, 0.5f);
    test_flt(turned(90, &ones, 0.75f, 2), 67.5f);
    test_flt(turned(90, &halves, 1, 2), 45);
    near(area(&ones, 1), 1);
    near(area(&ones, 0.4f), 0.4f);
    near(area(&halves, 1), 0.5f);
    test_flt(area(&ones, 0), 0);
}

void Curves_spin_area_ramp(void) {
    // particle/curves
    ParticleCurve ramp = FOUR(0, 1.0f / 3.0f, 2.0f / 3.0f, 1);
    ParticleCurve steps = FOUR(0, 3, 3, 0);
    int32_t i;
    for (i = 0; i <= 12; i ++) {
        float t = (float)i / 12.0f;
        near(area(&ramp, t), 0.5f * t * t);
    }

    near(area(&steps, 1.0f / 3.0f), 0.5f);
    near(area(&steps, 2.0f / 3.0f), 1.5f);
    near(area(&steps, 1), 2);
    near(turned(100, &ramp, 1, 2), 25);
    near(turned(100, &ramp, 2, 2), 200 * 0.5f);
}

void Curves_spin_area_not_spin_times_age(void) {
    // particle/curves
    ParticleCurve down = FOUR(1, 2.0f / 3.0f, 1.0f / 3.0f, 0);
    float life = 3, age = 3;
    float t = turned(60, &down, age, life);
    float naive = 60 * curve(&down, 1) * age;
    near(t, 90);
    test_flt(naive, 0);
    near(turned(60, &down, 1.5f, life), 60 * 3 * (0.5f - 0.125f));
}

void Curves_rotation_adds_turns(void) {
    // particle/curves
    ParticleCurve none = FOUR(0, 0, 0, 0), ones = FOUR(1, 1, 1, 1);
    ParticleCurve quarter = FOUR(0.25f, 0.25f, 0.25f, 0.25f);
    ParticleCurve spin_up = FOUR(0, 0.5f, 1, 1.5f);
    test_flt(rotation_of(30, 0, &none, &ones, 1, 2), 30);
    test_flt(rotation_of(30, 10, &none, &ones, 1, 2), 40);
    test_flt(rotation_of(30, 0, &quarter, &ones, 1, 2), 120);
    near(rotation_of(0, 0, &spin_up, &ones, 1, 2), 270);
    near(rotation_of(10, 20, &quarter, &ones, 0.5f, 2), 10 + 90 + 10);
}

void Curves_row_holds_keys(void) {
    // particle/curves
    float keys[4] = {0.25f, 3.1f, -1.7f, 4.3f}, row[BOX_PARTICLE_CURVE_SAMPLES];
    rgb colors[4] = {{26, 51, 77}, {102, 128, 153}, {179, 204, 230}, {255, 0, 128}};
    ParticleCurve c4 = FOUR(keys[0], keys[1], keys[2], keys[3]);
    ParticleGradient g4 = GRADIENT4(colors[0], colors[1], colors[2], colors[3]);
    float g[TABLE];
    int32_t i;
    test_int(BOX_PARTICLE_CURVE_SAMPLES, 64);
    box_particle_curve_bake(&c4, 1, row);
    test_assert(row[0] == keys[0]);
    test_assert(row[21] == keys[1]);
    test_assert(row[42] == keys[2]);
    test_assert(row[63] == keys[3]);
    near(row[7], keys[0] + (keys[1] - keys[0]) / 3.0f);
    near(row[49], keys[2] + (keys[3] - keys[2]) / 3.0f);

    box_particle_gradient_bake(&g4, g);
    for (i = 0; i < 4; i ++) {
        test_assert(g[i * 21 * 4 + 0] == box_particle_linear(colors[i].r));
        test_assert(g[i * 21 * 4 + 1] == box_particle_linear(colors[i].g));
        test_assert(g[i * 21 * 4 + 2] == box_particle_linear(colors[i].b));
        test_flt(g[i * 21 * 4 + 3], 1);
    }
}

void Curves_area_row_accumulates(void) {
    // particle/curves
    ParticleCurve keys = FOUR(0, 3, 3, 0);
    float table[TABLE];
    const float *v = bake(&keys, table), *a = table + BOX_PARTICLE_CURVE_SAMPLES;
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
        "plain { Curves: {} }\n"
        "custom { Curves: {\n"
        "  size: {keys: [{0, 1}, {0.33333334, 2}, {0.6666667, 3}, {1, 4}]}\n"
        "  rotation: {keys: [{0, 0}, {0.33333334, 0.5}, {0.6666667, 1}, {1, 2}]}\n"
        "  color: {keys: [{0, {255, 0, 0}}, {0.33333334, {255, 255, 255}}, "
        "{0.6666667, {255, 255, 255}}, {1, {0, 0, 255}}]}\n"
        "  spin: {keys: [{0, 2}, {0.33333334, 2}, {0.6666667, 0}, {1, 0}]}\n"
        "} }\n", NULL) == 0);

    const ParticleCurves *c = ecs_get(world, ecs_lookup(world, "plain"), ParticleCurves);
    test_assert(c != NULL);
    check_defaults(c);

    c = ecs_get(world, ecs_lookup(world, "custom"), ParticleCurves);
    test_assert(c != NULL);
    test_int(ecs_vec_count(&c->size.keys), 4);
    check_key(&c->size, 0, 0, 1);
    check_key(&c->size, 3, 1, 4);
    test_assert(ecs_vec_get_t(&c->size.keys, CurveKey, 1)->time == 1.0f / 3.0f);
    test_assert(ecs_vec_get_t(&c->size.keys, CurveKey, 2)->time == 2.0f / 3.0f);
    check_empty(&c->opacity);
    check_key(&c->rotation, 2, 2.0f / 3.0f, 1);
    check_key(&c->rotation, 3, 1, 2);
    test_int(ecs_vec_count(&c->color.keys), 4);
    check_color_key(&c->color, 0, 0, 255, 0, 0);
    check_color_key(&c->color, 1, 1.0f / 3.0f, 255, 255, 255);
    check_color_key(&c->color, 2, 2.0f / 3.0f, 255, 255, 255);
    check_color_key(&c->color, 3, 1, 0, 0, 255);
    check_key(&c->spin, 1, 1.0f / 3.0f, 2);
    check_key(&c->spin, 2, 2.0f / 3.0f, 0);

    float table[TABLE];
    test_flt(curve(&c->size, 1.0f / 3.0f), 2);
    test_flt(curve(&c->size, 2.0f / 3.0f), 3);
    test_assert(box_particle_curve(bake(&c->size, table), 0, 0, 0.5f) > 2);

    ecs_fini(world);
}
