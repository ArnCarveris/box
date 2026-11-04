/**
 * @file CurveRange.c
 * @brief Tests for random between two curves: a curve with max_keys blends each particle toward
 * its max curve by a draw of its own stream for its whole life, in the rows of the block, the
 * area under spin and gradients, and the errors of the validate hooks for max_keys.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
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

/** Seed of the emitters of the tests */
#define SEED (7u)

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

/** An emitter of a test in a world that renders into a null backend, after one frame */
typedef struct range_t {
    ecs_world_t *world;         /**< The world */
    box_render_null_t *null;    /**< The backend */
    ecs_entity_t e;             /**< The emitter */
    const box_render_particle_block_t *block; /**< Its block in the last packet */
    const float *table;         /**< The curve table */
    int32_t rows;               /**< Rows of the curve table */
} range_t;

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

/** Find the block of an emitter in the last packet.
 *
 * @param null The backend
 * @param e The emitter
 * @return The block, NULL when the packet has none
 */
static const box_render_particle_block_t* block_of(
    box_render_null_t *null,
    ecs_entity_t e)
{
    const box_render_particle_block_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.particles); i ++) {
        const box_render_particle_pool_t *p = ecs_vec_get_t(
            &null->last.particles, box_render_particle_pool_t, i);
        if (p->entity == e) {
            result = &p->block;
        }
    }

    return result;
}

/** Read the block and the curve table of the emitter of a test after the last frame.
 *
 * @param r The test
 */
static void range_read(
    range_t *r)
{
    r->block = block_of(r->null, r->e);
    test_assert(r->block != NULL);
    r->table = box_render_particles_curve_table(box_render_particles_get(r->world), &r->rows);
}

/** Create an emitter named jet with the seed SEED and curves, and run a frame.
 *
 * @param r Receives the test
 * @param curves The members of its ParticleCurves in script
 */
static void range_init(
    range_t *r,
    const char *curves)
{
    char code[2048];
    ecs_os_memset_t(r, 0, range_t);
    r->world = ecs_init();
    ECS_IMPORT(r->world, Box);
    r->null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(r->null);
    box_render_set_backend(r->world, &backend);
    snprintf(code, sizeof(code),
        "using box.curve\n"
        "using box.particle\n"
        "jet {\n"
        "  ParticleEmitter: {rate: 20, lifetime: {1, 1}, seed: %u}\n"
        "  ParticleCurves: {%s}\n"
        "}\n", SEED, curves);
    test_int(ecs_script_run(r->world, "range", code, NULL), 0);
    r->e = ecs_lookup(r->world, "jet");
    test_assert(r->e != 0);
    frame(r->world);
    range_read(r);
}

/** Free a test.
 *
 * @param r The test
 */
static void range_fini(
    range_t *r)
{
    ecs_fini(r->world);
    box_render_null_free(r->null);
}

/** Evaluate a scalar curve of the block of a test for a particle, as the draw does.
 *
 * @param r The test
 * @param curve BOX_PARTICLE_CURVE_SIZE to BOX_PARTICLE_CURVE_SPIN
 * @param index The spawn index of the particle
 * @param t The normalized age
 * @return The value
 */
static float value_of(
    const range_t *r,
    uint32_t curve,
    uint32_t index,
    float t)
{
    const box_render_particle_block_t *b = r->block;

    return box_particle_curve_range(r->table, b->curve_rows[curve], b->curve_max_rows[curve],
        b->curve_values[curve], t, box_particle_blend(b->seed, index, curve));
}

/** The size curves of the tests: from 1 to 0 and from 2 to 0.5, the second always above */
#define SIZES "size: {keys: [{0, 1}, {1, 0}], max_keys: [{0, 2}, {1, 0.5}]}"

