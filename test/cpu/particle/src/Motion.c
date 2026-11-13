/**
 * @file Motion.c
 * @brief Tests for ParticleMotion: its values, its errors, its rows in the block, and the C twin
 * of the integrate pass with its curves, each alone against a closed form or a numeric
 * reference, constant curves against the matching ParticleForces, the stretch along the velocity
 * a particle moves with, the ground plane under a velocity curve and random between two curves.
 *
 * The twin runs on a fixed delta of DT, the integrate pass of particle_sim.wgsl step by step:
 * age the particle, evaluate its curves at its age after the frame, integrate, collide.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "simulate.h"

/** Fixed frame delta of the twin, in seconds */
#define DT (0.01f)

/** Fixed frame delta of the twin, in microseconds */
#define DT_US (10000u)

/** Lifetime of the particles of the twin, in microseconds */
#define LIFE_US (1000000u)

/** Frames the twin runs, short of the lifetime */
#define FRAMES (80)

/** Frame delta of the tests that run the extract */
#define DELTA (0.1f)

/** Seed of the emitters and particles of the tests */
#define SEED (7u)

/** Tolerance of comparisons with a closed form, relative to its size */
#define EPS (1e-4f)

/** Floats of the table of the twin tests: the rows of the velocity vector, then of the force */
#define TABLE (2 * BOX_PARTICLE_VECTOR_ROWS * BOX_PARTICLE_CURVE_SAMPLES)

/** Index of the velocity x curve in the channels of a ParticleMotion: the velocity vector, then
 * the force vector */
#define VX (0)

/** Index of the velocity y curve */
#define VY (1)

/** Index of the velocity z curve */
#define VZ (2)

/** Index of the speed curve */
#define SPEED (3)

/** Index of the force x curve */
#define FX (4)

/** Index of the force y curve */
#define FY (5)

/** Index of the drag curve */
#define DRAG (7)

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
    bool ok = fabsf(a - b) <= eps * fmaxf(1.0f, fabsf(b));
    if (!ok) {
        printf("%.9g is not near %.9g\n", (double)a, (double)b);
    }

    test_assert(ok);
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

/** Check that two vectors are the same bit for bit.
 *
 * @param a The vector
 * @param b The expected vector
 */
static void same3(
    const float *a,
    const float *b)
{
    test_assert(!memcmp(a, b, 3 * sizeof(float)));
}

/** The rows of a ParticleMotion without curves: both vectors constant at their defaults.
 *
 * @param out Receives the rows
 */
static void rows_none(
    box_particle_motion_rows_t *out)
{
    *out = (box_particle_motion_rows_t){
        .velocity_row = BOX_PARTICLE_CURVE_CONSTANT,
        .velocity_max_row = BOX_PARTICLE_CURVE_CONSTANT,
        .force_row = BOX_PARTICLE_CURVE_CONSTANT,
        .force_max_row = BOX_PARTICLE_CURVE_CONSTANT,
        .velocity = {0, 0, 0, 1},
        .force = {0, 0, 0, 1}
    };
}

/** Get the constant of a channel of the rows.
 *
 * @param rows The rows
 * @param i Index of the channel, VX to DRAG
 * @return The constant
 */
static float* rows_value(
    box_particle_motion_rows_t *rows,
    int32_t i)
{
    return i < 4 ? &rows->velocity[i] : &rows->force[i - 4];
}

/** Bake a linear curve into channel i of the rows of its vector in a table, the velocity
 * vector at row 0 and the force vector after it, the other channels at their constants.
 *
 * @param table The table, TABLE floats
 * @param rows The rows
 * @param i Index of the channel, VX to DRAG
 * @param keys The keys
 */
