/**
 * @file Card.c
 * @brief Tests for ParticleCard: the enum value, the block and the pool of a card emitter, and
 * the orientation, axis, turn, rest and flat easing of a card from its streams.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "simulate.h"

/** Frame delta of the tests, a fixed clock */
#define DELTA (1.0f / 60.0f)

/** Bytes of a page of pools of the smallest size */
#define SMALL_PAGE (16 * 1024 * BOX_RENDER_PARTICLE_SIZE)

/** Slots of a card pool of the smallest size: 1024 particles and 128 slots of card words */
#define CARD_SLOTS (1024 + 128)

/** Bytes of a page of card pools of the smallest size, 14 pools of CARD_SLOTS */
#define CARD_PAGE (14 * CARD_SLOTS * BOX_RENDER_PARTICLE_SIZE)

/** Pi as a float */
#define PI_F (3.14159265358979f)

/** Draws of the tests over many particles */
#define DRAWS (1000)

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

/** Run a frame and get the stats it wrote.
 *
 * @param world The world
 * @return The stats
 */
static ParticleStats frame(
    ecs_world_t *world)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_assert(stats != NULL);

    return *stats;
}

/** Create an emitter with a rate, a long lifetime and an appearance shape.
 *
 * @param world The world
 * @param max Particles alive at once
 * @param shape The ParticleShape
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    int32_t max,
    ParticleShape shape)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 120;
    p->max = max;
    p->lifetime = (ParticleRange){100, 100};
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, ParticleAppearance, { .shape = shape });

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

/** Check that two vectors are equal within a tolerance.
 *
 * @param a The first vector
 * @param b The second vector
 * @param eps The tolerance per component
 */
static void test_vec3(
    const float *a,
    const float *b,
    float eps)
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        test_assert(fabsf(a[c] - b[c]) < eps);
    }
}

/** Get the normal of a card, the z axis of its orientation.
 *
 * @param q The orientation
 * @param out Receives the normal
 */
static void card_normal(
    const float *q,
    float *out)
{
    static const float z[3] = {0, 0, 1};
    box_particle_quat_rotate(q, z, out);
}

void Card_enum_reflects(void) {
    // particle/card
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ParticleCard, 2);
    ParticleShape shape = ParticleCard;
    char *str = ecs_ptr_to_expr(world, ecs_id(ParticleShape), &shape);
    test_assert(str != NULL);
    test_assert(strstr(str, "Card") != NULL);
    ecs_os_free(str);

    ParticleAppearance a = { .shape = ParticleSprite };
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, ecs_id(ParticleAppearance), &a);
    test_int(ecs_meta_push(&cur), 0);
    test_int(ecs_meta_member(&cur, "shape"), 0);
    test_int(ecs_meta_set_string(&cur, "Card"), 0);
    test_int(ecs_meta_pop(&cur), 0);
    test_int(a.shape, ParticleCard);

    ecs_fini(world);
}

void Card_script_value(void) {
    // particle/card
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "card",
        "using box.particle\n"
        "leaf_tex {}\n"
        "leaves { Appearance: {shape: Card, blend: Additive, texture: leaf_tex, lit: 1} }\n",
        NULL) == 0);

    const ParticleAppearance *a = ecs_get(
        world, ecs_lookup(world, "leaves"), ParticleAppearance);
    test_assert(a != NULL);
    test_int(a->shape, ParticleCard);
    test_int(a->blend, ParticleAdditive);
    test_uint(a->texture, ecs_lookup(world, "leaf_tex"));
    test_flt(a->lit, 1);

    ecs_fini(world);
}