void CurveRange_script_rows(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, SIZES);
    const ParticleCurves *c = ecs_get(r.world, r.e, ParticleCurves);
    test_assert(c != NULL);
    test_int(ecs_vec_count(&c->size.keys), 2);
    test_int(ecs_vec_count(&c->size.max_keys), 2);
    test_flt(ecs_vec_get_t(&c->size.max_keys, CurveKey, 1)->value, 0.5f);

    const box_render_particle_block_t *b = r.block;
    test_assert(b->curve_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->curve_max_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->curve_rows[0] != b->curve_max_rows[0]);
    near(box_particle_curve(r.table, b->curve_rows[0], 0, 0), 1);
    near(box_particle_curve(r.table, b->curve_rows[0], 0, 1), 0);
    near(box_particle_curve(r.table, b->curve_max_rows[0], 0, 0), 2);
    near(box_particle_curve(r.table, b->curve_max_rows[0], 0, 1), 0.5f);
    test_uint(b->curve_max_rows[1], BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(b->curve_max_rows[2], BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(b->curve_max_rows[3], BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(b->gradient_max_row, BOX_PARTICLE_CURVE_CONSTANT);
    range_fini(&r);
}

void CurveRange_without_max_no_second_row(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, "size: {keys: [{0, 1}, {1, 0}]}, color: {keys: [{0, {255, 0, 0}}, "
        "{1, {0, 0, 255}}]}");
    const box_render_particle_block_t *b = r.block;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        test_uint(b->curve_max_rows[i], BOX_PARTICLE_CURVE_CONSTANT);
    }

    test_uint(b->gradient_max_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_int(r.rows, 1 + BOX_PARTICLE_GRADIENT_ROWS);

    float a = value_of(&r, BOX_PARTICLE_CURVE_SIZE, 3, 0.25f);
    near(a, box_particle_curve(r.table, b->curve_rows[0], b->curve_values[0], 0.25f));
    near(a, 0.75f);
    range_fini(&r);
}

void CurveRange_same_blend_every_age(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, SIZES);
    const box_render_particle_block_t *b = r.block;
    static const float ages[] = { 0, 0.13f, 0.5f, 0.77f, 1 };
    uint32_t i;
    int32_t k;
    for (i = 0; i < 64; i ++) {
        float u = box_particle_blend(b->seed, i, BOX_PARTICLE_CURVE_SIZE);
        test_assert(u >= 0 && u < 1);
        test_flt(box_particle_blend(b->seed, i, BOX_PARTICLE_CURVE_SIZE), u);
        for (k = 0; k < (int32_t)(sizeof(ages) / sizeof(ages[0])); k ++) {
            float t = ages[k];
            float lo = box_particle_curve(r.table, b->curve_rows[0], 0, t);
            float hi = box_particle_curve(r.table, b->curve_max_rows[0], 0, t);
            float v = value_of(&r, BOX_PARTICLE_CURVE_SIZE, i, t);
            near_eps((v - lo) / (hi - lo), u, 1e-4f);
        }
    }

    range_fini(&r);
}

void CurveRange_between_linear_curves(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, "size: {keys: [{0, 0}, {0.5, 1}, {1, 0}], "
        "max_keys: [{0, 1}, {0.5, 0}, {1, 1}]}");
    const ParticleCurves *c = ecs_get(r.world, r.e, ParticleCurves);
    Curve lo = { .kind = CurveLinear, .keys = c->size.keys };
    Curve hi = { .kind = CurveLinear, .keys = c->size.max_keys };
    uint32_t i;
    int32_t k;
    for (i = 0; i < 200; i ++) {
        for (k = 0; k < BOX_PARTICLE_CURVE_SAMPLES; k ++) {
            float t = (float)k / (float)(BOX_PARTICLE_CURVE_SAMPLES - 1);
            float a = box_curve_value(&lo, t, 1), z = box_curve_value(&hi, t, 1);
            float v = value_of(&r, BOX_PARTICLE_CURVE_SIZE, i, t);
            test_assert(v >= fminf(a, z) - EPS);
            test_assert(v <= fmaxf(a, z) + EPS);
        }

        float mid = value_of(&r, BOX_PARTICLE_CURVE_SIZE, i, 0.25f);
        test_assert(mid >= 0.5f - EPS && mid <= 0.5f + EPS);
    }

    range_fini(&r);
}

void CurveRange_distribution_covers_range(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, SIZES);
    int32_t buckets[10] = {0}, i, n = 4096;
    float lo = 1, hi = 0;
    double sum = 0;
    for (i = 0; i < n; i ++) {
        float u = value_of(&r, BOX_PARTICLE_CURVE_SIZE, (uint32_t)i, 0) - 1;
        test_assert(u >= -EPS && u <= 1 + EPS);
        lo = fminf(lo, u);
        hi = fmaxf(hi, u);
        sum += u;
        buckets[u >= 1 ? 9 : (int32_t)(u * 10)] ++;
    }

    test_assert(lo < 0.01f);
    test_assert(hi > 0.99f);
    test_assert(fabs(sum / n - 0.5) < 0.02);
    for (i = 0; i < 10; i ++) {
        test_assert(buckets[i] > 330 && buckets[i] < 490);
    }

    range_fini(&r);
}

