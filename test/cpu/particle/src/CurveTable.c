/**
 * @file CurveTable.c
 * @brief Tests for the curve table of the particle extract: the rows the curves of the emitters
 * are baked into, shared by equal curves, baked when a curve changes and handed out again when
 * nothing uses them.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "simulate.h"

/** Frame delta of the tests, a fixed clock */
#define DELTA (1.0f / 60.0f)

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

/** Create an emitter that spawns a particle every frame.
 *
 * @param world The world
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 1 / DELTA + 1;
    p->max = 100;
    p->lifetime = (ParticleRange){10, 10};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Create an emitter with curves whose size curve rises to a value, the rest constant.
 *
 * @param world The world
 * @param end The end key of the size curve
 * @return The emitter
 */
static ecs_entity_t sized_new(
    ecs_world_t *world,
    float end)
{
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleCurves, {
        .size = { .keys = BOX_CURVE_KEYS(
            {0, 0}, {1.0f / 3.0f, 1}, {2.0f / 3.0f, 2}, {1, end}) }
    });

    return e;
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

/** Find the upload into the curve table in the last packet.
 *
 * @param null The backend
 * @return The upload, NULL when the packet has none
 */
static const box_render_upload_t* table_upload(
    box_render_null_t *null)
{
    const box_render_upload_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == BOX_RENDER_BUFFER_PARTICLE_CURVES) {
            result = u;
        }
    }

    return result;
}

/** Find the create of the curve table in the last packet.
 *
 * @param null The backend
 * @return The size it is created with, 0 when the packet does not create it
 */
static uint64_t table_created(
    box_render_null_t *null)
{
    uint64_t result = 0;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_BUFFER && r->op == BOX_RENDER_CREATE &&
            r->handle == BOX_RENDER_BUFFER_PARTICLE_CURVES)
        {
            const box_render_buffer_desc_t *desc = box_render_packet_data(&null->last, r->data);
            result = desc->size;
        }
    }

    return result;
}

/** Get the counts of the curve table of a world.
 *
 * @param world The world
 * @return The counts
 */
static box_render_particle_curve_stats_t stats_of(
    ecs_world_t *world)
{
    box_render_particle_curve_stats_t result;
    box_render_particles_curve_stats(box_render_particles_get(world), &result);

    return result;
}

/** Evaluate the size curve of a pool from the curve table.
 *
 * @param world The world
 * @param pool The pool
 * @param t The normalized age
 * @return The size multiplier
 */
static float size_at(
    ecs_world_t *world,
    const box_render_particle_pool_t *pool,
    float t)
{
    const float *table = box_render_particles_curve_table(box_render_particles_get(world), NULL);

    return box_particle_curve(table, pool->block.curve_rows[0], pool->block.curve_values[0], t);
}