static void rows_bake(
    float *table,
    box_particle_motion_rows_t *rows,
    int32_t i,
    ecs_vec_t keys)
{
    uint32_t *row = i < 4 ? &rows->velocity_row : &rows->force_row;
    float *values = i < 4 ? rows->velocity : rows->force;
    float samples[BOX_PARTICLE_CURVE_SAMPLES];
    int32_t c = i % 4, k, j;
    if (*row == BOX_PARTICLE_CURVE_CONSTANT) {
        *row = i < 4 ? 0 : BOX_PARTICLE_VECTOR_ROWS;
        for (k = 0; k < BOX_PARTICLE_CURVE_SAMPLES; k ++) {
            for (j = 0; j < 4; j ++) {
                table[*row * BOX_PARTICLE_CURVE_SAMPLES + k * 4 + j] = values[j];
            }
        }
    }

    ParticleCurve curve = { .kind = CurveLinear, .keys = keys };
    box_particle_curve_bake(&curve, values[c], samples);
    for (k = 0; k < BOX_PARTICLE_CURVE_SAMPLES; k ++) {
        table[*row * BOX_PARTICLE_CURVE_SAMPLES + k * 4 + c] = samples[k];
    }
}

/** Evaluate channel i of the rows of a ParticleMotion at a normalized age, without a max.
 *
 * @param table The table
 * @param rows The rows
 * @param i Index of the channel, VX to DRAG
 * @param t The normalized age
 * @return The value
 */
static float rows_eval(
    const float *table,
    const box_particle_motion_rows_t *rows,
    int32_t i,
    float t)
{
    float out[4];
    if (i < 4) {
        box_particle_vector(table, rows->velocity_row, rows->velocity, t, out);
    } else {
        box_particle_vector(table, rows->force_row, rows->force, t, out);
    }

    return out[i % 4];
}

/** Forces of a test: none, and no gravity.
 *
 * @return The forces
 */
static box_particle_forces_t forces_none(void)
{
    return (box_particle_forces_t){ .scale = 1.0f };
}

/** A particle the twin runs */
typedef struct twin_t {
    float position[3];      /**< World position */
    float velocity[3];      /**< Stored velocity */
    uint32_t remaining;     /**< Microseconds left, 0 once dead */
    float last[3];          /**< Velocity and speed of its motion in the last frame, as the
                             *   draw sees it: speed times velocity plus the velocity curve */
} twin_t;

/** Run a frame of the integrate pass of the twin on a particle of spawn index 0 and lifetime
 * LIFE_US.
 *
 * @param p The particle
 * @param table The curve table, NULL for an emitter without ParticleMotion
 * @param rows The rows of its ParticleMotion, NULL for none
 * @param forces The forces
 * @param ground The ground plane, NULL for none
 */
static void twin_step(
    twin_t *p,
    const float *table,
    const box_particle_motion_rows_t *rows,
    const box_particle_forces_t *forces,
    const box_particle_ground_t *ground)
{
    box_particle_motion_t motion;
    if (!p->remaining) {
        goto done;
    }

    p->remaining = box_particle_age(p->remaining, DT_US);
    if (!p->remaining) {
        goto done;
    }

    if (rows) {
        box_particle_motion(table, rows, SEED, 0, box_particle_life_age(LIFE_US, p->remaining),
            &motion);
    }

    box_particle_integrate(p->position, p->velocity, forces, rows ? &motion : NULL, DT);
    box_particle_moving(p->velocity, rows ? &motion : NULL, p->last);
    if (ground && !box_particle_collide(p->position, p->velocity, ground)) {
        p->remaining = 0;
    }
done:
    return;
}

/** A particle at rest at the origin with its whole life ahead.
 *
 * @return The particle
 */
static twin_t twin_new(void)
{
    return (twin_t){ .remaining = LIFE_US };
}

/** Normalized age of the particles of the twin after n frames, as the pass computes it.
 *
 * @param n The frames
 * @return The age
 */
static float age_after(
    int32_t n)
{
    return box_particle_life_age(LIFE_US, LIFE_US - (uint32_t)n * DT_US);
}

/** An emitter of a test in a world that renders into a null backend */
typedef struct scene_t {
    ecs_world_t *world;         /**< The world */
    box_render_null_t *null;    /**< The backend */
    ecs_entity_t e;             /**< The emitter */
    const box_render_particle_pool_t *pool; /**< Its pool in the last packet */
    const float *table;         /**< The curve table */
    int32_t rows;               /**< Rows of the curve table */
} scene_t;

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

/** Read the pool and the curve table of the emitter of a test after the last frame.
 *
 * @param s The test
 */