void CurveRange_other_fields_unchanged(void) {
    // particle/curve_range
    static const char *base =
        "opacity: {keys: [{0, 1}, {1, 0}], max_keys: [{0, 0.5}, {1, 0.2}]}, "
        "rotation: {keys: [{0, 0}, {1, 1}]}";
    char more[1024];
    snprintf(more, sizeof(more), "%s, %s", base, SIZES);
    range_t a, b;
    range_init(&a, base);
    range_init(&b, more);
    test_uint(a.block->curve_rows[0], BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(a.block->curve_max_rows[0], BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b.block->curve_max_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);

    uint32_t i;
    int32_t k, same = 0;
    for (i = 0; i < 100; i ++) {
        box_particle_sample_t sa, sb;
        const ParticleEmitter *ea = ecs_get(a.world, a.e, ParticleEmitter);
        const ParticleEmitter *eb = ecs_get(b.world, b.e, ParticleEmitter);
        box_particle_sample(ea, i, &sa);
        box_particle_sample(eb, i, &sb);
        test_assert(!memcmp(&sa, &sb, sizeof(sa)));
        for (k = 0; k <= 8; k ++) {
            float t = (float)k / 8.0f;
            test_flt(value_of(&b, BOX_PARTICLE_CURVE_OPACITY, i, t),
                value_of(&a, BOX_PARTICLE_CURVE_OPACITY, i, t));
            test_flt(value_of(&b, BOX_PARTICLE_CURVE_ROTATION, i, t),
                value_of(&a, BOX_PARTICLE_CURVE_ROTATION, i, t));
        }

        same += box_particle_blend(SEED, i, BOX_PARTICLE_CURVE_SIZE) ==
            box_particle_blend(SEED, i, BOX_PARTICLE_CURVE_OPACITY);
    }

    test_int(same, 0);
    range_fini(&a);
    range_fini(&b);
}

void CurveRange_streams_new(void) {
    // particle/curve_range
    uint32_t curve, stream, i;
    int32_t same = 0;
    test_assert(BOX_PARTICLE_STREAM_CURVE > BOX_PARTICLE_STREAM_RADIAL);
    for (curve = BOX_PARTICLE_CURVE_SIZE; curve <= BOX_PARTICLE_CURVE_COLOR; curve ++) {
        for (stream = 0; stream < BOX_PARTICLE_STREAM_CURVE; stream ++) {
            for (i = 0; i < 64; i ++) {
                same += box_particle_blend(SEED, i, curve) ==
                    box_particle_unit(box_particle_hash(SEED, i, stream));
            }
        }

        test_flt(box_particle_blend(SEED, 5, curve),
            box_particle_unit(box_particle_hash(SEED, 5, BOX_PARTICLE_STREAM_CURVE + curve)));
    }

    test_int(same, 0);
}

void CurveRange_blend_only_with_max(void) {
    // particle/curve_cost
    uint32_t curve, i;
    for (curve = BOX_PARTICLE_CURVE_SIZE; curve <= BOX_PARTICLE_CURVE_COLOR; curve ++) {
        for (i = 0; i < 16; i ++) {
            test_flt(box_particle_curve_blend(SEED, i, curve, BOX_PARTICLE_CURVE_CONSTANT), 0);
            test_flt(box_particle_curve_blend(SEED, i, curve, 0),
                box_particle_blend(SEED, i, curve));
            test_flt(box_particle_curve_blend(SEED, i, curve, 7),
                box_particle_blend(SEED, i, curve));
        }
    }
}

void CurveRange_spin_area_blended(void) {
    // particle/curve_range
    ParticleCurve lo = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1, 0}) };
    ParticleCurve hi = { CurveLinear, BOX_CURVE_KEYS({0, 3}, {1, 1}) };
    float table[2 * BOX_PARTICLE_SPIN_ROWS * BOX_PARTICLE_CURVE_SAMPLES];
    float *max = table + BOX_PARTICLE_SPIN_ROWS * BOX_PARTICLE_CURVE_SAMPLES;
    static const float blends[] = { 0, 0.25f, 0.7f, 1 };
    static const float ages[] = { 0.1f, 0.5f, 0.9f, 1 };
    int32_t i, k;
    box_particle_curve_bake(&lo, 1, table);
    box_particle_curve_bake_area(table, table + BOX_PARTICLE_CURVE_SAMPLES);
    box_particle_curve_bake(&hi, 1, max);
    box_particle_curve_bake_area(max, max + BOX_PARTICLE_CURVE_SAMPLES);
    for (i = 0; i < 4; i ++) {
        float u = blends[i], a0 = 1 + 2 * u, a1 = u;
        ParticleCurve mixed = { CurveLinear, BOX_CURVE_KEYS({0, a0}, {1, a1}) };
        float mixed_table[BOX_PARTICLE_SPIN_ROWS * BOX_PARTICLE_CURVE_SAMPLES];
        box_particle_curve_bake(&mixed, 1, mixed_table);
        box_particle_curve_bake_area(mixed_table, mixed_table + BOX_PARTICLE_CURVE_SAMPLES);
        for (k = 0; k < 4; k ++) {
            float t = ages[k];
            float area = box_particle_curve_area_range(table, 0, BOX_PARTICLE_SPIN_ROWS, 0, t, u);
            near_eps(area, a0 * t + 0.5f * (a1 - a0) * t * t, SAMPLED);
            near_eps(area, box_particle_curve_area(mixed_table, 0, 0, t), EPS);
            near_eps(box_particle_turned(90, table, 0, BOX_PARTICLE_SPIN_ROWS, 0, 2 * t, 2, u),
                90 * 2 * area, EPS);
        }
    }

    range_t r;
    range_init(&r, "spin: {keys: [{0, 1}, {1, 0}], max_keys: [{0, 3}, {1, 1}]}");
    const box_render_particle_block_t *b = r.block;
    test_assert(b->curve_rows[3] != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->curve_max_rows[3] != BOX_PARTICLE_CURVE_CONSTANT);
    for (i = 0; i < 32; i ++) {
        float u = box_particle_blend(b->seed, (uint32_t)i, BOX_PARTICLE_CURVE_SPIN);
        float a0 = 1 + 2 * u, a1 = u;
        float deg = box_particle_turned(100, r.table, b->curve_rows[3], b->curve_max_rows[3],
            b->curve_values[3], 0.5f, 1, u);
        near_eps(deg, 100 * (a0 * 0.5f + 0.5f * (a1 - a0) * 0.25f), SAMPLED);
    }

    range_fini(&r);
}

