/**
 * @file Simulate.c
 * @brief Tests for the C twin of the particle simulation: the hash, the range draws, the spawn
 * shapes, the cone, the step and the mesh table.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "simulate.h"
#include "mesh_cache.h"

/** Draws each shape test checks */
#define DRAWS (2000)

/** Draw the three numbers of the shape streams of a particle.
 *
 * @param seed The seed
 * @param index The spawn index
 * @param u Receives the numbers
 */
static void shape_draws(
    uint32_t seed,
    uint32_t index,
    float *u)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        u[i] = box_particle_unit(
            box_particle_hash(seed, index, BOX_PARTICLE_STREAM_SHAPE + (uint32_t)i));
    }
}

void Simulate_hash_reference_values(void) {
    // particle/simulate
    test_uint(box_particle_pcg(0), 129708002u);
    test_uint(box_particle_pcg(1), 2831084092u);
    test_uint(box_particle_pcg(12345), 4099845390u);
    test_uint(box_particle_hash(0, 0, 0), 2145236065u);
    test_uint(box_particle_hash(7, 3, 2), 3795992638u);
    test_uint(box_particle_hash(1, 100, 5), 3271986803u);
}

void Simulate_unit_in_range(void) {
    // particle/simulate
    float sum = 0;
    int32_t i;
    test_flt(box_particle_unit(0), 0);
    test_assert(box_particle_unit(UINT32_MAX) < 1.0f);
    for (i = 0; i < DRAWS; i ++) {
        float u = box_particle_unit(box_particle_hash(1, (uint32_t)i, 0));
        test_assert(u >= 0.0f && u < 1.0f);
        sum += u;
    }

    test_assert(fabsf(sum / DRAWS - 0.5f) < 0.03f);
}

void Simulate_range_draw(void) {
    // particle/simulate
    float lo = 1e9f, hi = -1e9f;
    int32_t i;
    test_flt(box_particle_range(2, 4, 0), 2);
    test_flt(box_particle_range(2, 4, 0.5f), 3);
    for (i = 0; i < DRAWS; i ++) {
        float v = box_particle_range(2, 4,
            box_particle_unit(box_particle_hash(3, (uint32_t)i, BOX_PARTICLE_STREAM_SIZE)));
        test_assert(v >= 2 && v < 4);
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }

    test_assert(lo < 2.1f);
    test_assert(hi > 3.9f);
}

void Simulate_range_max_not_above_min(void) {
    // particle/simulate
    test_flt(box_particle_range(3, 3, 0.7f), 3);
    test_flt(box_particle_range(3, 1, 0.7f), 3);
    test_flt(box_particle_range(-1, -2, 0.2f), -1);
}

void Simulate_life_in_range(void) {
    // particle/simulate
    uint32_t lo = UINT32_MAX, hi = 0;
    int32_t i;
    test_uint(box_particle_us(1.0), 1000000);
    test_uint(box_particle_us(-1.0), 0);
    test_uint(box_particle_us(1e9), BOX_PARTICLE_LIFE_MAX_US);
    test_uint(box_particle_life(5, 9, 1000, 0), 1000);
    for (i = 0; i < DRAWS; i ++) {
        uint32_t life = box_particle_life(5, (uint32_t)i, 1000000, 1000000);
        test_assert(life >= 1000000 && life <= 2000000);
        lo = life < lo ? life : lo;
        hi = life > hi ? life : hi;
    }

    test_assert(lo < 1010000);
    test_assert(hi > 1990000);
}

void Simulate_age_frees_at_lifetime(void) {
    // particle/simulate
    uint32_t remaining = 50000;
    remaining = box_particle_age(remaining, 16667);
    test_uint(remaining, 33333);
    remaining = box_particle_age(remaining, 16667);
    test_uint(remaining, 16666);
    remaining = box_particle_age(remaining, 16667);
    test_uint(remaining, 0);
    test_uint(box_particle_age(100, 100), 0);
    test_uint(box_particle_age(0, 0), 0);
}

void Simulate_point_shape(void) {
    // particle/simulate
    float extents[3] = {1, 2, 3}, u[3] = {0.3f, 0.6f, 0.9f}, p[3];
    box_particle_shape(EmitterPoint, 5, extents, u, p);
    test_flt(p[0], 0);
    test_flt(p[1], 0);
    test_flt(p[2], 0);
    box_particle_shape(EmitterMesh, 5, extents, u, p);
    test_flt(p[0], 0);
    test_flt(p[1], 0);
    test_flt(p[2], 0);
}