void Card_block(void) {
    // particle/card
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 1000, ParticleCard);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.look, ParticleCard);
    test_uint(pool->block.cards, 1024);
    test_uint(pool->capacity, CARD_SLOTS);
    test_uint(pool->block.styled, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Card_other_shapes_unchanged(void) {
    // particle/card
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t circle = emitter_new(world, 1000, ParticleCircle);
    ecs_entity_t sprite = emitter_new(world, 1000, ParticleSprite);
    ecs_entity_t plain = emitter_new(world, 1000, ParticleCircle);
    ecs_remove(world, plain, ParticleAppearance);
    ParticleStats stats = frame(world);

    ecs_entity_t all[3] = {circle, sprite, plain};
    int32_t i;
    for (i = 0; i < 3; i ++) {
        const box_render_particle_pool_t *pool = pool_of(null, all[i]);
        test_assert(pool != NULL);
        test_uint(pool->block.cards, 0);
        test_uint(pool->capacity, 1024);
    }

    test_int(stats.bytes, SMALL_PAGE);

    ecs_fini(world);
    box_render_null_free(null);
}

void Card_pool_bytes(void) {
    // particle/card
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    emitter_new(world, 1000, ParticleCard);
    ParticleStats stats = frame(world);
    test_int(stats.bytes, CARD_PAGE);

    emitter_new(world, 1000, ParticleSprite);
    stats = frame(world);
    test_int(stats.bytes, CARD_PAGE + SMALL_PAGE);

    ecs_fini(world);
    box_render_null_free(null);
}

void Card_max_clamped(void) {
    // particle/card
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, BOX_PARTICLE_MAX, ParticleCard);
    ParticleStats stats = frame(world);

    uint32_t half = BOX_PARTICLE_MAX / 2;
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.cards, half);
    test_uint(pool->capacity, half + half / BOX_PARTICLE_CARD_WORDS);
    test_int(stats.bytes, (int64_t)(half + half / BOX_PARTICLE_CARD_WORDS) *
        BOX_RENDER_PARTICLE_SIZE);

    ecs_fini(world);
    box_render_null_free(null);
}