void CurveRange_gradient_blends(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, "color: {keys: [{0, {255, 0, 0}}, {1, {0, 255, 0}}], "
        "max_keys: [{0, {0, 0, 255}}, {1, {255, 255, 255}}]}");
    const box_render_particle_block_t *b = r.block;
    const ParticleCurves *c = ecs_get(r.world, r.e, ParticleCurves);
    test_int(ecs_vec_count(&c->color.max_keys), 2);
    test_assert(b->gradient_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->gradient_max_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->gradient_row != b->gradient_max_row);

    float white[3] = {1, 1, 1}, out[3];
    uint32_t i;
    int32_t ch;
    for (i = 0; i < 32; i ++) {
        float u = box_particle_blend(b->seed, i, BOX_PARTICLE_CURVE_COLOR);
        float lo[3] = {1, 0, 0}, hi[3] = {0, 0, 1};
        box_particle_tinted(white, r.table, b->gradient_row, b->gradient_max_row, b->gradient,
            0, u, out);
        for (ch = 0; ch < 3; ch ++) {
            near(out[ch], lo[ch] + (hi[ch] - lo[ch]) * u);
        }

        box_particle_gradient_range(r.table, b->gradient_row, b->gradient_max_row, b->gradient,
            1, u, out);
        near(out[0], u);
        near(out[1], 1);
        near(out[2], u);
    }

    range_fini(&r);
}