void CurveTable_block_reads_rows(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleCurves, {
        .size = { .keys = BOX_CURVE_KEYS({0, 1}, {1.0f / 3.0f, 2}, {2.0f / 3.0f, 3}, {1, 4}) },
        .opacity = { .keys = BOX_CURVE_KEYS({0, 0.5f}) },
        .rotation = { .keys = BOX_CURVE_KEYS({0, 0}) },
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey, {0, {255, 255, 255}}) },
        .spin = { .keys = BOX_CURVE_KEYS({0, 1}, {1.0f / 3.0f, 0}, {2.0f / 3.0f, 0}, {1, 1}) }
    });
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    test_assert(b->curve_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(b->curve_rows[1], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->curve_values[1], 0.5f);
    test_uint(b->curve_rows[2], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->curve_values[2], 0);
    test_assert(b->curve_rows[3] != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(b->curve_rows[3] != b->curve_rows[0]);
    test_uint(b->gradient_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->gradient[0], 1);
    test_flt(size_at(world, pool, 0), 1);
    test_flt(size_at(world, pool, 1.0f / 3.0f), 2);
    test_flt(size_at(world, pool, 2.0f / 3.0f), 3);
    test_flt(size_at(world, pool, 1), 4);

    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.entries, 2);
    test_int(st.used, 1 + BOX_PARTICLE_SPIN_ROWS);
    test_assert(table_created(null) >= (uint64_t)st.rows * BOX_PARTICLE_CURVE_SAMPLES * 4);
    const box_render_upload_t *u = table_upload(null);
    test_assert(u != NULL);
    test_uint(u->offset, 0);
    test_uint(u->size, (uint32_t)st.rows * BOX_PARTICLE_CURVE_SAMPLES * 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_row_holds_keys(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = sized_new(world, 7.5f);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const float *table = box_render_particles_curve_table(box_render_particles_get(world), NULL);
    const float *row = table + pool->block.curve_rows[0] * BOX_PARTICLE_CURVE_SAMPLES;
    test_assert(row[0] == 0);
    test_assert(row[21] == 1);
    test_assert(row[42] == 2);
    test_assert(row[63] == 7.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_equal_curves_share_rows(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = sized_new(world, 4);
    ecs_entity_t b = sized_new(world, 4);
    ecs_entity_t c = sized_new(world, 5);
    frame(world);

    const box_render_particle_pool_t *pa = pool_of(null, a);
    const box_render_particle_pool_t *pb = pool_of(null, b);
    const box_render_particle_pool_t *pc = pool_of(null, c);
    test_assert(pa != NULL && pb != NULL && pc != NULL);
    test_uint(pa->block.curve_rows[0], pb->block.curve_rows[0]);
    test_assert(pa->block.curve_rows[0] != pc->block.curve_rows[0]);
    test_flt(size_at(world, pb, 1), 4);
    test_flt(size_at(world, pc, 1), 5);

    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.entries, 2);
    test_int(st.used, 2);
    test_int(st.rows, 2);
    test_int(st.bakes, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_change_rebakes(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = sized_new(world, 4);
    frame(world);
    test_int(stats_of(world).bakes, 1);
    test_assert(table_upload(null) != NULL);

    frame(world);
    frame(world);
    test_int(stats_of(world).bakes, 1);
    test_assert(table_upload(null) == NULL);
    test_uint(table_created(null), 0);

    ParticleCurves *c = ecs_get_mut(world, e, ParticleCurves);
    ecs_vec_get_t(&c->size.keys, CurveKey, 3)->value = 6;
    ecs_modified(world, e, ParticleCurves);
    frame(world);
    test_int(stats_of(world).bakes, 2);
    test_assert(table_upload(null) != NULL);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(size_at(world, pool, 1), 6);
    test_int(stats_of(world).used, 1);

    frame(world);
    test_int(stats_of(world).bakes, 2);
    test_assert(table_upload(null) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_no_curves_no_rows(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world);
    ecs_entity_t d = emitter_new(world);
    ecs_add(world, d, ParticleCurves);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        test_uint(pool->block.curve_rows[i], BOX_PARTICLE_CURVE_CONSTANT);
    }

    test_uint(pool->block.gradient_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pool->curves, 0);
    pool = pool_of(null, d);
    test_assert(pool != NULL);
    test_uint(pool->curves, 0);

    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.rows, 0);
    test_int(st.used, 0);
    test_int(st.bakes, 0);
    test_int(st.capacity, 0);
    test_uint(table_created(null), 0);
    test_assert(table_upload(null) == NULL);
    test_assert(box_render_particles_curve_table(box_render_particles_get(world), NULL) == NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_delete_frees_rows(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = sized_new(world, 4);
    frame(world);
    const box_render_particle_pool_t *pool = pool_of(null, a);
    test_assert(pool != NULL);
    uint32_t row = pool->block.curve_rows[0];
    test_int(stats_of(world).used, 1);

    ecs_delete(world, a);
    frame(world);
    test_int(stats_of(world).used, 0);
    test_int(stats_of(world).entries, 0);

    ecs_entity_t b = sized_new(world, 9);
    frame(world);
    pool = pool_of(null, b);
    test_assert(pool != NULL);
    test_uint(pool->block.curve_rows[0], row);
    test_flt(size_at(world, pool, 1), 9);
    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.rows, 1);
    test_int(st.used, 1);
    test_int(st.bakes, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_growth_creates_table_again(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e[40];
    int32_t i;
    for (i = 0; i < 8; i ++) {
        e[i] = sized_new(world, 10.0f + (float)i);
    }

    frame(world);
    uint64_t first = table_created(null);
    test_uint(first, (uint64_t)BOX_RENDER_PARTICLE_CURVE_ROWS * BOX_PARTICLE_CURVE_SAMPLES * 4);
    test_int(stats_of(world).capacity, BOX_RENDER_PARTICLE_CURVE_ROWS);

    for (i = 8; i < 40; i ++) {
        e[i] = sized_new(world, 10.0f + (float)i);
    }

    frame(world);
    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.rows, 40);
    test_int(st.capacity, 2 * BOX_RENDER_PARTICLE_CURVE_ROWS);
    test_uint(table_created(null), 2 * first);
    const box_render_upload_t *u = table_upload(null);
    test_assert(u != NULL);
    test_uint(u->offset, 0);
    test_uint(u->size, 40 * BOX_PARTICLE_CURVE_SAMPLES * 4);
    for (i = 0; i < 40; i ++) {
        const box_render_particle_pool_t *pool = pool_of(null, e[i]);
        test_assert(pool != NULL);
        test_uint(pool->curves, BOX_RENDER_BUFFER_PARTICLE_CURVES);
        test_flt(size_at(world, pool, 1), 10.0f + (float)i);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

/** Run the world with the error log silenced, for curves the validate hook reports.
 *
 * @param world The world
 */
static void frame_quiet(
    ecs_world_t *world)
{
    int32_t level = ecs_log_set_level(-4);
    frame(world);
    ecs_log_set_level(level);
}

void CurveTable_bad_curve_default(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world);
    int32_t level = ecs_log_set_level(-4);
    ecs_set(world, e, ParticleCurves, {
        .size = { .keys = BOX_CURVE_KEYS({0, 3}, {1.5f, 4}) },
        .opacity = { .keys = BOX_CURVE_KEYS({0.5f, 2}, {0.5f, 0}) },
        .rotation = { .kind = (CurveKind)7, .keys = BOX_CURVE_KEYS({0, 1}, {1, 2}) },
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey,
            {0, {255, 0, 0}}, {-0.5f, {0, 0, 255}}) },
        .spin = { .keys = BOX_CURVE_KEYS({0, NAN}) }
    });
    ecs_log_set_level(level);
    frame_quiet(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        test_uint(b->curve_rows[i], BOX_PARTICLE_CURVE_CONSTANT);
    }

    test_flt(b->curve_values[0], 1);
    test_flt(b->curve_values[1], 1);
    test_flt(b->curve_values[2], 0);
    test_flt(b->curve_values[3], 1);
    test_uint(b->gradient_row, BOX_PARTICLE_CURVE_CONSTANT);
    for (i = 0; i < 3; i ++) {
        test_flt(b->gradient[i], 1);
    }

    test_uint(pool->curves, 0);
    test_int(stats_of(world).bakes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_not_finite_default(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleCurves, {
        .size = { .kind = CurveBezier,
            .keys = BOX_CURVE_KEYS({0, 0, 0, 3e38f}, {1, 3e38f, -3e38f, 0}) },
        .spin = { .kind = CurveCatmullRom,
            .keys = BOX_CURVE_KEYS({0, -3e38f}, {0.5f, 3e38f}, {1, -3e38f}) }
    });
    frame_quiet(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    test_uint(b->curve_rows[0], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->curve_values[0], 1);
    test_uint(b->curve_rows[3], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->curve_values[3], 1);
    test_int(stats_of(world).bakes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_one_key_constant(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleCurves, {
        .size = { .kind = CurveCatmullRom, .keys = BOX_CURVE_KEYS({0.4f, 2.5f}) },
        .opacity = { .kind = CurveCatmullRom,
            .keys = BOX_CURVE_KEYS({0, 0.5f}, {0.3f, 0.5f}, {1, 0.5f}) },
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey, {0.5f, {255, 0, 0}}) }
    });
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    test_uint(b->curve_rows[0], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->curve_values[0], 2.5f);
    test_uint(b->curve_rows[1], BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->curve_values[1], 0.5f);
    test_uint(b->gradient_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(b->gradient[0], 1);
    test_flt(b->gradient[1], 0);
    test_flt(b->gradient[2], 0);
    test_uint(pool->curves, 0);
    test_int(stats_of(world).rows, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_kinds_bake_rows(void) {
    // particle/curve_table
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    CurveKind kinds[3] = { CurveLinear, CurveCatmullRom, CurveBezier };
    ecs_entity_t e[3];
    int32_t k, i;
    for (k = 0; k < 3; k ++) {
        e[k] = emitter_new(world);
        ecs_set(world, e[k], ParticleCurves, {
            .size = { .kind = kinds[k],
                .keys = BOX_CURVE_KEYS({0, 0, 0, 4}, {0.5f, 1}, {1, 0}) }
        });
    }

    frame(world);
    const float *table = box_render_particles_curve_table(box_render_particles_get(world), NULL);
    test_assert(table != NULL);
    for (k = 0; k < 3; k ++) {
        const box_render_particle_pool_t *pool = pool_of(null, e[k]);
        test_assert(pool != NULL);
        const ParticleCurves *c = ecs_get(world, e[k], ParticleCurves);
        float row[BOX_PARTICLE_CURVE_SAMPLES];
        box_particle_curve_bake(&c->size, 1, row);
        const float *sent = table + pool->block.curve_rows[0] * BOX_PARTICLE_CURVE_SAMPLES;
        for (i = 0; i < BOX_PARTICLE_CURVE_SAMPLES; i ++) {
            test_assert(sent[i] == row[i]);
        }
    }

    test_assert(pool_of(null, e[0])->block.curve_rows[0] !=
        pool_of(null, e[1])->block.curve_rows[0]);
    test_assert(pool_of(null, e[1])->block.curve_rows[0] !=
        pool_of(null, e[2])->block.curve_rows[0]);
    test_int(stats_of(world).entries, 3);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Create an emitter with keyed ParticleMotion and no ParticleCurves.
 *
 * @param world The world
 * @param end The end key of the velocity_y curve
 * @return The emitter
 */
static ecs_entity_t moving_new(
    ecs_world_t *world,
    float end)
{
    ecs_entity_t e = emitter_new(world);
    ecs_set(world, e, ParticleMotion, {
        .velocity_y = { .keys = BOX_CURVE_KEYS({0, 0}, {0.5f, 1}, {1, end}) }
    });

    return e;
}

void CurveTable_seen_curves_not_sampled(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = sized_new(world, 4);
    frame(world);
    test_int(stats_of(world).samplings, 1);
    test_int(stats_of(world).bakes, 1);

    ecs_entity_t b = sized_new(world, 4);
    frame(world);
    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.samplings, 1);
    test_int(st.bakes, 1);
    test_int(st.entries, 1);
    test_int(st.used, 1);
    const box_render_particle_pool_t *pa = pool_of(null, a);
    const box_render_particle_pool_t *pb = pool_of(null, b);
    test_assert(pa != NULL && pb != NULL);
    test_uint(pa->block.curve_rows[0], pb->block.curve_rows[0]);
    test_flt(size_at(world, pb, 1), 4);
    test_uint(pb->curves, BOX_RENDER_BUFFER_PARTICLE_CURVES);

    frame(world);
    test_int(stats_of(world).samplings, 1);

    ecs_delete(world, a);
    frame(world);
    test_int(stats_of(world).used, 1);
    ecs_delete(world, b);
    frame(world);
    test_int(stats_of(world).used, 0);

    ecs_entity_t c = sized_new(world, 4);
    frame(world);
    st = stats_of(world);
    test_int(st.samplings, 1);
    test_int(st.bakes, 1);
    test_int(st.used, 1);
    const box_render_particle_pool_t *pc = pool_of(null, c);
    test_assert(pc != NULL);
    test_flt(size_at(world, pc, 1), 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_rewritten_rows_sampled_again(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = sized_new(world, 4);
    frame(world);
    ecs_delete(world, a);
    frame(world);
    test_int(stats_of(world).used, 0);

    ecs_entity_t c = sized_new(world, 5);
    frame(world);
    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.rows, 1);
    test_int(st.bakes, 2);

    ecs_entity_t d = sized_new(world, 4);
    frame(world);
    st = stats_of(world);
    test_int(st.samplings, 3);
    test_int(st.bakes, 3);
    test_int(st.used, 2);
    const box_render_particle_pool_t *pc = pool_of(null, c);
    const box_render_particle_pool_t *pd = pool_of(null, d);
    test_assert(pc != NULL && pd != NULL);
    test_assert(pc->block.curve_rows[0] != pd->block.curve_rows[0]);
    test_flt(size_at(world, pc, 1), 5);
    test_flt(size_at(world, pd, 1), 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_change_back_takes_known_rows(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = sized_new(world, 4);
    ecs_entity_t keep = sized_new(world, 4);
    frame(world);
    test_int(stats_of(world).samplings, 1);

    ParticleCurves *c = ecs_get_mut(world, e, ParticleCurves);
    ecs_vec_get_t(&c->size.keys, CurveKey, 3)->value = 6;
    ecs_modified(world, e, ParticleCurves);
    frame(world);
    test_int(stats_of(world).samplings, 2);
    test_int(stats_of(world).bakes, 2);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(size_at(world, pool, 1), 6);

    c = ecs_get_mut(world, e, ParticleCurves);
    ecs_vec_get_t(&c->size.keys, CurveKey, 3)->value = 4;
    ecs_modified(world, e, ParticleCurves);
    frame(world);
    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.samplings, 2);
    test_int(st.bakes, 2);
    test_int(st.used, 1);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(size_at(world, pool, 1), 4);
    test_uint(pool->block.curve_rows[0], pool_of(null, keep)->block.curve_rows[0]);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_in_place_write_rebakes(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = sized_new(world, 4);
    frame(world);
    test_int(stats_of(world).bakes, 1);

    ParticleCurves *c = ecs_get_mut(world, e, ParticleCurves);
    ecs_vec_get_t(&c->size.keys, CurveKey, 3)->value = 9;
    frame(world);
    test_int(stats_of(world).bakes, 2);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_flt(size_at(world, pool, 1), 9);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_seen_motion_not_sampled(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = moving_new(world, 3);
    frame(world);
    test_int(stats_of(world).samplings, 1);
    int64_t bakes = stats_of(world).bakes;
    test_assert(bakes > 0);

    ecs_entity_t b = moving_new(world, 3);
    frame(world);
    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.samplings, 1);
    test_int(st.bakes, bakes);
    const box_render_particle_pool_t *pa = pool_of(null, a);
    const box_render_particle_pool_t *pb = pool_of(null, b);
    test_assert(pa != NULL && pb != NULL);
    test_assert(pa->block.motion_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pa->block.motion_rows[0], pb->block.motion_rows[0]);
    test_uint(pb->curves, BOX_RENDER_BUFFER_PARTICLE_CURVES);

    ecs_entity_t c = moving_new(world, 5);
    frame(world);
    test_int(stats_of(world).samplings, 2);
    const box_render_particle_pool_t *pc = pool_of(null, c);
    test_assert(pc != NULL);
    test_assert(pc->block.motion_rows[0] != pa->block.motion_rows[0]);

    ecs_fini(world);
    box_render_null_free(null);
}

void CurveTable_motion_from_prefab(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, ParticleMotion, {
        .velocity_y = { .keys = BOX_CURVE_KEYS({0, 0}, {0.5f, 1}, {1, 3}) }
    });
    ecs_entity_t a = emitter_new(world);
    ecs_add_pair(world, a, EcsIsA, base);
    ecs_entity_t b = emitter_new(world);
    ecs_add_pair(world, b, EcsIsA, base);
    ecs_entity_t plain = emitter_new(world);
    frame(world);

    test_int(stats_of(world).samplings, 1);
    const box_render_particle_pool_t *pa = pool_of(null, a);
    const box_render_particle_pool_t *pb = pool_of(null, b);
    const box_render_particle_pool_t *pp = pool_of(null, plain);
    test_assert(pa != NULL && pb != NULL && pp != NULL);
    test_assert(pa->block.motion_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pa->block.motion_rows[0], pb->block.motion_rows[0]);
    test_uint(pp->block.motion_rows[0], BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pp->curves, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Write the last key of the size curve of an emitter.
 *
 * @param world The world
 * @param e The emitter
 * @param end The value
 */
static void sized_set(
    ecs_world_t *world,
    ecs_entity_t e,
    float end)
{
    ParticleCurves *c = ecs_get_mut(world, e, ParticleCurves);
    ecs_vec_get_t(&c->size.keys, CurveKey, 3)->value = end;
    ecs_modified(world, e, ParticleCurves);
}

void CurveTable_change_reuses_freed_rows(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = sized_new(world, 4);
    ecs_entity_t b = sized_new(world, 5);
    frame(world);
    test_int(stats_of(world).rows, 2);

    sized_set(world, a, 6);
    frame(world);
    box_render_particle_curve_stats_t st = stats_of(world);
    test_int(st.rows, 3);
    test_int(st.used, 2);

    sized_set(world, b, 7);
    frame(world);
    st = stats_of(world);
    test_int(st.rows, 3);
    test_int(st.used, 2);
    test_int(st.bakes, 4);
    const box_render_particle_pool_t *pa = pool_of(null, a);
    const box_render_particle_pool_t *pb = pool_of(null, b);
    test_assert(pa != NULL && pb != NULL);
    test_flt(size_at(world, pa, 1), 6);
    test_flt(size_at(world, pb, 1), 7);

    ecs_entity_t c = sized_new(world, 6);
    frame(world);
    st = stats_of(world);
    test_int(st.rows, 3);
    test_int(st.bakes, 4);
    const box_render_particle_pool_t *pc = pool_of(null, c);
    test_assert(pc != NULL);
    test_uint(pc->block.curve_rows[0], pool_of(null, a)->block.curve_rows[0]);

    ecs_fini(world);
    box_render_null_free(null);
}
