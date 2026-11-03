/**
 * @file CurveKeys.c
 * @brief Tests for keyed particle curves: uneven keys, each kind, one and no keys, the area under
 * the spin curve, gradients, the clamp of an overshoot, the errors of the validate hooks, and
 * keys that prefabs, IsA, effect instances, copies and script reloads own.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "simulate.h"

/** Tolerance of comparisons at the samples of a row */
#define EPS (1e-5f)

/** Tolerance of comparisons between two samples of a cubic, where the row is linear */
#define SAMPLED (1e-3f)

/** Floats of a table that holds a curve and its area, or a gradient */
#define TABLE (BOX_PARTICLE_GRADIENT_ROWS * BOX_PARTICLE_CURVE_SAMPLES)

/** Frame delta of the tests that run the extract */
#define DELTA (0.1f)

/** The three kinds of a curve */
static const CurveKind kinds[3] = { CurveLinear, CurveCatmullRom, CurveBezier };

/** Check that two floats are within a tolerance of each other, relative to their size.
 *
 * @param a The value
 * @param b The expected value
 * @param eps The tolerance
 */
static void near_eps(
    float a,
    float b,
    float eps)
{
    test_assert(fabsf(a - b) <= eps * fmaxf(1.0f, fabsf(b)));
}

/** Check that two floats are within EPS of each other.
 *
 * @param a The value
 * @param b The expected value
 */
static void near(
    float a,
    float b)
{
    near_eps(a, b, EPS);
}

/** Bake a curve into row 0 of a table and its accumulated area into row 1.
 *
 * @param c The curve
 * @param fallback The value without keys
 * @param table Receives the rows, TABLE floats
 * @return The table
 */
static const float* bake(
    const ParticleCurve *c,
    float fallback,
    float *table)
{
    box_particle_curve_bake(c, fallback, table);
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

    return box_particle_curve(bake(c, 1, table), 0, 0, t);
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

    return box_particle_curve_area(bake(c, 1, table), 0, 0, t);
}

/** Evaluate a curve exactly with the curve module.
 *
 * @param c The curve
 * @param t The time
 * @return The value
 */
static float exact(
    const ParticleCurve *c,
    float t)
{
    Curve view = { .kind = c->kind, .keys = c->keys };

    return box_curve_value(&view, t, 1);
}

/** Check that the row of a curve holds the exact value of the curve at every sample.
 *
 * @param c The curve
 */
static void check_samples(
    const ParticleCurve *c)
{
    float table[TABLE];
    int32_t i;
    bake(c, 1, table);
    for (i = 0; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        float t = (float)i / (float)(BOX_PARTICLE_CURVE_SAMPLES - 1);
        near(box_particle_curve(table, 0, 0, t), exact(c, t));
    }
}

/** Evaluate a gradient from its baked rows.
 *
 * @param g The gradient
 * @param t The normalized age
 * @param out Receives the linear color
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

/** A curve of a kind over three keys that rises from 0 to 1 at 0.5 and falls back to 0, its
 * first key leaving at a slope of 4 for CurveBezier: at age 0.25 it is 1/2 linear, 9/16
 * Catmull-Rom and 3/4 Bezier.
 *
 * @param kind The kind
 * @param keys Receives the three keys the curve views
 * @return The curve
 */
static ParticleCurve hump(
    CurveKind kind,
    CurveKey *keys)
{
    keys[0] = (CurveKey){ .time = 0, .value = 0, .out = 4 };
    keys[1] = (CurveKey){ .time = 0.5f, .value = 1 };
    keys[2] = (CurveKey){ .time = 1, .value = 0 };

    return (ParticleCurve){ kind, { .array = keys, .count = 3, .size = 3 } };
}

void CurveKeys_uneven_keys(void) {
    // particle/curve_keys
    ParticleCurve c = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {0.2f, 1}, {1, 0}) };
    check_samples(&c);
    near(curve(&c, 0), 0);
    near(curve(&c, 0.1f), 0.5f);
    near(curve(&c, 0.6f), 0.5f);
    near(curve(&c, 1), 0);
    near(curve(&c, 0.9f), 0.125f);
}