void CurveRange_flat_curves_take_rows(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, "size: {keys: [{0, 1}], max_keys: [{0, 3}]}, "
        "opacity: {keys: [{0, 0.5}], max_keys: [{0.5, 0.5}]}, "
        "rotation: {max_keys: [{0, 1}, {1, 2}]}, "
        "color: {keys: [{0, {255, 0, 0}}], max_keys: [{0, {0, 0, 255}}]}");
    const box_render_particle_block_t *b = r.block;
    test_assert(b->curve_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->curve_max_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(b->curve_rows[1], BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(b->curve_max_rows[1], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->curve_values[1], 0.5f);
    test_assert(b->curve_rows[2] != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->curve_max_rows[2] != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->gradient_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->gradient_max_row != BOX_PARTICLE_CURVE_CONSTANT);

    uint32_t i;
    for (i = 0; i < 16; i ++) {
        float u = box_particle_blend(b->seed, i, BOX_PARTICLE_CURVE_SIZE);
        near(value_of(&r, BOX_PARTICLE_CURVE_SIZE, i, 0.3f), 1 + 2 * u);
        u = box_particle_blend(b->seed, i, BOX_PARTICLE_CURVE_ROTATION);
        near(value_of(&r, BOX_PARTICLE_CURVE_ROTATION, i, 1), 2 * u);
    }

    range_fini(&r);
}

void CurveRange_equal_curves_share_rows(void) {
    // particle/curve_range
    range_t r;
    range_init(&r, "size: {keys: [{0, 1}, {1, 0}], max_keys: [{0, 2}, {1, 0.5}]}, "
        "opacity: {keys: [{0, 2}, {1, 0.5}]}");
    const box_render_particle_block_t *b = r.block;
    test_uint(b->curve_max_rows[0], b->curve_rows[1]);
    test_int(r.rows, 2);

    ParticleCurves *c = ecs_get_mut(r.world, r.e, ParticleCurves);
    ecs_vec_get_t(&c->size.max_keys, CurveKey, 1)->value = 0.25f;
    ecs_modified(r.world, r.e, ParticleCurves);
    frame(r.world);
    range_read(&r);
    near(box_particle_curve(r.table, r.block->curve_max_rows[0], 0, 1), 0.25f);
    near(box_particle_curve(r.table, r.block->curve_rows[1], 0, 1), 0.5f);
    range_fini(&r);
}

void CurveRange_copy_owns_max_keys(void) {
    // particle/curve_range
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(ecs_script_run(world, "prefabs",
        "using box.curve\n"
        "using box.particle\n"
        "prefab p {\n"
        "  ParticleCurves: {\n"
        "    size: {keys: [{0, 1}, {1, 0}], max_keys: [{0, 2}, {1, 0.5}]}\n"
        "    spin: {max_keys: [{0, 2}, {1, 1}]}\n"
        "    color: {keys: [{0, {255, 0, 0}}], max_keys: [{0, {0, 0, 255}}, {1, {0, 0, 0}}]}\n"
        "  }\n"
        "}\n", NULL), 0);
    ecs_entity_t prefab = ecs_lookup(world, "p");
    test_assert(prefab != 0);
    const ParticleCurves *src = ecs_get(world, prefab, ParticleCurves);
    test_int(ecs_vec_count(&src->spin.max_keys), 2);
    test_int(ecs_vec_count(&src->color.max_keys), 2);

    int64_t before = 0;
    int32_t i;
    for (i = 0; i < 5; i ++) {
        if (i == 1) {
            before = ecs_os_api_malloc_count + ecs_os_api_calloc_count - ecs_os_api_free_count;
        }

        src = ecs_get(world, prefab, ParticleCurves);
        ecs_entity_t b = ecs_new(world);
        ecs_set_ptr(world, b, ParticleCurves, src);
        const ParticleCurves *copy = ecs_get(world, b, ParticleCurves);
        test_assert(ecs_vec_first(&copy->size.max_keys) != ecs_vec_first(&src->size.max_keys));
        test_assert(ecs_vec_first(&copy->color.max_keys) !=
            ecs_vec_first(&src->color.max_keys));
        test_int(ecs_vec_count(&copy->size.max_keys), 2);
        ecs_entity_t c = ecs_clone(world, 0, b, true);
        ecs_delete(world, b);
        ecs_delete(world, c);
    }

    test_int(ecs_os_api_malloc_count + ecs_os_api_calloc_count - ecs_os_api_free_count, before);
    ecs_fini(world);
}

/** Errors logged while capturing */
static char range_errors[4096];

/** Log level before capturing */
static int32_t range_level;

/** Log function before capturing */
static ecs_os_api_log_t range_log;

/** Whether the log was colored before capturing */
static bool range_colors;

/** Log function that keeps the errors.
 *
 * @param level The level
 * @param file The file
 * @param line The line
 * @param msg The message
 */
static void range_capture(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level <= -3 && strlen(range_errors) + strlen(msg) + 2 < sizeof(range_errors)) {
        strcat(range_errors, msg);
        strcat(range_errors, "\n");
    }
}