void Simulate_sphere_points_inside(void) {
    // particle/simulate
    float extents[3] = {0, 0, 0}, u[3], p[3], far = 0;
    float octant[8] = {0};
    int32_t i;
    for (i = 0; i < DRAWS; i ++) {
        shape_draws(1, (uint32_t)i, u);
        box_particle_shape(EmitterSphere, 2, extents, u, p);
        float r = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        test_assert(r <= 2.0001f);
        far = r > far ? r : far;
        octant[(p[0] > 0) + 2 * (p[1] > 0) + 4 * (p[2] > 0)] += 1;
    }

    test_assert(far > 1.9f);
    for (i = 0; i < 8; i ++) {
        test_assert(octant[i] > DRAWS / 16);
    }
}

void Simulate_box_points_inside(void) {
    // particle/simulate
    float extents[3] = {1, 2, 3}, u[3], p[3], reach[3] = {0};
    int32_t i, c;
    for (i = 0; i < DRAWS; i ++) {
        shape_draws(2, (uint32_t)i, u);
        box_particle_shape(EmitterBox, 9, extents, u, p);
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(p[c]) <= extents[c]);
            reach[c] = fabsf(p[c]) > reach[c] ? fabsf(p[c]) : reach[c];
        }
    }

    for (c = 0; c < 3; c ++) {
        test_assert(reach[c] > 0.95f * extents[c]);
    }
}

void Simulate_disc_points_inside(void) {
    // particle/simulate
    float extents[3] = {5, 5, 5}, u[3], p[3], far = 0;
    int32_t i;
    for (i = 0; i < DRAWS; i ++) {
        shape_draws(3, (uint32_t)i, u);
        box_particle_shape(EmitterDisc, 1.5f, extents, u, p);
        float r = sqrtf(p[0] * p[0] + p[2] * p[2]);
        test_flt(p[1], 0);
        test_assert(r <= 1.5001f);
        far = r > far ? r : far;
    }

    test_assert(far > 1.45f);
}

void Simulate_mesh_points_on_surface(void) {
    // particle/simulate
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {2, 4, 6});
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh != NULL);

    box_particle_point_t *points = ecs_os_malloc_n(box_particle_point_t, DRAWS);
    test_int(box_particle_mesh_table(mesh, shape->scale, 4, points, DRAWS), DRAWS);
    float half[3] = {1, 2, 3}, faces[3] = {0};
    int32_t i, c;
    for (i = 0; i < DRAWS; i ++) {
        float *p = points[i].position, *n = points[i].normal;
        int32_t on = -1;
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(p[c]) <= half[c] + 1e-4f);
            if (fabsf(fabsf(n[c]) - 1) < 1e-4f) {
                on = c;
            }
        }

        test_assert(on >= 0);
        test_assert(fabsf(fabsf(p[on]) - half[on]) < 1e-4f);
        test_assert(n[on] * p[on] > 0);
        faces[on] += 1;
    }

    test_assert(faces[0] > faces[1] && faces[1] > faces[2]);
    test_assert(fabsf(faces[0] / DRAWS - 48.0f / 88.0f) < 0.05f);

    box_particle_point_t again[4];
    test_int(box_particle_mesh_table(mesh, shape->scale, 4, again, 4), 4);
    for (i = 0; i < 4; i ++) {
        for (c = 0; c < 3; c ++) {
            test_flt(again[i].position[c], points[i].position[c]);
        }
    }

    ecs_os_free(points);
    ecs_fini(world);
}

void Simulate_mesh_without_area(void) {
    // particle/simulate
    ecs_world_t *world = ecs_init();
    box_vertex_t vertices[3] = {{{0, 0, 0}}, {{1, 0, 0}}, {{2, 0, 0}}};
    uint32_t indices[3] = {0, 1, 2};
    box_mesh_t mesh = {
        .vertices = vertices, .indices = indices, .vertex_count = 3, .index_count = 3
    };
    float scale[3] = {1, 1, 1};
    box_particle_point_t out[4];
    test_int(box_particle_mesh_table(&mesh, scale, 0, out, 4), 0);
    test_int(box_particle_mesh_table(NULL, scale, 0, out, 4), 0);
    ecs_fini(world);
}