static void scene_read(
    scene_t *s)
{
    int32_t i;
    s->pool = NULL;
    for (i = 0; i < ecs_vec_count(&s->null->last.particles); i ++) {
        const box_render_particle_pool_t *p = ecs_vec_get_t(
            &s->null->last.particles, box_render_particle_pool_t, i);
        if (p->entity == s->e) {
            s->pool = p;
        }
    }

    test_assert(s->pool != NULL);
    s->table = box_render_particles_curve_table(box_render_particles_get(s->world), &s->rows);
}

/** Create an emitter named jet with the seed SEED and more components in script, and run a
 * frame.
 *
 * @param s Receives the test
 * @param components Components of the emitter in script, besides its ParticleEmitter
 */
static void scene_init(
    scene_t *s,
    const char *components)
{
    char code[2048];
    ecs_os_memset_t(s, 0, scene_t);
    s->world = ecs_init();
    ECS_IMPORT(s->world, Box);
    s->null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(s->null);
    box_render_set_backend(s->world, &backend);
    snprintf(code, sizeof(code),
        "using box.curve\n"
        "using box.particle\n"
        "using box.transform\n"
        "jet {\n"
        "  Position: {0, 0, 0}\n"
        "  Emitter: {rate: 20, lifetime: {1, 1}, seed: %u}\n"
        "  %s\n"
        "}\n", SEED, components);
    test_int(ecs_script_run(s->world, "motion", code, NULL), 0);
    s->e = ecs_lookup(s->world, "jet");
    test_assert(s->e != 0);
    frame(s->world);
    scene_read(s);
}

/** Free a test.
 *
 * @param s The test
 */
static void scene_fini(
    scene_t *s)
{
    ecs_fini(s->world);
    box_render_null_free(s->null);
}

/** Get the rows of the ParticleMotion of the block of a test.
 *
 * @param s The test
 * @param out Receives the rows
 */
static void scene_rows(
    const scene_t *s,
    box_particle_motion_rows_t *out)
{
    const box_render_particle_block_t *b = &s->pool->block;
    out->velocity_row = b->motion_rows[0];
    out->velocity_max_row = b->motion_rows[1];
    out->force_row = b->motion_rows[2];
    out->force_max_row = b->motion_rows[3];
    memcpy(out->velocity, b->motion_velocity, sizeof(out->velocity));
    memcpy(out->force, b->motion_force, sizeof(out->force));
}

/** Check that the ParticleMotion of the block of a test changes nothing.
 *
 * @param s The test
 */
static void scene_no_motion(
    const scene_t *s)
{
    box_particle_motion_rows_t rows, none;
    rows_none(&none);
    scene_rows(s, &rows);
    test_uint(s->pool->block.motion, 0);
    test_assert(!memcmp(&rows, &none, sizeof(rows)));
}

/** Errors logged while capturing */
static char motion_errors[4096];

/** Log level before capturing */
static int32_t motion_level;

/** Log function before capturing */
static ecs_os_api_log_t motion_log;

/** Whether the log was colored before capturing */
static bool motion_colors;

/** Log function that keeps the errors.
 *
 * @param level The level
 * @param file The file
 * @param line The line
 * @param msg The message
 */
static void motion_capture(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level <= -3 && strlen(motion_errors) + strlen(msg) + 2 < sizeof(motion_errors)) {
        strcat(motion_errors, msg);
        strcat(motion_errors, "\n");
    }
}

/** Start keeping the errors in motion_errors. */
static void errors_begin(void)
{
    motion_errors[0] = '\0';
    motion_level = ecs_log_set_level(-1);
    motion_colors = ecs_log_enable_colors(false);
    motion_log = ecs_os_api.log_;
    ecs_os_api.log_ = motion_capture;
}

/** Stop keeping the errors. */
static void errors_end(void)
{
    ecs_os_api.log_ = motion_log;
    ecs_log_enable_colors(motion_colors);
    ecs_log_set_level(motion_level);
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

void Motion_c_defaults(void) {
    // particle/motion
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleMotion);
    const ParticleMotion *m = ecs_get(world, e, ParticleMotion);
    test_assert(m != NULL);
    const ParticleCurve *curves[] = {
        &m->velocity_x, &m->velocity_y, &m->velocity_z, &m->force_x, &m->force_y,
        &m->force_z, &m->speed, &m->drag
    };
    int32_t i;
    for (i = 0; i < 8; i ++) {
        test_int(curves[i]->kind, CurveLinear);
        test_int(ecs_vec_count(&curves[i]->keys), 0);
        test_int(ecs_vec_count(&curves[i]->max_keys), 0);
    }

    ecs_fini(world);
}