void CurveKeys_kind_linear(void) {
    // particle/curve_keys
    CurveKey keys[3];
    ParticleCurve c = hump(CurveLinear, keys);
    check_samples(&c);
    near_eps(curve(&c, 0.25f), 0.5f, SAMPLED);
    near_eps(curve(&c, 0.75f), 0.5f, SAMPLED);
    near(curve(&c, 0), 0);
    near(curve(&c, 1), 0);
}

void CurveKeys_kind_catmull_rom(void) {
    // particle/curve_keys
    CurveKey keys[3];
    ParticleCurve c = hump(CurveCatmullRom, keys);
    check_samples(&c);
    near_eps(curve(&c, 0.25f), 0.5625f, SAMPLED);
    near_eps(curve(&c, 0.75f), 0.5625f, SAMPLED);
    near(curve(&c, 0), 0);
    near(curve(&c, 1), 0);
}

void CurveKeys_kind_bezier(void) {
    // particle/curve_keys
    CurveKey keys[3];
    ParticleCurve c = hump(CurveBezier, keys);
    check_samples(&c);
    near_eps(curve(&c, 0.25f), 0.75f, SAMPLED);
    near_eps(curve(&c, 0.75f), 0.5f, SAMPLED);
    near(curve(&c, 0), 0);
    near(curve(&c, 1), 0);
}

void CurveKeys_one_key(void) {
    // particle/curve_keys
    int32_t k, i;
    for (k = 0; k < 3; k ++) {
        ParticleCurve c = { kinds[k], BOX_CURVE_KEYS({0.4f, 2.5f, 3, -3}) };
        for (i = 0; i <= 10; i ++) {
            test_flt(curve(&c, (float)i / 10.0f), 2.5f);
        }

        near(area(&c, 1), 2.5f);
    }
}

void CurveKeys_no_keys(void) {
    // particle/curve_keys
    ParticleCurve c = {0};
    float row[BOX_PARTICLE_CURVE_SAMPLES];
    int32_t i;
    box_particle_curve_bake(&c, 1, row);
    for (i = 0; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        test_flt(row[i], 1);
    }

    box_particle_curve_bake(&c, 0, row);
    for (i = 0; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
        test_flt(row[i], 0);
    }

    near(area(&c, 0.5f), 0.5f);
}

void CurveKeys_keys_hold_outside(void) {
    // particle/curve_keys
    ParticleCurve c = { CurveCatmullRom, BOX_CURVE_KEYS({0.25f, 2}, {0.75f, 4}) };
    near(curve(&c, 0), 2);
    near(curve(&c, 0.2f), 2);
    near(curve(&c, 0.8f), 4);
    near(curve(&c, 1), 4);
}

void CurveKeys_spin_area_uneven(void) {
    // particle/curve_keys
    ParticleCurve c = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {0.25f, 1}, {1, 1}) };
    near_eps(area(&c, 0.25f), 0.125f, SAMPLED);
    near_eps(area(&c, 1), 0.875f, SAMPLED);
    near_eps(box_particle_turned(100, NULL, BOX_PARTICLE_CURVE_CONSTANT,
        BOX_PARTICLE_CURVE_CONSTANT, 1, 1, 2, 0), 100, EPS);

    float table[TABLE];
    bake(&c, 1, table);
    near_eps(box_particle_turned(100, table, 0, BOX_PARTICLE_CURVE_CONSTANT, 0, 2, 2, 0),
        100 * 2 * 0.875f, SAMPLED);
}

void CurveKeys_spin_area_each_kind(void) {
    // particle/curve_keys
    int32_t k, i;
    for (k = 0; k < 3; k ++) {
        CurveKey keys[3];
        ParticleCurve c = hump(kinds[k], keys);
        Curve view = { .kind = c.kind, .keys = c.keys };
        for (i = 0; i <= 8; i ++) {
            float t = (float)i / 8.0f;
            near_eps(area(&c, t), box_curve_area(&view, 0, t, 1), SAMPLED);
        }
    }

    CurveKey a[3], b[3];
    ParticleCurve linear = hump(CurveLinear, a), bezier = hump(CurveBezier, b);
    test_assert(area(&bezier, 0.5f) > area(&linear, 0.5f) + 0.05f);
}

void CurveKeys_spin_area_one_and_no_key(void) {
    // particle/curve_keys
    ParticleCurve one = { CurveBezier, BOX_CURVE_KEYS({0.5f, 2}) }, none = {0};
    near(area(&one, 0.5f), 1);
    near(area(&one, 1), 2);
    near(area(&none, 1), 1);
    near(area(&none, 0.25f), 0.25f);
}

