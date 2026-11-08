/**
 * @file Appearance.c
 * @brief Tests for the appearance of particles: the sprite frames, the soft fade, the stretch,
 * the sort key and the block and billboard corners the extract sends for an emitter.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "simulate.h"
#include "mesh_cache.h"

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

/** Create an emitter that spawns a number of particles every frame.
 *
 * @param world The world
 * @param per_frame Particles spawned per frame
 * @param max Particles alive at once
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    float per_frame,
    int32_t max)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, Position, {0, 0, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = per_frame / DELTA + 1;
    p->max = max;
    p->lifetime = (ParticleRange){10, 10};
    ecs_modified(world, e, ParticleEmitter);

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

/** Find the upload into a buffer in the last packet.
 *
 * @param null The backend
 * @param buffer The buffer handle
 * @return The upload, NULL when the packet has none
 */
static const box_render_upload_t* upload_of(
    box_render_null_t *null,
    uint32_t buffer)
{
    const box_render_upload_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.uploads); i ++) {
        const box_render_upload_t *u = ecs_vec_get_t(
            &null->last.uploads, box_render_upload_t, i);
        if (u->buffer == buffer) {
            result = u;
        }
    }

    return result;
}

/** Check whether the last packet destroys a buffer.
 *
 * @param null The backend
 * @param buffer The buffer handle
 * @return Whether it does
 */
static bool destroyed(
    box_render_null_t *null,
    uint32_t buffer)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_BUFFER && r->op == BOX_RENDER_DESTROY && r->handle == buffer) {
            result = true;
        }
    }

    return result;
}

/** Check the curves of a block that change nothing.
 *
 * @param b The block
 */
static void check_identity_curves(
    const box_render_particle_block_t *b)
{
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
}

void Appearance_c_defaults(void) {
    // particle/appearance
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleAppearance);
    const ParticleAppearance *a = ecs_get(world, e, ParticleAppearance);
    test_assert(a != NULL);
    test_int(a->shape, ParticleCircle);
    test_int(a->blend, ParticleAlpha);
    test_uint(a->texture, 0);
    test_uint(a->mesh, 0);
    test_int(a->columns, 0);
    test_int(a->rows, 0);
    test_flt(a->fps, 0);
    test_flt(a->softness, 0);
    test_flt(a->stretch, 0);
    test_flt(a->lit, 0);

    ecs_fini(world);
}

void Appearance_script_values(void) {
    // particle/appearance
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "appearance",
        "using box.particle\n"
        "smoke_tex {}\n"
        "rock {}\n"
        "sprite { ParticleAppearance: {shape: ParticleSprite, blend: ParticleAdditive, "
        "texture: smoke_tex, columns: 4, rows: 2, fps: 12, softness: 0.5, stretch: 0.1, "
        "lit: 0.75} }\n"
        "debris { ParticleAppearance: {shape: ParticleBillboard, mesh: rock} }\n", NULL) == 0);

    const ParticleAppearance *a = ecs_get(
        world, ecs_lookup(world, "sprite"), ParticleAppearance);
    test_assert(a != NULL);
    test_int(a->shape, ParticleSprite);
    test_int(a->blend, ParticleAdditive);
    test_uint(a->texture, ecs_lookup(world, "smoke_tex"));
    test_int(a->columns, 4);
    test_int(a->rows, 2);
    test_flt(a->fps, 12);
    test_flt(a->softness, 0.5f);
    test_flt(a->stretch, 0.1f);
    test_flt(a->lit, 0.75f);

    a = ecs_get(world, ecs_lookup(world, "debris"), ParticleAppearance);
    test_assert(a != NULL);
    test_int(a->shape, ParticleBillboard);
    test_int(a->blend, ParticleAlpha);
    test_uint(a->mesh, ecs_lookup(world, "rock"));

    ecs_fini(world);
}

void Appearance_frame_over_life(void) {
    // particle/sprite
    test_uint(box_particle_frame(4, 2, 0, 0, 2), 0);
    test_uint(box_particle_frame(4, 2, 0, 0.26f, 2), 1);
    test_uint(box_particle_frame(4, 2, 0, 1.01f, 2), 4);
    test_uint(box_particle_frame(4, 2, 0, 1.99f, 2), 7);
    test_uint(box_particle_frame(4, 2, 0, 2, 2), 7);
    test_uint(box_particle_frame(4, 2, 0, 5, 2), 7);
}