void Motion_script_values(void) {
    // particle/motion
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(ecs_script_run(world, "motion",
        "using box.curve\n"
        "using box.particle\n"
        "plain { Motion: {} }\n"
        "custom { Motion: {\n"
        "  velocity_y: {kind: CurveCatmullRom, keys: [{0, 0}, {0.5, 0}, {1, 2}]}\n"
        "  force_x: {keys: [{0, 1}], max_keys: [{0, 3}]}\n"
        "  drag: {keys: [{0, 0}, {0.2, 4}]}\n"
        "} }\n", NULL), 0);
    const ParticleMotion *m = ecs_get(world, ecs_lookup(world, "plain"), ParticleMotion);
    test_assert(m != NULL);
    test_int(ecs_vec_count(&m->velocity_y.keys), 0);
    test_int(ecs_vec_count(&m->drag.keys), 0);

    m = ecs_get(world, ecs_lookup(world, "custom"), ParticleMotion);
    test_assert(m != NULL);
    test_int(m->velocity_y.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&m->velocity_y.keys), 3);
    test_flt(ecs_vec_get_t(&m->velocity_y.keys, CurveKey, 2)->value, 2);
    test_int(ecs_vec_count(&m->force_x.max_keys), 1);
    test_flt(ecs_vec_first_t(&m->force_x.max_keys, CurveKey)->value, 3);
    test_flt(ecs_vec_get_t(&m->drag.keys, CurveKey, 1)->time, 0.2f);
    test_int(ecs_vec_count(&m->speed.keys), 0);
    ecs_fini(world);
}

void Motion_error_names_field(void) {
    // particle/motion
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_entity(world, { .name = "smoke" });
    errors_begin();
    ecs_set(world, e, ParticleMotion, {
        .velocity_z = { CurveLinear, BOX_CURVE_KEYS({0, 1}, {1.5f, 0}) },
        .drag = { CurveLinear, BOX_CURVE_KEYS({0, 1}), BOX_CURVE_KEYS({0.5f, 1}, {0.25f, 2}) }
    });
    errors_end();
    has(motion_errors, "invalid ParticleMotion on smoke: velocity_z key 1: time 1.5 is outside "
        "of 0 to 1");
    has(motion_errors, "invalid ParticleMotion on smoke: drag max_keys key 1: time 0.25 is not "
        "after time 0.5 of key 0");
    test_assert(strstr(motion_errors, "speed") == NULL);
    ecs_fini(world);
}

void Motion_no_component_unchanged(void) {
    // particle/motion
    box_particle_forces_t f = {
        .acceleration = {0.5f, -9.81f, 0.25f}, .wind = {3, 0, -1}, .drag = 1.5f,
        .turbulence = 2, .scale = 0.7f, .phase = {1, 2, 3}, .drift = {0.3f, 0, -0.2f}
    };
    float p[3] = {0.1f, 2, -0.3f}, v[3] = {1, 4, -2};
    float q[3], w[3];
    int32_t i, c;
    memcpy(q, p, sizeof(q));
    memcpy(w, v, sizeof(w));
    for (i = 0; i < FRAMES; i ++) {
        float wander[3], keep = expf(-f.drag * DT);
        box_particle_integrate(p, v, &f, NULL, DT);
        box_particle_wander(q, f.phase, f.scale, f.turbulence, wander);
        for (c = 0; c < 3; c ++) {
            w[c] += (f.acceleration[c] + wander[c]) * DT;
            w[c] = f.wind[c] + (w[c] - f.wind[c]) * keep;
            q[c] += (w[c] + f.drift[c]) * DT;
        }

        same3(p, q);
        same3(v, w);
    }

    scene_t s;
    scene_init(&s, "Curves: {size: {keys: [{0, 1}, {1, 0}]}}");
    scene_no_motion(&s);
    test_int(s.rows, 1);
    scene_fini(&s);

    scene_init(&s, "Motion: {}");
    scene_no_motion(&s);
    test_int(s.rows, 0);
    test_uint(s.pool->curves, 0);
    scene_fini(&s);
}