void CurveKeys_gradient_uneven(void) {
    // particle/curve_keys
    ParticleGradient g = { CurveLinear, BOX_VEC_VIEW(ParticleGradientKey,
        {0, {255, 0, 0}}, {0.25f, {0, 255, 0}}, {1, {0, 0, 255}}) };
    float c[3];
    gradient(&g, 0, c);
    near(c[0], 1);
    near(c[1], 0);
    gradient(&g, 0.125f, c);
    near(c[0], 0.5f);
    near(c[1], 0.5f);
    near(c[2], 0);
    gradient(&g, 0.625f, c);
    near(c[0], 0);
    near(c[1], 0.5f);
    near(c[2], 0.5f);
    gradient(&g, 1, c);
    near(c[2], 1);
}

void CurveKeys_gradient_each_kind(void) {
    // particle/curve_keys
    float quarter[3] = { 0.5f, 0.5625f, 0.5f };
    float eighth[3] = { 0.25f, 0.2265625f, 0.15625f };
    float c[3];
    int32_t k;
    for (k = 0; k < 3; k ++) {
        ParticleGradient g = { kinds[k], BOX_VEC_VIEW(ParticleGradientKey,
            {0, {255, 0, 0}}, {0.5f, {0, 255, 0}}, {1, {0, 0, 255}}) };
        gradient(&g, 0.25f, c);
        near_eps(c[1], quarter[k], SAMPLED);
        gradient(&g, 0.125f, c);
        near_eps(c[1], eighth[k], SAMPLED);
        gradient(&g, 0, c);
        near(c[0], 1);
        near(c[1], 0);
        gradient(&g, 1, c);
        near(c[1], 0);
        near(c[2], 1);
    }
}

void CurveKeys_gradient_one_and_no_key(void) {
    // particle/curve_keys
    ParticleGradient one = { CurveCatmullRom, BOX_VEC_VIEW(ParticleGradientKey,
        {0.3f, {128, 0, 255}}) }, none = {0};
    float c[3];
    gradient(&one, 0.9f, c);
    near(c[0], box_particle_linear(128));
    near(c[1], 0);
    near(c[2], 1);
    gradient(&none, 0.5f, c);
    near(c[0], 1);
    near(c[1], 1);
    near(c[2], 1);
}

void CurveKeys_overshoot_clamps(void) {
    // particle/curve_keys
    ParticleCurve dip = { CurveBezier, BOX_CURVE_KEYS({0, 0, 0, -4}, {1, 1}) };
    ParticleGradient dark = { CurveCatmullRom, BOX_VEC_VIEW(ParticleGradientKey,
        {0, {0, 0, 0}}, {0.1f, {255, 255, 255}}, {0.2f, {0, 0, 0}}, {1, {0, 0, 0}}) };
    float table[TABLE], g[TABLE], color[3] = {1, 1, 1}, raw[3], tinted[3];
    bake(&dip, 1, table);
    test_assert(box_particle_curve(table, 0, 0, 0.1f) < 0);
    test_flt(box_particle_scaled(2, table, 0, BOX_PARTICLE_CURVE_CONSTANT, 0, 0.1f, 0), 0);
    near(box_particle_scaled(2, table, 0, BOX_PARTICLE_CURVE_CONSTANT, 0, 1, 0), 2);
    test_flt(box_particle_scaled(2, NULL, BOX_PARTICLE_CURVE_CONSTANT,
        BOX_PARTICLE_CURVE_CONSTANT, -1, 0.5f, 0), 0);
    test_assert(box_particle_rotation(0, 0, table, 0, BOX_PARTICLE_CURVE_CONSTANT, 0, 0,
        BOX_PARTICLE_CURVE_CONSTANT, BOX_PARTICLE_CURVE_CONSTANT, 1, 0, 0.1f, 1) < 0);

    box_particle_gradient_bake(&dark, g);
    box_particle_gradient(g, 0, NULL, 0.3f, raw);
    test_assert(raw[0] < 0);
    box_particle_tinted(color, g, 0, BOX_PARTICLE_CURVE_CONSTANT, NULL, 0.3f, 0, tinted);
    test_flt(tinted[0], 0);
    test_flt(tinted[1], 0);
    test_flt(tinted[2], 0);
    box_particle_tinted(color, g, 0, BOX_PARTICLE_CURVE_CONSTANT, NULL, 0.1f, 0, tinted);
    test_assert(tinted[0] > 0.9f);
}