/** Start keeping the errors in range_errors. */
static void errors_begin(void)
{
    range_errors[0] = '\0';
    range_level = ecs_log_set_level(-1);
    range_colors = ecs_log_enable_colors(false);
    range_log = ecs_os_api.log_;
    ecs_os_api.log_ = range_capture;
}

/** Stop keeping the errors. */
static void errors_end(void)
{
    ecs_os_api.log_ = range_log;
    ecs_log_enable_colors(range_colors);
    ecs_log_set_level(range_level);
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

    return range_errors;
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

void CurveRange_error_none(void) {
    // particle/curve_range
    const char *errors = curves_errors((ParticleCurves){
        .size = { CurveCatmullRom, BOX_CURVE_KEYS({0, 1}, {1, 0}),
            BOX_CURVE_KEYS({0, 2}, {0.5f, 3}, {1, 0.5f}) },
        .color = { CurveLinear, BOX_VEC_VIEW(ParticleGradientKey, {0, {1, 2, 3}}),
            BOX_VEC_VIEW(ParticleGradientKey, {0, {4, 5, 6}}, {1, {7, 8, 9}}) }
    });
    test_str(errors, "");
}

void CurveRange_error_max_keys(void) {
    // particle/curve_range
    const char *errors = curves_errors((ParticleCurves){
        .size = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1, 0}),
            BOX_CURVE_KEYS({0, 2}, {0.5f, 3}, {0.5f, 1}) },
        .opacity = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1.5f, 0}),
            BOX_CURVE_KEYS({0, 1}, {1, 0}) },
        .spin = { CurveBezier, BOX_CURVE_KEYS({0, 1}),
            BOX_CURVE_KEYS({0, 1, 0, NAN}) },
        .rotation = { CurveLinear, BOX_CURVE_KEYS({0, 1}),
            BOX_CURVE_KEYS({0, 1}, {1.25f, 0}) }
    });
    has(errors, "invalid ParticleCurves on smoke: size max_keys key 2: time 0.5 is not after "
        "time 0.5 of key 1");
    has(errors, "invalid ParticleCurves on smoke: opacity key 1: time 1.5 is outside of 0 to 1");
    has(errors, "invalid ParticleCurves on smoke: spin max_keys key 0: out is not finite");
    has(errors, "invalid ParticleCurves on smoke: rotation max_keys key 1: time 1.25 is "
        "outside of 0 to 1");
    test_assert(strstr(errors, "opacity max_keys") == NULL);
}

void CurveRange_error_gradient_max_keys(void) {
    // particle/curve_range
    const char *errors = curves_errors((ParticleCurves){
        .color = { CurveLinear, BOX_VEC_VIEW(ParticleGradientKey, {0, {1, 2, 3}}),
            BOX_VEC_VIEW(ParticleGradientKey, {0.5f, {1, 2, 3}}, {0.25f, {4, 5, 6}}) }
    });
    has(errors, "invalid ParticleCurves on smoke: color max_keys key 1: time 0.25 is not after "
        "time 0.5 of key 0");
}

void CurveRange_bad_max_keys_default(void) {
    // particle/curve_range
    int32_t level = ecs_log_set_level(-4);
    range_t r;
    range_init(&r, "size: {keys: [{0, 1}, {1, 0}], max_keys: [{0, 2}, {2, 0.5}]}");
    ecs_log_set_level(level);
    test_uint(r.block->curve_rows[0], BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(r.block->curve_max_rows[0], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(r.block->curve_values[0], 1);
    range_fini(&r);
}

void CurveRange_error_light(void) {
    // particle/curve_range
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t e = ecs_entity(world, { .name = "flash" });
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, PointLight, { .intensity = 1, .range = 10 });
    errors_begin();
    ecs_set(world, e, ParticleLight, {
        .intensity = 3,
        .curve = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 1}), BOX_CURVE_KEYS({0, 1}, {1, 2}) },
        .period = 1
    });
    errors_end();
    has(range_errors, "invalid ParticleLight on flash: curve max_keys is not allowed");

    int32_t level = ecs_log_set_level(-4);
    frame(world);
    frame(world);
    ecs_log_set_level(level);
    const PointLight *light = ecs_get(world, e, PointLight);
    test_assert(light != NULL);
    near(light->intensity, 3);

    ecs_fini(world);
    box_render_null_free(null);
}