void Motion_defaults_change_nothing(void) {
    // particle/motion
    box_particle_forces_t f = {
        .acceleration = {0, -9.81f, 0}, .wind = {2, 0, 0}, .drag = 3, .turbulence = 1,
        .scale = 1, .drift = {0, 0, 0.5f}
    };
    box_particle_motion_t defaults = {
        .velocity = {0, 0, 0}, .speed = 1, .force = {0, 0, 0}, .drag = 1
    };
    float p[3] = {0, 1, 0}, v[3] = {2, 5, -1}, q[3] = {0, 1, 0}, w[3] = {2, 5, -1};
    int32_t i;
    for (i = 0; i < FRAMES; i ++) {
        box_particle_integrate(p, v, &f, NULL, DT);
        box_particle_integrate(q, w, &f, &defaults, DT);
        same3(p, q);
        same3(v, w);
    }

    scene_t s;
    scene_init(&s, "Motion: {speed: {keys: [{0, 1}]}, drag: {keys: [{0, 1}, {1, 1}]}, "
        "velocity_x: {keys: [{0, 0}]}}");
    scene_no_motion(&s);
    test_int(s.rows, 0);
    scene_fini(&s);
}

void Motion_velocity_alone(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = forces_none();
    twin_t p = twin_new();
    int32_t n;
    rows_none(&rows);
    rows_bake(table, &rows, VY, BOX_CURVE_KEYS({0, 0}, {1, 3}));
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);

        near(p.position[1], 3.0f * DT * DT * (float)(n * (n + 1)) / 2.0f);
        test_flt(p.position[0], 0);
        test_flt(p.velocity[0], 0);
        test_flt(p.velocity[1], 0);
        test_flt(p.velocity[2], 0);
    }

    float t = FRAMES * DT;
    test_assert(fabsf(p.position[1] - 1.5f * t * t) < 3.0f * DT * t);
}

void Motion_force_alone(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = forces_none();
    twin_t p = twin_new();
    double v = 0, x = 0;
    int32_t n;
    rows_none(&rows);
    rows_bake(table, &rows, FX, BOX_CURVE_KEYS({0, 2}, {1, -4}));
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);
        double t = (double)n * DT, a = 2.0 - 6.0 * t;
        v += a * DT;
        x += v * DT;
        near(p.velocity[0], (float)v);
        near(p.position[0], (float)x);
        test_flt(p.position[1], 0);
    }

    double t = FRAMES * DT;
    test_assert(fabs(p.velocity[0] - (2 * t - 3 * t * t)) < 6.0 * DT);
}

void Motion_speed_alone(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = forces_none();
    twin_t p = twin_new();
    int32_t n;
    p.velocity[0] = 2;
    p.velocity[2] = -1;
    rows_none(&rows);
    rows_bake(table, &rows, SPEED, BOX_CURVE_KEYS({0, 1}, {1, 0}));
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);

        float moved = (float)n * DT - DT * DT * (float)(n * (n + 1)) / 2.0f;
        near(p.position[0], 2 * moved);
        near(p.position[2], -moved);
        test_flt(p.velocity[0], 2);
        test_flt(p.velocity[2], -1);
    }

    rows_none(&rows);
    *rows_value(&rows, SPEED) = 0.5f;
    p = twin_new();
    p.velocity[1] = 4;
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);
        near(p.position[1], 2.0f * DT * (float)n);
    }
}

void Motion_drag_alone(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = forces_none();
    twin_t p = twin_new();
    double x = 0;
    int32_t n;
    f.drag = 1.5f;
    p.velocity[2] = 4;
    rows_none(&rows);
    rows_bake(table, &rows, DRAG, BOX_CURVE_KEYS({0, 0}, {1, 2}));
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);

        double v = 4.0 * exp(-1.5 * 2.0 * (double)DT * DT * (double)(n * (n + 1)) / 2.0);
        x += v * DT;
        near(p.velocity[2], (float)v);
        near(p.position[2], (float)x);
    }

    p = twin_new();
    p.velocity[2] = 4;
    twin_step(&p, table, &rows, &f, NULL);
    near(p.velocity[2], 4.0f * expf(-1.5f * 2.0f * DT * DT));

    rows_none(&rows);
    *rows_value(&rows, DRAG) = -2;
    p = twin_new();
    p.velocity[2] = 4;
    twin_step(&p, table, &rows, &f, NULL);
    test_flt(p.velocity[2], 4);
}