void Simulate_cone_within_spread(void) {
    // particle/simulate
    float dirs[3][3] = {{0, 1, 0}, {0, 0, -1}, {0.6f, 0, 0.8f}};
    float spread = 30, cos_spread = cosf(glm_rad(spread)), widest = 1;
    int32_t d, i;
    for (d = 0; d < 3; d ++) {
        for (i = 0; i < DRAWS; i ++) {
            float u[2] = {
                box_particle_unit(box_particle_hash(1, (uint32_t)i, BOX_PARTICLE_STREAM_DIRECTION)),
                box_particle_unit(
                    box_particle_hash(1, (uint32_t)i, BOX_PARTICLE_STREAM_DIRECTION + 1))
            }, v[3];
            box_particle_cone(dirs[d], cos_spread, u, v);
            float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            float cos_v = v[0] * dirs[d][0] + v[1] * dirs[d][1] + v[2] * dirs[d][2];
            test_assert(fabsf(len - 1) < 1e-4f);
            test_assert(cos_v >= cos_spread - 1e-4f);
            widest = cos_v < widest ? cos_v : widest;
        }
    }

    test_assert(widest < cos_spread + 0.01f);

    float u[2] = {0.7f, 0.2f}, v[3];
    box_particle_cone(dirs[0], 1, u, v);
    test_assert(fabsf(v[1] - 1) < 1e-5f);
}

void Simulate_cone_zero_direction(void) {
    // particle/simulate
    float n[3] = {0, 0, 0}, u[2] = {0.5f, 0.5f}, v[3];
    box_particle_cone(n, 0, u, v);
    test_flt(v[0], 0);
    test_flt(v[1], 0);
    test_flt(v[2], 0);
}

void Simulate_integrate_constant_acceleration(void) {
    // particle/simulate
    float p[3] = {1, 2, 3}, v[3] = {0.5f, 4, -1}, a[3] = {0, -9.8f, 2};
    float p0[3] = {1, 2, 3}, v0[3] = {0.5f, 4, -1};
    float dt = 1.0f / 60.0f;
    int32_t i, c, n = 120;
    for (i = 0; i < n; i ++) {
        box_particle_integrate(p, v, a, dt);
    }

    float t = (float)n * dt;
    for (c = 0; c < 3; c ++) {
        float analytic = p0[c] + v0[c] * t + 0.5f * a[c] * t * t;
        float euler = analytic + 0.5f * a[c] * t * dt;
        test_assert(fabsf(v[c] - (v0[c] + a[c] * t)) < 1e-3f);
        test_assert(fabsf(p[c] - euler) < 1e-3f);
        test_assert(fabsf(p[c] - analytic) <= fabsf(0.5f * a[c] * t * dt) + 1e-3f);
    }
}

void Simulate_same_seed_same_draws(void) {
    // particle/simulate
    int32_t i;
    for (i = 0; i < 100; i ++) {
        float a[3], b[3];
        shape_draws(9, (uint32_t)i, a);
        shape_draws(9, (uint32_t)i, b);
        test_flt(a[0], b[0]);
        test_flt(a[1], b[1]);
        test_flt(a[2], b[2]);
        test_uint(box_particle_life(9, (uint32_t)i, 10, 1000),
            box_particle_life(9, (uint32_t)i, 10, 1000));
    }
}

void Simulate_other_seed_other_draws(void) {
    // particle/simulate
    int32_t i, same = 0, same_stream = 0;
    for (i = 0; i < 100; i ++) {
        same += box_particle_hash(1, (uint32_t)i, BOX_PARTICLE_STREAM_SPEED) ==
            box_particle_hash(2, (uint32_t)i, BOX_PARTICLE_STREAM_SPEED);
        same_stream += box_particle_hash(1, (uint32_t)i, BOX_PARTICLE_STREAM_SPEED) ==
            box_particle_hash(1, (uint32_t)i, BOX_PARTICLE_STREAM_SIZE);
        if (i) {
            same += box_particle_hash(1, (uint32_t)i, 0) ==
                box_particle_hash(1, (uint32_t)i - 1, 0);
        }
    }

    test_int(same, 0);
    test_int(same_stream, 0);
}