/** Errors logged while capturing */
static char curve_errors[4096];

/** Log level before capturing */
static int32_t curve_level;

/** Log function before capturing */
static ecs_os_api_log_t curve_log;

/** Whether the log was colored before capturing */
static bool curve_colors;

/** Log function that keeps the errors.
 *
 * @param level The level
 * @param file The file
 * @param line The line
 * @param msg The message
 */
static void curve_capture(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level <= -3 && strlen(curve_errors) + strlen(msg) + 2 < sizeof(curve_errors)) {
        strcat(curve_errors, msg);
        strcat(curve_errors, "\n");
    }
}

/** Start keeping the errors in curve_errors. */
static void errors_begin(void)
{
    curve_errors[0] = '\0';
    curve_level = ecs_log_set_level(-1);
    curve_colors = ecs_log_enable_colors(false);
    curve_log = ecs_os_api.log_;
    ecs_os_api.log_ = curve_capture;
}

/** Stop keeping the errors. */
static void errors_end(void)
{
    ecs_os_api.log_ = curve_log;
    ecs_log_enable_colors(curve_colors);
    ecs_log_set_level(curve_level);
}

/** Set ParticleCurves on an entity named smoke and get the errors it logs.
 *
 * @param value The curves
 * @return The errors
 */
static const char* curves_errors(
    ParticleCurves value)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_entity(world, { .name = "smoke" });
    errors_begin();
    ecs_set_ptr(world, e, ParticleCurves, &value);
    errors_end();
    ecs_fini(world);

    return curve_errors;
}

/** Check that the errors hold a text.
 *
 * @param errors The errors
 * @param text The text
 */
static void has(
    const char *errors,
    const char *text)
{
    bool found = strstr(errors, text) != NULL;
    if (!found) {
        printf("expected '%s' in:\n%s\n", text, errors);
    }

    test_assert(found);
}

void CurveKeys_error_none(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .size = { CurveCatmullRom, BOX_CURVE_KEYS({0, 1}, {0.5f, 2}, {1, 0}) },
        .color = { CurveBezier, BOX_VEC_VIEW(ParticleGradientKey, {0, {1, 2, 3}}) }
    });
    test_str(errors, "");
}

void CurveKeys_error_kind(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .opacity = { (CurveKind)5, BOX_CURVE_KEYS({0, 1}, {1, 0}) }
    });
    has(errors, "invalid ParticleCurves on smoke: opacity kind 5 is not CurveLinear");
}

void CurveKeys_error_time_not_finite(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .size = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {NAN, 0}) }
    });
    has(errors, "invalid ParticleCurves on smoke: size key 1: time is not finite");
}

void CurveKeys_error_value_not_finite(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .rotation = { CurveLinear, BOX_CURVE_KEYS({0, INFINITY}) }
    });
    has(errors, "invalid ParticleCurves on smoke: rotation key 0: value is not finite");
}

void CurveKeys_error_slope_not_finite(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .spin = { CurveBezier, BOX_CURVE_KEYS({0, 1}, {1, 0, NAN, 0}) }
    });
    has(errors, "invalid ParticleCurves on smoke: spin key 1: in is not finite");
}

void CurveKeys_error_not_increasing(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .size = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {0.5f, 2}, {0.5f, 3}) }
    });
    has(errors, "invalid ParticleCurves on smoke: size key 2: time 0.5 is not after time 0.5 "
        "of key 1");
}

void CurveKeys_error_time_outside(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .opacity = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1.5f, 0}) },
        .size = { CurveLinear, BOX_CURVE_KEYS({-0.25f, 1}) }
    });
    has(errors, "invalid ParticleCurves on smoke: opacity key 1: time 1.5 is outside of 0 to 1");
    has(errors, "invalid ParticleCurves on smoke: size key 0: time -0.25 is outside of 0 to 1");
}

void CurveKeys_error_gradient_kind(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .color = { (CurveKind)-1, BOX_VEC_VIEW(ParticleGradientKey, {0, {1, 2, 3}}) }
    });
    has(errors, "invalid ParticleCurves on smoke: color kind -1 is not CurveLinear");
}