void Appearance_frame_at_fps_loops(void) {
    // particle/sprite
    test_uint(box_particle_frame(2, 2, 10, 0.05f, 100), 0);
    test_uint(box_particle_frame(2, 2, 10, 0.15f, 100), 1);
    test_uint(box_particle_frame(2, 2, 10, 0.35f, 100), 3);
    test_uint(box_particle_frame(2, 2, 10, 0.45f, 100), 0);
    test_uint(box_particle_frame(2, 2, 10, 0.55f, 0.5f), 1);
    test_uint(box_particle_frame(3, 1, 24, 1.0f, 2), 0);
}

void Appearance_frame_whole_texture(void) {
    // particle/sprite
    float rect[4];
    test_uint(box_particle_frame(0, 0, 0, 1.5f, 2), 0);
    test_uint(box_particle_frame(1, 1, 0, 1.5f, 2), 0);
    test_uint(box_particle_frame(0, 1, 30, 1.5f, 2), 0);
    box_particle_frame_rect(0, 0, 0, rect);
    test_flt(rect[0], 0);
    test_flt(rect[1], 0);
    test_flt(rect[2], 1);
    test_flt(rect[3], 1);
}

void Appearance_frame_rect(void) {
    // particle/sprite
    float rect[4];
    box_particle_frame_rect(5, 4, 2, rect);
    test_flt(rect[0], 0.25f);
    test_flt(rect[1], 0.5f);
    test_flt(rect[2], 0.25f);
    test_flt(rect[3], 0.5f);
    box_particle_frame_rect(3, 4, 2, rect);
    test_flt(rect[0], 0.75f);
    test_flt(rect[1], 0);
}

void Appearance_soft_fade_term(void) {
    // particle/soft
    test_flt(box_particle_soft(10, 9, 0), 1);
    test_flt(box_particle_soft(10, 10.5f, 0), 1);
    test_flt(box_particle_soft(10, 9, 2), 0.5f);
    test_flt(box_particle_soft(10, 10, 2), 0);
    test_flt(box_particle_soft(10, 11, 2), 0);
    test_flt(box_particle_soft(10, 4, 2), 1);
    test_flt(box_particle_soft(INFINITY, 4, 2), 1);
}

void Appearance_soft_view_depth(void) {
    // particle/soft
    Camera cameras[2] = {
        {.fov = 60, .near_ = 0.1f, .far_ = 100},
        {.ortho = true, .ortho_height = 8, .near_ = 0.1f, .far_ = 100}
    };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        mat4 proj;
        vec4 clip, point = {0.5f, -0.25f, -7.5f, 1};
        box_camera_projection(&cameras[i], 640, 360, proj);
        glm_mat4_mulv(proj, point, clip);
        float rows[4] = { proj[2][2], proj[3][2], proj[2][3], proj[3][3] };
        float depth = box_particle_view_depth(rows, clip[2] / clip[3]);
        test_assert(fabsf(depth - 7.5f) < 1e-3f);
    }

    Camera infinite = {.fov = 60, .near_ = 0.1f};
    mat4 proj;
    box_camera_projection(&infinite, 640, 360, proj);
    float rows[4] = { proj[2][2], proj[3][2], proj[2][3], proj[3][3] };
    test_assert(box_particle_view_depth(rows, 0) > 1e30f);
}

void Appearance_stretch_factor(void) {
    // particle/stretch
    test_flt(box_particle_stretch(0, 5), 1);
    test_flt(box_particle_stretch(0.5f, 4), 3);
    test_flt(box_particle_stretch(-1, 4), 1);
    test_flt(box_particle_stretch(2, 0), 1);
}

void Appearance_sort_key_back_to_front(void) {
    // particle/sort
    float depths[6] = {1000, 10, 5, 0.5f, 0, -3};
    int32_t i;
    for (i = 0; i < 5; i ++) {
        test_assert(box_particle_sort_key(depths[i]) < box_particle_sort_key(depths[i + 1]));
    }

    for (i = 0; i < 6; i ++) {
        test_assert(box_particle_sort_key(depths[i]) < BOX_PARTICLE_SORT_DEAD);
    }
}