void Motion_constant_velocity_is_drift(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = {
        .acceleration = {0, -9.81f, 0}, .wind = {1, 0, 0}, .drag = 2, .turbulence = 3,
        .scale = 1, .phase = {0.5f, 1, 2}
    };
    box_particle_forces_t drift = f;
    twin_t p = twin_new(), q = twin_new();
    int32_t n;
    rows_none(&rows);
    *rows_value(&rows, VX) = 2;
    *rows_value(&rows, VZ) = -1;
    drift.drift[0] = 2;
    drift.drift[2] = -1;
    p.velocity[1] = q.velocity[1] = 5;
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);
        twin_step(&q, NULL, NULL, &drift, NULL);
        same3(p.position, q.position);
        same3(p.velocity, q.velocity);
    }
}

void Motion_constant_force_is_gravity(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = {
        .wind = {0, 0, 2}, .drag = 0.5f, .turbulence = 2, .scale = 1.5f, .phase = {3, 2, 1}
    };
    box_particle_forces_t gravity = f;
    twin_t p = twin_new(), q = twin_new();
    int32_t n;
    rows_none(&rows);
    *rows_value(&rows, FX) = 1;
    *rows_value(&rows, FY) = -9.81f;
    gravity.acceleration[0] = 1;
    gravity.acceleration[1] = -9.81f;
    p.velocity[0] = q.velocity[0] = 3;
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);
        twin_step(&q, NULL, NULL, &gravity, NULL);
        same3(p.position, q.position);
        same3(p.velocity, q.velocity);
    }
}

void Motion_constant_drag_is_drag(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = {
        .acceleration = {0, -9.81f, 0}, .wind = {4, 0, 0}, .drag = 0.75f, .scale = 1
    };
    box_particle_forces_t drag = f;
    twin_t p = twin_new(), q = twin_new();
    int32_t n;
    rows_none(&rows);
    *rows_value(&rows, DRAG) = 4;
    drag.drag = f.drag * 4;
    p.velocity[1] = q.velocity[1] = 6;
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);
        twin_step(&q, NULL, NULL, &drag, NULL);
        same3(p.position, q.position);
        same3(p.velocity, q.velocity);
    }
}