void CurveKeys_error_gradient_not_increasing(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .color = { CurveLinear, BOX_VEC_VIEW(ParticleGradientKey,
            {0.5f, {1, 2, 3}}, {0.25f, {4, 5, 6}}) }
    });
    has(errors, "invalid ParticleCurves on smoke: color key 1: time 0.25 is not after time 0.5 "
        "of key 0");
}

void CurveKeys_error_gradient_time_outside(void) {
    // particle/curve_keys
    const char *errors = curves_errors((ParticleCurves){
        .color = { CurveLinear, BOX_VEC_VIEW(ParticleGradientKey,
            {0, {1, 2, 3}}, {2, {4, 5, 6}}) }
    });
    has(errors, "invalid ParticleCurves on smoke: color key 1: time 2 is outside of 0 to 1");
}

void CurveKeys_error_light(void) {
    // particle/curve_keys
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_entity(world, { .name = "flash" });
    errors_begin();
    ecs_set(world, e, ParticleLight, {
        .intensity = 1, .curve = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {0, 2}) }
    });
    errors_end();
    has(curve_errors, "invalid ParticleLight on flash: curve key 1: time 0 is not after time 0 "
        "of key 0");
    ecs_fini(world);
}

void CurveKeys_error_script(void) {
    // particle/curve_keys
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    errors_begin();
    int result = ecs_script_run(world, "curves",
        "using box.curve\n"
        "using box.particle\n"
        "fx {\n"
        "  smoke {\n"
        "    ParticleCurves: {size: {kind: CurveCatmullRom, keys: [{0, 1}, {1.2, 2}]}}\n"
        "  }\n"
        "}\n", NULL);
    errors_end();
    test_int(result, 0);
    has(curve_errors, "invalid ParticleCurves on fx.smoke: size key 1: time 1.2 is outside of "
        "0 to 1");
    ecs_fini(world);
}

/** Create a world that renders into a null backend.
 *
 * @param null Receives the backend, freed by the caller after the world
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Run a frame and wait until the backend received it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
}

/** Find the pool of an emitter in the last packet.
 *
 * @param null The backend
 * @param e The emitter
 * @return The pool, NULL when the packet has none
 */
static const box_render_particle_pool_t* pool_of(
    box_render_null_t *null,
    ecs_entity_t e)
{
    const box_render_particle_pool_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.particles); i ++) {
        const box_render_particle_pool_t *p = ecs_vec_get_t(
            &null->last.particles, box_render_particle_pool_t, i);
        if (p->entity == e) {
            result = p;
        }
    }

    return result;
}

/** Templates of the inheritance tests: a prefab with keyed curves, an instance that keeps
 * them and one that overrides its opacity */
#define PREFABS \
    "using box.curve\n" \
    "using box.particle\n" \
    "prefab smoke {\n" \
    "  ParticleEmitter: {rate: 20, lifetime: {1, 1}}\n" \
    "  ParticleCurves: {\n" \
    "    size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.4, 1.8}, {1, 2.2}]}\n" \
    "    color: {kind: CurveBezier, keys: [{0, {255, 220, 60}}, {1, {120, 20, 10}}]}\n" \
    "  }\n" \
    "}\n" \
    "a : smoke {}\n" \
    "b : smoke {}\n" \
    "c : smoke {\n" \
    "  ParticleCurves: {\n" \
    "    size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.4, 1.8}, {1, 2.2}]}\n" \
    "    opacity: {keys: [{0, 1}, {1, 0}]}\n" \
    "  }\n" \
    "}\n"