void Appearance_block_without_appearance(void) {
    // particle/appearance
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 2, 100);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    check_identity_curves(b);
    test_uint(b->look, ParticleCircle);
    test_uint(b->blend, ParticleAlpha);
    test_uint(b->columns, 1);
    test_uint(b->rows, 1);
    test_flt(b->fps, 0);
    test_flt(b->softness, 0);
    test_flt(b->stretch, 0);
    test_flt(b->lit, 0);
    test_uint(b->corners, 6);
    test_uint(b->sorted, 0);
    test_uint(b->sort_count, 0);
    test_uint(b->styled, 0);
    test_uint(pool->texture, 0);
    test_uint(pool->mesh, 0);

    ecs_add(world, e, ParticleCurves);
    ecs_add(world, e, ParticleAppearance);
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    check_identity_curves(&pool->block);
    test_uint(pool->block.look, ParticleCircle);
    test_uint(pool->block.corners, 6);
    test_uint(pool->block.styled, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_block_curves(void) {
    // particle/curves
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 2, 100);
    ecs_set(world, e, ParticleCurves, {
        .size = { .keys = BOX_CURVE_KEYS({0, 1}, {1.0f / 3.0f, 2}, {2.0f / 3.0f, 3}, {1, 4}) },
        .opacity = { .keys = BOX_CURVE_KEYS(
            {0, 0}, {1.0f / 3.0f, 1}, {2.0f / 3.0f, 1}, {1, 0}) },
        .rotation = { .keys = BOX_CURVE_KEYS(
            {0, 0}, {1.0f / 3.0f, 0.25f}, {2.0f / 3.0f, 0.5f}, {1, 1}) },
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey,
            {0, {255, 0, 0}}, {1.0f / 3.0f, {0, 255, 0}}, {2.0f / 3.0f, {0, 0, 255}},
            {1, {128, 128, 128}}) },
        .spin = { .keys = BOX_CURVE_KEYS({0, 1}, {1.0f / 3.0f, 0}, {2.0f / 3.0f, 0}, {1, 1}) }
    });
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    const float *table = box_render_particles_curve_table(box_render_particles_get(world), NULL);
    float c[3];
    test_assert(table != NULL);
    test_uint(pool->curves, BOX_RENDER_BUFFER_PARTICLE_CURVES);
    test_flt(box_particle_curve(table, b->curve_rows[0], b->curve_values[0], 0), 1);
    test_flt(box_particle_curve(table, b->curve_rows[0], b->curve_values[0], 1), 4);
    test_flt(box_particle_curve(table, b->curve_rows[1], b->curve_values[1], 1.0f / 3.0f), 1);
    test_flt(box_particle_curve(table, b->curve_rows[1], b->curve_values[1], 1), 0);
    test_flt(box_particle_curve(table, b->curve_rows[2], b->curve_values[2], 1.0f / 3.0f),
        0.25f);
    test_flt(box_particle_curve(table, b->curve_rows[3], b->curve_values[3], 1.0f / 3.0f), 0);
    box_particle_gradient(table, b->gradient_row, b->gradient, 0, c);
    test_flt(c[0], 1);
    test_flt(c[1], 0);
    box_particle_gradient(table, b->gradient_row, b->gradient, 1.0f / 3.0f, c);
    test_flt(c[1], 1);
    box_particle_gradient(table, b->gradient_row, b->gradient, 2.0f / 3.0f, c);
    test_flt(c[2], 1);
    box_particle_gradient(table, b->gradient_row, b->gradient, 1, c);
    test_assert(fabsf(c[0] - box_particle_linear(128)) < 1e-6f);
    test_uint(b->look, ParticleCircle);
    test_uint(b->sorted, 1);
    test_assert(b->sort_count >= b->used);
    test_assert(b->sort_count < 2 * b->used || b->sort_count == 1);
    test_uint(b->sort_count & (b->sort_count - 1), 0);
    test_uint(b->styled, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_block_sprite(void) {
    // particle/sprite
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 2, 100);
    ecs_set(world, e, ParticleAppearance, {
        .shape = ParticleSprite, .columns = 4, .rows = 0, .fps = 12, .softness = 0.5f,
        .stretch = 0.1f, .lit = 2
    });
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    const box_render_particle_block_t *b = &pool->block;
    check_identity_curves(b);
    test_uint(b->look, ParticleSprite);
    test_uint(b->blend, ParticleAlpha);
    test_uint(b->columns, 4);
    test_uint(b->rows, 1);
    test_flt(b->fps, 12);
    test_flt(b->softness, 0.5f);
    test_flt(b->stretch, 0.1f);
    test_flt(b->lit, 1);
    test_uint(b->corners, 6);
    test_uint(b->sorted, 1);
    test_assert(b->sort_count >= b->used);
    test_assert(b->sort_count < 2 * b->used || b->sort_count == 1);
    test_uint(b->sort_count & (b->sort_count - 1), 0);
    test_uint(b->styled, 1);
    test_uint(pool->texture, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_block_additive_unsorted(void) {
    // particle/sort
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 2, 100);
    ecs_set(world, e, ParticleAppearance, {.blend = ParticleAdditive});
    ecs_add(world, e, ParticleCurves);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.blend, ParticleAdditive);
    test_uint(pool->block.sorted, 0);
    test_uint(pool->block.sort_count, 0);

    ecs_set(world, e, ParticleAppearance, {.blend = ParticleAlpha});
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.blend, ParticleAlpha);
    test_uint(pool->block.sorted, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_sorted_only_with_color_curve(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 2, 100);
    ecs_set(world, e, ParticleCurves, {
        .size = { .keys = BOX_CURVE_KEYS({0, 0.5f}, {1, 2}) },
        .opacity = { .keys = BOX_CURVE_KEYS({0, 0}, {0.2f, 1}, {1, 0}) },
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey, {0, {200, 100, 50}}) }
    });
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->block.curve_rows[0] != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pool->block.gradient_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pool->block.sorted, 0);
    test_uint(pool->block.sort_count, 0);

    ecs_set(world, e, ParticleCurves, {
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey,
            {0, {200, 100, 50}}, {1, {50, 50, 50}}) }
    });
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->block.gradient_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pool->block.sorted, 1);
    test_assert(pool->block.sort_count >= pool->block.used);
    test_assert(pool->block.sort_count < 2 * pool->block.used || pool->block.sort_count == 1);
    test_uint(pool->block.sort_count & (pool->block.sort_count - 1), 0);

    ecs_set(world, e, ParticleCurves, {
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey, {0, {200, 100, 50}}),
            .max_keys = BOX_VEC_VIEW(ParticleGradientKey, {0, {50, 100, 200}}) }
    });
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->block.gradient_max_row != BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pool->block.sorted, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_sorted_with_color_variance(void) {
    // particle/curve_cost
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 2, 100);
    ecs_entity_t plain = emitter_new(world, 2, 100);
    ecs_entity_t bare = emitter_new(world, 2, 100);
    ecs_entity_t added = emitter_new(world, 2, 100);
    ecs_get_mut(world, e, ParticleEmitter)->color_variance = (rgb){40, 0, 0};
    ecs_modified(world, e, ParticleEmitter);
    ecs_get_mut(world, bare, ParticleEmitter)->color_variance = (rgb){0, 0, 30};
    ecs_modified(world, bare, ParticleEmitter);
    ecs_get_mut(world, added, ParticleEmitter)->color_variance = (rgb){0, 20, 0};
    ecs_modified(world, added, ParticleEmitter);
    ecs_set(world, e, ParticleCurves, {
        .opacity = { .keys = BOX_CURVE_KEYS({0, 0}, {0.2f, 1}, {1, 0}) }
    });
    ecs_set(world, plain, ParticleCurves, {
        .opacity = { .keys = BOX_CURVE_KEYS({0, 0}, {0.2f, 1}, {1, 0}) }
    });
    ecs_set(world, added, ParticleAppearance, {.blend = ParticleAdditive});
    ecs_set(world, added, ParticleCurves, {
        .opacity = { .keys = BOX_CURVE_KEYS({0, 0}, {0.2f, 1}, {1, 0}) }
    });
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.blend, ParticleAlpha);
    test_uint(pool->block.gradient_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_uint(pool->block.gradient_max_row, BOX_PARTICLE_CURVE_CONSTANT);
    test_assert(pool->block.variance[0] != 0);
    test_uint(pool->block.sorted, 1);
    test_assert(pool->block.sort_count >= pool->block.used);
    test_assert(pool->block.sort_count < 2 * pool->block.used || pool->block.sort_count == 1);
    test_uint(pool->block.sort_count & (pool->block.sort_count - 1), 0);

    pool = pool_of(null, plain);
    test_assert(pool != NULL);
    test_uint(pool->block.blend, ParticleAlpha);
    test_flt(pool->block.variance[0], 0);
    test_flt(pool->block.variance[1], 0);
    test_flt(pool->block.variance[2], 0);
    test_uint(pool->block.sorted, 0);
    test_uint(pool->block.sort_count, 0);

    pool = pool_of(null, bare);
    test_assert(pool != NULL);
    test_uint(pool->block.blend, ParticleAlpha);
    test_assert(pool->block.variance[2] != 0);
    test_uint(pool->block.sorted, 1);

    pool = pool_of(null, added);
    test_assert(pool != NULL);
    test_uint(pool->block.blend, ParticleAdditive);
    test_uint(pool->block.sorted, 0);
    test_uint(pool->block.sort_count, 0);

    ecs_get_mut(world, e, ParticleEmitter)->color_variance = (rgb){0, 0, 0};
    ecs_modified(world, e, ParticleEmitter);
    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.sorted, 0);
    test_uint(pool->block.sort_count, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_sort_count_power_of_two(void) {
    // particle/sort
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 700, 2000);
    ecs_entity_t full = emitter_new(world, 1000, 1000);
    ecs_add(world, e, ParticleAppearance);
    ecs_add(world, full, ParticleAppearance);
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->capacity, 2048);
    test_uint(pool->block.used, 700);
    test_uint(pool->block.sort_count, 1024);
    pool = pool_of(null, full);
    test_assert(pool != NULL);
    test_uint(pool->capacity, 1024);
    test_uint(pool->block.used, 1000);
    test_uint(pool->block.sort_count, 1024);

    frame(world);
    pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->block.used, 1400);
    test_uint(pool->block.sort_count, 2048);

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_billboard_corners_sent(void) {
    // particle/billboard
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t rock = ecs_new(world);
    ecs_set(world, rock, Box, {2, 2, 2});
    ecs_set(world, rock, Position, {0, 0, 0});
    ecs_entity_t e = emitter_new(world, 2, 100);
    ecs_set(world, e, ParticleAppearance, {.shape = ParticleBillboard, .mesh = rock});
    frame(world);

    const ShapeMesh *shape = ecs_get(world, rock, ShapeMesh);
    test_assert(shape != NULL);
    const box_mesh_t *m = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(m != NULL);
    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_assert(pool->mesh >= BOX_RENDER_BUFFER_PARTICLE_MESH_FIRST);
    test_uint(pool->block.look, ParticleBillboard);
    test_uint(pool->block.corners, (uint32_t)m->index_count);
    const box_render_upload_t *u = upload_of(null, pool->mesh);
    test_assert(u != NULL);
    test_uint(u->size, (uint32_t)m->index_count * sizeof(box_render_particle_corner_t));
    const box_render_particle_corner_t *c = box_render_packet_data(&null->last, u->data);
    const box_vertex_t *v = &m->vertices[m->indices[0]];
    test_flt(c[0].position[0], v->position[0] * shape->scale[0]);
    test_flt(c[0].position[1], v->position[1] * shape->scale[1]);
    test_flt(c[0].position[3], v->uv[0]);
    test_flt(c[0].normal[3], v->uv[1]);
    uint32_t handle = pool->mesh;

    frame(world);
    pool = pool_of(null, e);
    test_uint(pool->mesh, handle);
    test_assert(upload_of(null, handle) == NULL);

    ecs_set(world, rock, Box, {4, 1, 1});
    frame(world);
    pool = pool_of(null, e);
    test_assert(upload_of(null, pool->mesh) != NULL);

    ecs_set(world, e, ParticleAppearance, {.shape = ParticleCircle});
    frame(world);
    pool = pool_of(null, e);
    test_uint(pool->mesh, 0);
    test_uint(pool->block.corners, 6);
    test_assert(destroyed(null, handle));

    ecs_fini(world);
    box_render_null_free(null);
}

void Appearance_billboard_without_mesh_is_circle(void) {
    // particle/billboard
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, 2, 100);
    ecs_set(world, e, ParticleAppearance, {.shape = ParticleBillboard});
    frame(world);

    const box_render_particle_pool_t *pool = pool_of(null, e);
    test_assert(pool != NULL);
    test_uint(pool->mesh, 0);
    test_uint(pool->block.look, ParticleCircle);
    test_uint(pool->block.corners, 6);

    ecs_fini(world);
    box_render_null_free(null);
}