void Motion_block_reads_rows(void) {
    // particle/motion
    scene_t s;
    box_particle_motion_rows_t rows;
    scene_init(&s, "Motion: {velocity_y: {keys: [{0, 0}, {0.5, 0}, {1, 2}]}, "
        "drag: {keys: [{0, 3}]}, speed: {kind: CurveCatmullRom, keys: [{0, 1}, {1, 0.2}]}}");
    const box_render_particle_block_t *b = &s.pool->block;
    scene_rows(&s, &rows);
    test_uint(b->motion, 1);
    test_uint(s.pool->curves, BOX_RENDER_BUFFER_PARTICLE_CURVES);
    test_int(s.rows, BOX_PARTICLE_VECTOR_ROWS);
    test_assert(rows.velocity_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(rows.velocity_max_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(rows.force_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(rows.force_max_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(rows.force[3], 3);
    test_flt(rows.force[1], 0);

    near(rows_eval(s.table, &rows, VY, 0.25f), 0);
    near(rows_eval(s.table, &rows, VY, 1), 2);
    near(rows_eval(s.table, &rows, SPEED, 0), 1);
    near(rows_eval(s.table, &rows, SPEED, 1), 0.2f);
    test_flt(rows_eval(s.table, &rows, VX, 0.5f), 0);
    test_flt(rows_eval(s.table, &rows, VZ, 0.5f), 0);

    box_particle_motion_t m;
    box_particle_motion(s.table, &rows, SEED, 3, 1, &m);
    near(m.velocity[1], 2);
    near(m.speed, 0.2f);
    test_flt(m.drag, 3);
    test_flt(m.force[0], 0);

    box_render_particle_curve_stats_t stats;
    box_render_particles_curve_stats(box_render_particles_get(s.world), &stats);
    test_int(stats.used, BOX_PARTICLE_VECTOR_ROWS);
    ecs_delete(s.world, s.e);
    frame(s.world);
    box_render_particles_curve_stats(box_render_particles_get(s.world), &stats);
    test_int(stats.used, 0);
    scene_fini(&s);
}

void Motion_constants_take_no_row(void) {
    // particle/motion
    scene_t s;
    box_particle_motion_rows_t rows;
    scene_init(&s, "Motion: {velocity_y: {keys: [{0, 2}]}, "
        "force_x: {keys: [{0, -1}, {1, -1}]}, drag: {keys: [{0, 0.5}], max_keys: [{0, 0.5}]}}");
    scene_rows(&s, &rows);
    test_uint(s.pool->block.motion, 1);
    test_uint(s.pool->curves, 0);
    test_int(s.rows, 0);
    test_uint(rows.velocity_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(rows.velocity_max_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(rows.force_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(rows.force_max_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_flt(rows.velocity[0], 0);
    test_flt(rows.velocity[1], 2);
    test_flt(rows.velocity[3], 1);
    test_flt(rows.force[0], -1);
    test_flt(rows.force[3], 0.5f);
    scene_fini(&s);

    scene_init(&s, "Motion: {force_z: {keys: [{0, 1}], max_keys: [{0, 3}]}}");
    scene_rows(&s, &rows);
    test_uint(s.pool->block.motion, 1);
    test_uint(rows.velocity_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(rows.force_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(rows.force_max_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_int(s.rows, 2 * BOX_PARTICLE_VECTOR_ROWS);
    box_particle_motion_t m;
    box_particle_motion(s.table, &rows, SEED, 5, 0.5f, &m);
    near(m.force[2], 1 + 2 * box_particle_motion_blend(SEED, 5, BOX_PARTICLE_MOTION_FORCE));
    test_flt(m.force[0], 0);
    test_flt(m.drag, 1);
    scene_fini(&s);
}

void Motion_stretch_sees_velocity(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = forces_none();
    twin_t p = twin_new();
    float moving[3], v[3] = {1, 0, 0};
    int32_t n;
    box_particle_motion_t m = {
        .velocity = {0, 2, 0}, .speed = 0.5f, .force = {0, 0, 0}, .drag = 1
    };
    box_particle_moving(v, &m, moving);
    test_flt(moving[0], 0.5f);
    test_flt(moving[1], 2);
    test_flt(moving[2], 0);
    box_particle_moving(v, NULL, moving);
    same3(moving, v);

    rows_none(&rows);
    rows_bake(table, &rows, VY, BOX_CURVE_KEYS({0, 0}, {1, 3}));
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);
        box_particle_motion_velocity(table, &rows, SEED, 0, age_after(n), &m);
        box_particle_moving(p.velocity, &m, moving);
        same3(moving, p.last);
        test_flt(p.velocity[1], 0);
        near(moving[1], 3.0f * age_after(n));
        near(box_particle_stretch(0.5f, moving[1]), 1 + 1.5f * age_after(n));
    }

    scene_t s;
    scene_init(&s, "Appearance: {stretch: 0.5}\n"
        "  Motion: {velocity_x: {keys: [{0, 4}, {1, 0}]}}");
    test_uint(s.pool->block.motion, 1);
    test_flt(s.pool->block.stretch, 0.5f);
    scene_rows(&s, &rows);
    box_particle_motion_velocity(s.table, &rows, SEED, 0, 0.25f, &m);
    near(m.velocity[0], 3);
    test_flt(m.speed, 1);
    scene_fini(&s);
}

void Motion_ground_velocity_curve(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = forces_none();
    box_particle_ground_t g = {
        .mode = BOX_PARTICLE_GROUND_BOUNCE, .height = 0, .bounce = 0.5f, .friction = 0.5f
    };
    twin_t p = twin_new();
    int32_t n, landed = 0;
    rows_none(&rows);
    *rows_value(&rows, VX) = 1;
    *rows_value(&rows, VY) = -3;
    p.position[1] = 0.5f;
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, &g);
        test_assert(p.remaining != 0);
        test_assert(p.position[1] >= 0);
        near(p.position[0], DT * (float)n);
        if (p.position[1] == 0) {
            landed = landed ? landed : n;
        } else {
            test_assert(!landed);
        }

        test_flt(p.velocity[0], 0);
        test_flt(p.velocity[1], 0);
        test_flt(p.velocity[2], 0);
    }

    test_assert(landed > 0 && landed <= 17);

    p = twin_new();
    p.position[1] = 0.01f;
    p.velocity[1] = -2;
    p.velocity[0] = 2;
    twin_step(&p, table, &rows, &f, &g);
    test_flt(p.position[1], 0);
    near(p.velocity[1], 1);
    near(p.velocity[0], 1);

    g.mode = BOX_PARTICLE_GROUND_DIE;
    p = twin_new();
    p.position[1] = 0.05f;
    twin_step(&p, table, &rows, &f, &g);
    test_assert(p.remaining != 0);
    twin_step(&p, table, &rows, &f, &g);
    test_uint(p.remaining, 0);
}

void Motion_random_between_two(void) {
    // particle/motion
    scene_t s;
    box_particle_motion_rows_t rows;
    scene_init(&s, "Motion: {\n"
        "    velocity_x: {keys: [{0, 0}, {1, 2}], max_keys: [{0, 0}, {1, 4}]}\n"
        "    velocity_y: {keys: [{0, 1}], max_keys: [{0, 3}]}\n"
        "    speed: {keys: [{0, 1}], max_keys: [{0, 0}]}\n"
        "  }");
    scene_rows(&s, &rows);
    test_uint(s.pool->block.motion, 1);
    test_assert(rows.velocity_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(rows.velocity_max_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(rows.velocity_row != rows.velocity_max_row);
    test_uint(rows.force_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(rows.force_max_row, BOX_PARTICLE_CURVE_CONSTANT);

    uint32_t i;
    int32_t same = 0, low = 0, high = 0;
    for (i = 0; i < 200; i ++) {
        float uv = box_particle_motion_blend(SEED, i, BOX_PARTICLE_MOTION_VELOCITY);
        float us = box_particle_motion_blend(SEED, i, BOX_PARTICLE_MOTION_SPEED);
        box_particle_motion_t m, late;
        box_particle_motion(s.table, &rows, SEED, i, 0.5f, &m);
        box_particle_motion(s.table, &rows, SEED, i, 0.9f, &late);

        near(m.velocity[1], 1 + 2 * uv);
        near(m.velocity[0], 1 + uv);
        near(late.velocity[0], 1.8f + 1.8f * uv);
        near(late.velocity[1], m.velocity[1]);
        test_flt(m.velocity[2], 0);
        near(m.speed, 1 - us);
        test_flt(m.drag, 1);
        same += uv == us;
        same += uv == box_particle_blend(SEED, i, BOX_PARTICLE_CURVE_SIZE);
        low += uv < 0.25f;
        high += uv > 0.75f;
    }

    test_assert(same < 3);
    test_assert(low > 20 && high > 20);
    scene_fini(&s);
}

void Motion_burst_slows_and_rises(void) {
    // particle/motion
    float table[TABLE];
    box_particle_motion_rows_t rows;
    box_particle_forces_t f = forces_none();
    twin_t p = twin_new();
    float x_stopped = 0, y_before = 1;
    int32_t n;
    f.drag = 20;
    p.velocity[0] = 10;
    rows_none(&rows);
    rows_bake(table, &rows, DRAG, BOX_CURVE_KEYS({0, 0}, {0.2f, 0}, {0.3f, 1}));
    rows_bake(table, &rows, VY, BOX_CURVE_KEYS({0, 0}, {0.5f, 0}, {0.7f, 2}));
    for (n = 1; n <= FRAMES; n ++) {
        twin_step(&p, table, &rows, &f, NULL);
        if (n == 19) {
            near(p.position[0], 1.9f);
            test_flt(p.position[1], 0);
        }

        if (n == 49) {
            y_before = p.position[1];
        }

        if (n == 60) {
            x_stopped = p.position[0];
        }
    }

    test_assert(x_stopped > 2.1f && x_stopped < 3.5f);
    test_assert(fabsf(p.position[0] - x_stopped) < 1e-3f);
    test_flt(y_before, 0);
    test_assert(p.position[1] > 0.3f);
}