void CurveKeys_prefab_inherit(void) {
    // particle/curve_keys
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_int(ecs_script_run(world, "prefabs", PREFABS, NULL), 0);
    ecs_entity_t prefab = ecs_lookup(world, "smoke");
    ecs_entity_t a = ecs_lookup(world, "a"), b = ecs_lookup(world, "b");
    test_assert(prefab && a && b);

    const ParticleCurves *pc = ecs_get(world, prefab, ParticleCurves);
    const ParticleCurves *ac = ecs_get(world, a, ParticleCurves);
    test_assert(pc != NULL && ac != NULL);
    test_int(ac->size.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&ac->size.keys), 3);
    test_flt(ecs_vec_get_t(&ac->size.keys, CurveKey, 1)->value, 1.8f);
    test_int(ac->color.kind, CurveBezier);
    test_int(ecs_vec_count(&ac->color.keys), 2);
    test_int(ecs_vec_count(&ac->opacity.keys), 0);
    if (ecs_owns(world, a, ParticleCurves)) {
        test_assert(ecs_vec_first(&ac->size.keys) != ecs_vec_first(&pc->size.keys));
    }

    frame(world);
    const box_render_particle_pool_t *pa = pool_of(null, a), *pb = pool_of(null, b);
    test_assert(pa != NULL && pb != NULL);
    test_assert(pa->block.curve_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pa->block.curve_rows[0], pb->block.curve_rows[0]);
    test_assert(pa->block.gradient_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pa->block.gradient_row, pb->block.gradient_row);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveKeys_instance_override(void) {
    // particle/curve_keys
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_int(ecs_script_run(world, "prefabs", PREFABS, NULL), 0);
    ecs_entity_t a = ecs_lookup(world, "a"), c = ecs_lookup(world, "c");
    test_assert(a && c);
    test_assert(ecs_owns(world, c, ParticleCurves));

    const ParticleCurves *cc = ecs_get(world, c, ParticleCurves);
    test_int(ecs_vec_count(&cc->opacity.keys), 2);
    test_flt(ecs_vec_get_t(&cc->opacity.keys, CurveKey, 1)->value, 0);
    test_int(ecs_vec_count(&ecs_get(world, a, ParticleCurves)->opacity.keys), 0);

    ParticleCurves *mut = ecs_get_mut(world, c, ParticleCurves);
    ecs_vec_get_t(&mut->size.keys, CurveKey, 2)->value = 5;
    ecs_modified(world, c, ParticleCurves);
    test_flt(ecs_vec_get_t(&ecs_get(world, ecs_lookup(world, "smoke"), ParticleCurves)->size.keys,
        CurveKey, 2)->value, 2.2f);
    test_flt(ecs_vec_get_t(&ecs_get(world, a, ParticleCurves)->size.keys, CurveKey, 2)->value,
        2.2f);

    frame(world);
    const box_render_particle_pool_t *pa = pool_of(null, a), *pc = pool_of(null, c);
    test_assert(pa != NULL && pc != NULL);
    test_assert(pa->block.curve_rows[0] != pc->block.curve_rows[0]);
    test_assert(pc->block.curve_rows[1] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pa->block.curve_rows[1], BOX_PARTICLE_CURVE_CONSTANT);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Allocations not freed yet, counted by the flecs os api.
 *
 * @return Allocations minus frees
 */
static int64_t live_allocs(void) {
    return ecs_os_api_malloc_count + ecs_os_api_calloc_count - ecs_os_api_free_count;
}

/** Copy, clone, instantiate and delete the ParticleCurves and ParticleLight of a prefab.
 *
 * @param world The world
 * @param prefab Prefab with ParticleCurves and ParticleLight
 */
static void churn(
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    const ParticleCurves *src = ecs_get(world, prefab, ParticleCurves);
    ecs_entity_t a = ecs_new(world);
    ecs_set_ptr(world, a, ParticleCurves, src);
    ecs_set_ptr(world, a, ParticleLight, ecs_get(world, prefab, ParticleLight));
    ecs_set_ptr(world, a, ParticleCurves, src);
    ecs_add(world, a, ParticlePlayback);

    const ParticleCurves *copy = ecs_get(world, a, ParticleCurves);
    test_assert(ecs_vec_first(&copy->size.keys) != ecs_vec_first(&src->size.keys));
    test_assert(ecs_vec_first(&copy->color.keys) != ecs_vec_first(&src->color.keys));
    test_int(ecs_vec_count(&copy->size.keys), ecs_vec_count(&src->size.keys));

    ecs_entity_t b = ecs_clone(world, 0, a, true);
    ecs_entity_t c = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_auto_override(world, c, ParticleCurves);
    ecs_add(world, c, ParticlePlayback);
    ecs_remove(world, b, ParticleLight);
    ecs_delete(world, a);
    ecs_delete(world, b);
    ecs_delete(world, c);
}

void CurveKeys_copy_delete_no_leak(void) {
    // particle/curve_keys
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(ecs_script_run(world, "prefabs",
        "using box.curve\n"
        "using box.particle\n"
        "prefab p {\n"
        "  ParticleCurves: {\n"
        "    size: {kind: CurveCatmullRom, keys: [{0, 0}, {0.5, 1}, {1, 0}]}\n"
        "    spin: {kind: CurveBezier, keys: [{0, 1, 0, -2}, {1, 0}]}\n"
        "    color: {keys: [{0, {255, 0, 0}}, {1, {0, 0, 255}}]}\n"
        "  }\n"
        "  ParticleLight: {curve: {keys: [{0, 1}, {1, 0}]}, period: 1}\n"
        "}\n", NULL), 0);
    ecs_entity_t prefab = ecs_lookup(world, "p");
    test_assert(prefab != 0);

    churn(world, prefab);
    int64_t before = live_allocs();
    int32_t i;
    for (i = 0; i < 4; i ++) {
        churn(world, prefab);
    }

    test_int(live_allocs(), before);

    ecs_delete(world, prefab);
    ecs_fini(world);
}

void CurveKeys_effect_instance(void) {
    // particle/curve_keys
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_int(ecs_script_run(world, "effects",
        "using box.curve\n"
        "using box.particle\n"
        "@tree ChildOf\n"
        "prefab Puff {\n"
        "  ParticleEmitter: {max: 50, lifetime: {0.5, 0.5}}\n"
        "  ParticleBurst: {count: 5}\n"
        "  ParticleCurves: {opacity: {kind: CurveCatmullRom, keys: [{0, 0}, {0.2, 1}, {1, 0}]}}\n"
        "}\n", NULL), 0);
    ecs_entity_t effect = ecs_lookup(world, "Puff");
    test_assert(effect != 0);
    vec3 at = {0, 0, 0}, other = {5, 0, 0};
    ecs_entity_t first = box_particle_burst_at(world, effect, at, 1);
    ecs_entity_t second = box_particle_burst_at(world, effect, other, 1);
    test_assert(first != 0 && second != 0);
    frame(world);

    const ParticleCurves *c = ecs_get(world, first, ParticleCurves);
    test_assert(c != NULL);
    test_int(c->opacity.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&c->opacity.keys), 3);
    const box_render_particle_pool_t *p1 = pool_of(null, first), *p2 = pool_of(null, second);
    test_assert(p1 != NULL && p2 != NULL);
    test_assert(p1->block.curve_rows[1] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(p1->block.curve_rows[1], p2->block.curve_rows[1]);

    ecs_delete(world, effect);
    frame(world);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveKeys_script_reload(void) {
    // particle/curve_keys
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "fx" }),
        .code =
            "using box.curve\n"
            "using box.particle\n"
            "jet {\n"
            "  ParticleEmitter: {rate: 20, lifetime: {1, 1}}\n"
            "  ParticleCurves: {size: {keys: [{0, 1}, {1, 2}]}}\n"
            "}\n"
    });
    test_assert(script != 0);
    ecs_entity_t jet = ecs_lookup(world, "jet");
    test_assert(jet != 0);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, jet);
    test_assert(pool != NULL);
    const float *table = box_render_particles_curve_table(box_render_particles_get(world), NULL);
    near(box_particle_curve(table, pool->block.curve_rows[0], 0, 1), 2);

    test_int(ecs_script_update(world, script, 0,
        "using box.curve\n"
        "using box.particle\n"
        "jet {\n"
        "  ParticleEmitter: {rate: 20, lifetime: {1, 1}}\n"
        "  ParticleCurves: {size: {kind: CurveBezier, keys: [{0, 1}, {0.5, 3}, {1, 4}]}}\n"
        "}\n"), 0);
    jet = ecs_lookup(world, "jet");
    test_assert(jet != 0);
    const ParticleCurves *c = ecs_get(world, jet, ParticleCurves);
    test_assert(c != NULL);
    test_int(c->size.kind, CurveBezier);
    test_int(ecs_vec_count(&c->size.keys), 3);
    frame(world);
    pool = pool_of(null, jet);
    test_assert(pool != NULL);
    table = box_render_particles_curve_table(box_render_particles_get(world), NULL);
    near(box_particle_curve(table, pool->block.curve_rows[0], 0, 1), 4);
    near_eps(box_particle_curve(table, pool->block.curve_rows[0], 0, 0.5f), 3, SAMPLED);

    ecs_fini(world);
    box_render_null_free(null);
}