void Card_shape_change_empties_pool(void) {
    // particle/card
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 1000, ParticleSprite);
    frame(world);
    frame(world);
    box_render_particles_t *s = box_render_particles_get(world);
    test_int(box_render_particles_live(s, e), 4);

    ecs_set(world, e, ParticleAppearance, { .shape = ParticleCard });
    frame(world);
    test_int(box_render_particles_live(s, e), 2);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->clear);
    test_uint(pool->capacity, CARD_SLOTS);

    frame(world);
    test_int(box_render_particles_live(s, e), 4);

    ecs_set(world, e, ParticleAppearance, { .shape = ParticleSprite });
    frame(world);
    test_int(box_render_particles_live(s, e), 2);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->capacity, 1024);
    test_uint(pool->block.cards, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Card_orientation_unit(void) {
    // particle/card
    uint32_t i;
    for (i = 0; i < DRAWS; i ++) {
        float q[4], axis[3];
        box_particle_card_orientation(5, i, q);
        box_particle_card_axis(5, i, axis);
        float lq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        float la = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
        test_assert(fabsf(lq - 1) < 1e-4f);
        test_assert(fabsf(la - 1) < 1e-4f);
    }
}

void Card_orientation_spread(void) {
    // particle/card
    double normal[3] = {0, 0, 0}, axis[3] = {0, 0, 0};
    uint32_t i;
    int32_t c;
    for (i = 0; i < DRAWS; i ++) {
        float q[4], n[3], a[3];
        box_particle_card_orientation(9, i, q);
        card_normal(q, n);
        box_particle_card_axis(9, i, a);
        for (c = 0; c < 3; c ++) {
            normal[c] += n[c];
            axis[c] += a[c];
        }
    }

    for (c = 0; c < 3; c ++) {
        test_assert(fabs(normal[c] / DRAWS) < 0.1);
        test_assert(fabs(axis[c] / DRAWS) < 0.1);
    }
}

void Card_same_seed_same_draws(void) {
    // particle/card
    uint32_t i;
    for (i = 0; i < 64; i ++) {
        float q0[4], q1[4], a0[3], a1[3];
        box_particle_card_orientation(42, i, q0);
        box_particle_card_orientation(42, i, q1);
        box_particle_card_axis(42, i, a0);
        box_particle_card_axis(42, i, a1);
        test_assert(memcmp(q0, q1, sizeof(q0)) == 0);
        test_assert(memcmp(a0, a1, sizeof(a0)) == 0);

        float c0[4], c1[4];
        box_particle_card(42, i, 1.5f, 0, -1, c0);
        box_particle_card(42, i, 1.5f, 0, -1, c1);
        test_assert(memcmp(c0, c1, sizeof(c0)) == 0);
    }
}

void Card_other_seed_other_draws(void) {
    // particle/card
    int32_t same_q = 0, same_a = 0;
    uint32_t i;
    for (i = 0; i < 64; i ++) {
        float q0[4], q1[4], a0[3], a1[3];
        box_particle_card_orientation(1, i, q0);
        box_particle_card_orientation(2, i, q1);
        box_particle_card_axis(1, i, a0);
        box_particle_card_axis(2, i, a1);
        same_q += memcmp(q0, q1, sizeof(q0)) == 0;
        same_a += memcmp(a0, a1, sizeof(a0)) == 0;
    }

    test_int(same_q, 0);
    test_int(same_a, 0);
}

void Card_streams_own(void) {
    // particle/card
    test_assert(BOX_PARTICLE_STREAM_CARD >= BOX_PARTICLE_STREAM_MOTION + 4);

    // The other draws of a particle do not depend on the card streams
    ParticleEmitter emitter = {
        .max = 10, .speed = {1, 2}, .lifetime = {1, 2}, .size = {0.1f, 0.4f},
        .rotation = {0, 360}, .spin = {10, 90}, .opacity = {0.5f, 1},
        .color = {200, 100, 50}, .color_variance = {20, 20, 20}, .seed = 3
    };
    box_particle_sample_t a, b;
    box_particle_sample(&emitter, 7, &a);
    box_particle_sample(&emitter, 7, &b);
    test_assert(memcmp(&a, &b, sizeof(a)) == 0);

    // The orientation and the axis draw from different streams
    float q[4], axis[3];
    box_particle_card_orientation(3, 7, q);
    box_particle_card_axis(3, 7, axis);
    test_assert(box_particle_hash(3, 7, BOX_PARTICLE_STREAM_CARD) !=
        box_particle_hash(3, 7, BOX_PARTICLE_STREAM_CARD + 3));
}

void Card_turn_keeps_axis(void) {
    // particle/card
    uint32_t i;
    for (i = 0; i < 32; i ++) {
        float q[4], axis[3], turned[4], local[3], back[3];
        box_particle_card_orientation(11, i, q);
        box_particle_card_axis(11, i, axis);
        float conj[4] = {-q[0], -q[1], -q[2], q[3]};
        box_particle_quat_rotate(conj, axis, local);
        box_particle_card_turn(q, axis, 1.3f + (float)i, turned);
        box_particle_quat_rotate(turned, local, back);
        test_vec3(back, axis, 1e-4f);
    }
}

void Card_turn_shows_edge(void) {
    // particle/card
    // A card turning around an axis in its plane shows its edge after a quarter turn
    float q[4] = {0, 0, 0, 1}, axis[3] = {1, 0, 0}, turned[4], n[3];
    float eye[3] = {0, 0, 1};
    box_particle_card_turn(q, axis, PI_F * 0.5f, turned);
    card_normal(turned, n);
    test_assert(fabsf(n[0] * eye[0] + n[1] * eye[1] + n[2] * eye[2]) < 1e-5f);

    box_particle_card_turn(q, axis, PI_F, turned);
    card_normal(turned, n);
    test_assert(fabsf(n[2] + 1) < 1e-5f);
}

void Card_turn_full_circle(void) {
    // particle/card
    float q[4], axis[3], turned[4], x[3] = {1, 0, 0}, a[3], b[3];
    box_particle_card_orientation(4, 2, q);
    box_particle_card_axis(4, 2, axis);
    box_particle_card_turn(q, axis, 2.0f * PI_F, turned);
    box_particle_quat_rotate(q, x, a);
    box_particle_quat_rotate(turned, x, b);
    test_vec3(a, b, 1e-4f);
}

void Card_ease(void) {
    // particle/card
    test_flt(box_particle_card_ease(0), 0);
    test_flt(box_particle_card_ease(-1), 0);
    test_assert(box_particle_card_ease(0.1f) < box_particle_card_ease(0.2f));
    test_assert(box_particle_card_ease(2) > 0.99f);
    test_assert(box_particle_card_ease(100) <= 1);
}

void Card_flat_lies_flat(void) {
    // particle/card
    uint32_t i;
    for (i = 0; i < DRAWS; i ++) {
        float q[4], before[3], flat[4], n[3];
        box_particle_card_orientation(13, i, q);
        card_normal(q, before);
        box_particle_card_flat(q, 1, flat);
        card_normal(flat, n);
        test_assert(fabsf(fabsf(n[1]) - 1) < 1e-4f);
        // It lies on the side it faced
        test_assert(n[1] * before[1] >= 0 || fabsf(before[1]) < 1e-4f);
    }
}

void Card_flat_keeps_heading(void) {
    // particle/card
    // The horizontal direction the normal leans to stays the axis that tips the card
    float q[4], n0[3], half[4], n1[3];
    box_particle_card_orientation(21, 5, q);
    card_normal(q, n0);
    box_particle_card_flat(q, 0.5f, half);
    card_normal(half, n1);
    float h0 = sqrtf(n0[0] * n0[0] + n0[2] * n0[2]);
    float h1 = sqrtf(n1[0] * n1[0] + n1[2] * n1[2]);
    test_assert(h1 < h0);
    if (h1 > 1e-3f) {
        test_assert(fabsf(n0[0] / h0 - n1[0] / h1) < 1e-3f);
        test_assert(fabsf(n0[2] / h0 - n1[2] / h1) < 1e-3f);
    }

    // Halfway in angle
    float a0 = atan2f(h0, fabsf(n0[1])), a1 = atan2f(h1, fabsf(n1[1]));
    test_assert(fabsf(a1 - 0.5f * a0) < 1e-3f);
}

void Card_flat_zero_unchanged(void) {
    // particle/card
    float q[4], out[4], x[3] = {1, 0, 0}, a[3], b[3];
    box_particle_card_orientation(8, 3, q);
    box_particle_card_flat(q, 0, out);
    box_particle_quat_rotate(q, x, a);
    box_particle_quat_rotate(out, x, b);
    test_vec3(a, b, 1e-5f);
}

void Card_rest_stops_turning(void) {
    // particle/card
    float a[4], b[4], turned[4], q[4], axis[3];
    box_particle_card(6, 9, 1.0f, 0.4f, 0, a);
    box_particle_card(6, 9, 25.0f, 0.4f, 0, b);
    test_assert(memcmp(a, b, sizeof(a)) == 0);

    box_particle_card_orientation(6, 9, q);
    box_particle_card_axis(6, 9, axis);
    box_particle_card_turn(q, axis, 0.4f, turned);
    int32_t c;
    for (c = 0; c < 4; c ++) {
        test_assert(fabsf(a[c] - turned[c]) < 1e-6f);
    }

    // Seconds after it came to rest it lies flat
    float n[3];
    box_particle_card(6, 9, 1.0f, 0.4f, 3, a);
    card_normal(a, n);
    test_assert(fabsf(fabsf(n[1]) - 1) < 1e-3f);
}

void Card_in_air_turns(void) {
    // particle/card
    float a[4], q[4], axis[3], turned[4];
    box_particle_card(6, 9, 1.0f, 0.4f, -1, a);
    box_particle_card_orientation(6, 9, q);
    box_particle_card_axis(6, 9, axis);
    box_particle_card_turn(q, axis, 1.0f, turned);
    test_assert(memcmp(a, turned, sizeof(a)) == 0);
}

void Card_rests(void) {
    // particle/card
    uint32_t bounce = BOX_PARTICLE_GROUND_BOUNCE;
    test_assert(box_particle_card_rests(bounce, 0, 0.1f, 0, 1));
    test_assert(box_particle_card_rests(bounce, 1.005f, 0.4f, 1, 1));
    test_assert(!box_particle_card_rests(bounce, 0.5f, 0.1f, 0, 1));
    test_assert(!box_particle_card_rests(bounce, 0, 0.6f, 0, 1));
    test_assert(!box_particle_card_rests(BOX_PARTICLE_GROUND_NONE, 0, 0, 0, 1));
    test_assert(!box_particle_card_rests(BOX_PARTICLE_GROUND_DIE, 0, 0, 0, 1));

    // The speed and the height scale with the emitter
    test_assert(box_particle_card_rests(bounce, 0.05f, 4, 0, 10));
    test_assert(!box_particle_card_rests(bounce, 0, 0.1f, 0, 0.1f));
}

void Card_word(void) {
    // particle/card
    uint32_t pool[4 * 8];
    uint32_t i;
    for (i = 0; i < 4 * 8; i ++) {
        pool[i] = 1000 + i;
    }

    // Two particle slots, the card words of their particles in the third
    test_uint(box_particle_card_word(pool, 2, 0), 1016);
    test_uint(box_particle_card_word(pool, 2, 7), 1023);
    test_uint(box_particle_card_word(pool, 2, 8), 1024);
    test_uint(box_particle_card_word(pool, 2, 13), 1029);
    test_uint(box_particle_card_slots(1024), 128);
    test_uint(box_particle_card_slots(1), 1);
    test_uint(box_particle_card_slots(9), 2);
    test_int(sizeof(box_particle_t), BOX_PARTICLE_CARD_WORDS * sizeof(uint32_t));
}
