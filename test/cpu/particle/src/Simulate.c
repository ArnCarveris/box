/**
 * @file Simulate.c
 * @brief Tests for the C twin of the particle simulation: the hash, the range draws, the spawn
 * shapes, the cone, the step and the mesh table.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
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
    box_particle_forces_t f = { .acceleration = {0, -9.8f, 2}, .scale = 1 };
    int32_t i, c, n = 120;
    for (i = 0; i < n; i ++) {
        box_particle_integrate(p, v, &f, dt);
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

/** Frame delta of the step tests */
#define DT (1.0f / 60.0f)

/** Create an emitter with the constructor values of ParticleEmitter and some ranges.
 *
 * @return The emitter
 */
static ParticleEmitter sample_emitter(void)
{
    return (ParticleEmitter){
        .max = 1000,
        .direction = {0, 1, 0},
        .spread = 40,
        .speed = {1, 3},
        .radial = {0.5f, 2},
        .lifetime = {1, 2},
        .size = {0.1f, 0.4f},
        .rotation = {-30, 30},
        .spin = {10, 90},
        .opacity = {0.25f, 0.75f},
        .color = {128, 64, 200},
        .color_variance = {20, 0, 40},
        .seed = 11
    };
}

/** Check that two samples are identical.
 *
 * @param a The first sample
 * @param b The second sample
 */
static void sample_equal(
    const box_particle_sample_t *a,
    const box_particle_sample_t *b)
{
    test_assert(memcmp(a, b, sizeof(box_particle_sample_t)) == 0);
}

void Simulate_integrate_drag_toward_wind(void) {
    // particle/forces
    box_particle_forces_t f = { .wind = {3, 0, -2}, .drag = 2, .scale = 1 };
    float p0[3] = {1, 2, 3}, v0[3] = {0, 5, 1};
    float p[3] = {1, 2, 3}, v[3] = {0, 5, 1};
    int32_t i, c, n = 120;
    for (i = 0; i < n; i ++) {
        box_particle_integrate(p, v, &f, DT);
    }

    float t = (float)n * DT, e = expf(-f.drag * DT), decay = expf(-f.drag * t);
    for (c = 0; c < 3; c ++) {
        float u0 = v0[c] - f.wind[c];
        float exact = f.wind[c] + u0 * decay;
        float steps = p0[c] + f.wind[c] * t + u0 * DT * e * (1 - decay) / (1 - e);
        float analytic = p0[c] + f.wind[c] * t + u0 * (1 - decay) / f.drag;
        test_assert(fabsf(v[c] - exact) < 1e-4f);
        test_assert(fabsf(p[c] - steps) < 1e-3f);
        test_assert(fabsf(p[c] - analytic) <= fabsf(u0) * DT + 1e-3f);
    }
}

void Simulate_integrate_gravity_and_drag(void) {
    // particle/forces
    box_particle_forces_t f = {
        .acceleration = {0, -9.81f, 0}, .wind = {1, 0, 0}, .drag = 1.5f, .scale = 1
    };
    float v0[3] = {0, 8, 2}, v[3] = {0, 8, 2}, p[3] = {0, 0, 0};
    int32_t i, c, n = 90;
    for (i = 0; i < n; i ++) {
        box_particle_integrate(p, v, &f, DT);
    }

    float t = (float)n * DT, e = expf(-f.drag * DT), decay = expf(-f.drag * t);
    for (c = 0; c < 3; c ++) {
        float g = f.acceleration[c];
        float fixed = g * DT * e / (1 - e);
        float steps = f.wind[c] + fixed + (v0[c] - f.wind[c] - fixed) * powf(e, (float)n);
        float analytic = f.wind[c] + g / f.drag +
            (v0[c] - f.wind[c] - g / f.drag) * decay;
        test_assert(fabsf(v[c] - steps) < 1e-3f);
        test_assert(fabsf(v[c] - analytic) <= fabsf(g) * DT + 1e-3f);
    }

    for (i = 0; i < 1200; i ++) {
        box_particle_integrate(p, v, &f, DT);
    }

    test_assert(fabsf(v[0] - 1) < 1e-3f);
    test_assert(fabsf(v[1] - (-9.81f / 1.5f)) < 9.81f * DT);
    test_assert(fabsf(v[2]) < 1e-3f);
}

void Simulate_integrate_wind_needs_drag(void) {
    // particle/forces
    box_particle_forces_t f = { .wind = {10, 10, 10}, .scale = 1 };
    float p[3] = {0, 0, 0}, v[3] = {1, 2, 3};
    int32_t i;
    for (i = 0; i < 60; i ++) {
        box_particle_integrate(p, v, &f, DT);
    }

    test_flt(v[0], 1);
    test_flt(v[1], 2);
    test_flt(v[2], 3);
    test_assert(fabsf(p[0] - 1) < 1e-4f);
    test_assert(fabsf(p[1] - 2) < 1e-4f);
    test_assert(fabsf(p[2] - 3) < 1e-4f);
}

void Simulate_wander_bounded(void) {
    // particle/forces
    float phase[3], w[3], q[3], near_w[3], largest = 0;
    int32_t i, c;
    box_particle_phase(4, phase);
    for (i = 0; i < DRAWS; i ++) {
        float pos[3];
        shape_draws(5, (uint32_t)i, pos);
        for (c = 0; c < 3; c ++) {
            pos[c] = pos[c] * 40 - 20;
            q[c] = pos[c] + 1e-3f;
        }

        box_particle_wander(pos, phase, 1.5f, 3, w);
        box_particle_wander(q, phase, 1.5f, 3, near_w);
        for (c = 0; c < 3; c ++) {
            test_assert(fabsf(w[c]) <= 3.0001f);
            test_assert(fabsf(w[c] - near_w[c]) < 0.02f);
            largest = fabsf(w[c]) > largest ? fabsf(w[c]) : largest;
        }

        box_particle_wander(pos, phase, 1.5f, 0, w);
        test_flt(w[0], 0);
        test_flt(w[1], 0);
        test_flt(w[2], 0);
    }

    test_assert(largest > 2.5f);
}

void Simulate_wander_scale_and_phase(void) {
    // particle/forces
    float a[3], b[3], again[3], other[3], w1[3], w2[3];
    float pos[3] = {1.25f, -0.5f, 3}, twice[3] = {2.5f, -1, 6};
    int32_t c;
    box_particle_phase(7, a);
    box_particle_phase(7, again);
    box_particle_phase(8, other);
    for (c = 0; c < 3; c ++) {
        test_assert(a[c] >= 0 && a[c] < 64);
        test_flt(a[c], again[c]);
    }

    test_assert(a[0] != other[0] || a[1] != other[1] || a[2] != other[2]);
    box_particle_wander(pos, a, 2, 1, w1);
    box_particle_wander(twice, a, 1, 1, w2);
    for (c = 0; c < 3; c ++) {
        test_assert(fabsf(w1[c] - w2[c]) < 1e-5f);
    }

    box_particle_wander(pos, a, 1, 1, w1);
    box_particle_wander(pos, other, 1, 1, w2);
    test_assert(w1[0] != w2[0] || w1[1] != w2[1] || w1[2] != w2[2]);
    b[0] = b[1] = b[2] = 0;
    box_particle_wander(b, b, 1, 2, w1);
    test_flt(w1[0], 0);
    test_flt(w1[1], 0);
    test_flt(w1[2], 0);
}

void Simulate_integrate_turbulence(void) {
    // particle/forces
    box_particle_forces_t f = {
        .acceleration = {0, -1, 0}, .turbulence = 4, .scale = 0.5f
    };
    float p[3] = {2, 1, -3}, v[3] = {1, 0, 0}, w[3], expect_v[3], expect_p[3];
    int32_t c;
    box_particle_phase(3, f.phase);
    box_particle_wander(p, f.phase, f.scale, f.turbulence, w);
    for (c = 0; c < 3; c ++) {
        expect_v[c] = v[c] + (f.acceleration[c] + w[c]) * DT;
        expect_p[c] = p[c] + expect_v[c] * DT;
    }

    box_particle_integrate(p, v, &f, DT);
    for (c = 0; c < 3; c ++) {
        test_assert(fabsf(v[c] - expect_v[c]) < 1e-6f);
        test_assert(fabsf(p[c] - expect_p[c]) < 1e-6f);
    }

    test_assert(fabsf(w[0]) + fabsf(w[2]) > 0);
}

void Simulate_color_within_variance(void) {
    // particle/simulate
    float color[3] = {0.5f, 0.2f, 0.95f}, variance[3] = {0.1f, 0, 0.2f}, out[3];
    float lo[3] = {1, 1, 1}, hi[3] = {0, 0, 0};
    int32_t i, c, same_rb = 0;
    for (i = 0; i < DRAWS; i ++) {
        box_particle_color(6, (uint32_t)i, color, variance, out);
        for (c = 0; c < 3; c ++) {
            float min = box_particle_decode(fmaxf(color[c] - variance[c], 0));
            float max = box_particle_decode(fminf(color[c] + variance[c], 1));
            test_assert(out[c] >= min - 1e-6f && out[c] <= max + 1e-6f);
            lo[c] = out[c] < lo[c] ? out[c] : lo[c];
            hi[c] = out[c] > hi[c] ? out[c] : hi[c];
        }

        test_flt(out[1], box_particle_decode(0.2f));
        same_rb += (out[0] - box_particle_decode(0.5f)) * (out[2] - box_particle_decode(0.95f))
            > 0;
    }

    test_assert(lo[0] < box_particle_decode(0.41f));
    test_assert(hi[0] > box_particle_decode(0.59f));
    test_assert(fabsf(hi[2] - 1) < 1e-6f);
    test_assert(same_rb > DRAWS / 3 && same_rb < 2 * DRAWS / 3);
    test_assert(fabsf(box_particle_decode(1) - 1) < 1e-6f);
    test_flt(box_particle_decode(0), 0);
}

void Simulate_sample_in_range(void) {
    // particle/simulate
    ParticleEmitter e = sample_emitter();
    float cos_spread = cosf(glm_rad(e.spread));
    int32_t i, c;
    for (i = 0; i < DRAWS; i ++) {
        box_particle_sample_t s;
        box_particle_sample(&e, (uint32_t)i, &s);
        float len = sqrtf(s.direction[0] * s.direction[0] + s.direction[1] * s.direction[1] +
            s.direction[2] * s.direction[2]);
        test_assert(fabsf(len - 1) < 1e-4f);
        test_assert(s.direction[1] >= cos_spread - 1e-4f);
        test_assert(s.speed >= 1 && s.speed < 3);
        test_assert(s.radial >= 0.5f && s.radial < 2);
        test_assert(s.life >= 1000000 && s.life <= 2000000);
        test_assert(s.size >= 0.1f && s.size < 0.4f);
        test_assert(s.rotation >= -30 && s.rotation < 30);
        test_assert(s.spin >= 10 && s.spin < 90);
        test_assert(s.opacity >= 0.25f && s.opacity < 0.75f);
        float base[3] = {128, 64, 200}, var[3] = {20, 0, 40};
        for (c = 0; c < 3; c ++) {
            float min = box_particle_decode((base[c] - var[c]) / 255.0f);
            float max = box_particle_decode((base[c] + var[c]) / 255.0f);
            test_assert(s.color[c] >= min - 1e-6f && s.color[c] <= max + 1e-6f);
        }
    }
}

void Simulate_sample_streams_independent(void) {
    // particle/simulate
    ParticleEmitter base = sample_emitter();
    int32_t change, i;
    for (change = 0; change < 9; change ++) {
        ParticleEmitter e = base;
        int32_t moved = 0;
        switch (change) {
        case 0: e.speed = (ParticleRange){5, 9}; break;
        case 1: e.size = (ParticleRange){1, 2}; break;
        case 2: e.lifetime = (ParticleRange){3, 7}; break;
        case 3: e.rotation = (ParticleRange){0, 360}; break;
        case 4: e.spin = (ParticleRange){-200, -100}; break;
        case 5: e.opacity = (ParticleRange){0, 1}; break;
        case 6: e.spread = 90; break;
        case 7: e.color_variance = (rgb){60, 60, 60}; break;
        default: e.radial = (ParticleRange){5, 9}; break;
        }

        for (i = 0; i < 200; i ++) {
            box_particle_sample_t a, b;
            box_particle_sample(&base, (uint32_t)i, &a);
            box_particle_sample(&e, (uint32_t)i, &b);
            moved += memcmp(&a, &b, sizeof(a)) != 0;
            if (change != 0) {
                test_flt(a.speed, b.speed);
            }

            if (change != 1) {
                test_flt(a.size, b.size);
            }

            if (change != 2) {
                test_uint(a.life, b.life);
            }

            if (change != 3) {
                test_flt(a.rotation, b.rotation);
            }

            if (change != 4) {
                test_flt(a.spin, b.spin);
            }

            if (change != 5) {
                test_flt(a.opacity, b.opacity);
            }

            if (change != 6) {
                test_flt(a.direction[0], b.direction[0]);
                test_flt(a.direction[1], b.direction[1]);
                test_flt(a.direction[2], b.direction[2]);
            }

            if (change != 7) {
                test_flt(a.color[0], b.color[0]);
                test_flt(a.color[1], b.color[1]);
                test_flt(a.color[2], b.color[2]);
            }

            if (change != 8) {
                test_flt(a.radial, b.radial);
            }
        }

        test_assert(moved > 190);
    }
}

void Simulate_sample_fixed_seed(void) {
    // particle/simulate
    ParticleEmitter e = sample_emitter(), other = sample_emitter();
    int32_t i, differ = 0;
    other.seed = 12;
    for (i = 0; i < 200; i ++) {
        box_particle_sample_t a, b, c;
        box_particle_sample(&e, (uint32_t)i, &a);
        box_particle_sample(&e, (uint32_t)i, &b);
        box_particle_sample(&other, (uint32_t)i, &c);
        sample_equal(&a, &b);
        differ += a.speed != c.speed && a.size != c.size && a.life != c.life &&
            a.color[0] != c.color[0] && a.direction[0] != c.direction[0];
    }

    test_assert(differ > 190);
}

void Simulate_radial_along_offset(void) {
    // particle/simulate
    float offset[3] = {3, 0, -4}, v[3];
    box_particle_radial(offset, 2, v);
    test_assert(fabsf(v[0] - 1.2f) < 1e-6f);
    test_flt(v[1], 0);
    test_assert(fabsf(v[2] + 1.6f) < 1e-6f);

    float tiny[3] = {0, 1e-6f, 0};
    box_particle_radial(tiny, 5, v);
    test_flt(v[0], 0);
    test_assert(fabsf(v[1] - 5) < 1e-5f);
    test_flt(v[2], 0);

    box_particle_radial(offset, 0, v);
    test_flt(v[0], 0);
    test_flt(v[1], 0);
    test_flt(v[2], 0);
}

void Simulate_radial_at_origin(void) {
    // particle/simulate
    float offset[3] = {0, 0, 0}, v[3] = {1, 1, 1};
    box_particle_radial(offset, 3, v);
    test_flt(v[0], 0);
    test_flt(v[1], 0);
    test_flt(v[2], 0);
}

void Simulate_ground_bounce(void) {
    // particle/collision
    box_particle_ground_t g = {
        .mode = BOX_PARTICLE_GROUND_BOUNCE, .height = 0, .bounce = 0.5f
    };
    float p[3] = {1, -0.2f, 2}, v[3] = {2, -4, 1};
    test_bool(box_particle_collide(p, v, &g), true);
    test_flt(p[0], 1);
    test_flt(p[1], 0);
    test_flt(p[2], 2);
    test_flt(v[0], 2);
    test_flt(v[1], 2);
    test_flt(v[2], 1);

    p[1] = -0.1f;
    v[1] = 3;
    test_bool(box_particle_collide(p, v, &g), true);
    test_flt(p[1], 0);
    test_flt(v[1], 3);

    box_particle_forces_t f = { .acceleration = {0, -9.81f, 0}, .scale = 1 };
    float q[3] = {0, 5, 0}, w[3] = {0, 0, 0}, peak = 0;
    int32_t i, hits = 0;
    bool falling = true;
    for (i = 0; i < 1200; i ++) {
        box_particle_integrate(q, w, &f, 1.0f / 600.0f);
        bool down = w[1] < 0;
        box_particle_collide(q, w, &g);
        test_assert(q[1] >= 0);
        if (down && w[1] > 0) {
            hits ++;
            falling = false;
        }

        if (!falling && hits == 1) {
            peak = q[1] > peak ? q[1] : peak;
        }
    }

    test_assert(hits >= 1);
    test_assert(fabsf(peak - 5 * 0.25f) < 0.1f);
}

void Simulate_ground_friction(void) {
    // particle/collision
    box_particle_ground_t g = {
        .mode = BOX_PARTICLE_GROUND_BOUNCE, .height = 1, .bounce = 0, .friction = 0.25f
    };
    float p[3] = {0, 0.9f, 0}, v[3] = {4, -1, -2};
    test_bool(box_particle_collide(p, v, &g), true);
    test_flt(p[1], 1);
    test_flt(v[0], 3);
    test_flt(v[1], 0);
    test_flt(v[2], -1.5f);

    box_particle_forces_t f = { .acceleration = {0, -9.81f, 0}, .scale = 1 };
    float q[3] = {0, 1, 0}, w[3] = {2, 0, 0};
    int32_t i;
    for (i = 0; i < 10; i ++) {
        box_particle_integrate(q, w, &f, DT);
        box_particle_collide(q, w, &g);
        test_flt(q[1], 1);
    }

    test_assert(fabsf(w[0] - 2 * powf(0.75f, 10)) < 1e-5f);
}

void Simulate_ground_die(void) {
    // particle/collision
    box_particle_ground_t g = {
        .mode = BOX_PARTICLE_GROUND_DIE, .height = 0.5f, .bounce = 1, .friction = 1
    };
    float p[3] = {0, 0.4f, 0}, v[3] = {1, -1, 1};
    test_bool(box_particle_collide(p, v, &g), false);

    float q[3] = {0, 0.6f, 0}, w[3] = {1, -1, 1};
    test_bool(box_particle_collide(q, w, &g), true);
    test_flt(q[1], 0.6f);
    test_flt(w[0], 1);
    test_flt(w[1], -1);
}

void Simulate_ground_above_plane(void) {
    // particle/collision
    box_particle_ground_t g = {
        .mode = BOX_PARTICLE_GROUND_BOUNCE, .height = 2, .bounce = 0.5f, .friction = 0.5f
    };
    float p[3] = {0, 2, 0}, v[3] = {1, -1, 1};
    test_bool(box_particle_collide(p, v, &g), true);
    test_flt(p[1], 2);
    test_flt(v[0], 1);
    test_flt(v[1], -1);

    box_particle_ground_t none = { .mode = BOX_PARTICLE_GROUND_NONE, .height = 2 };
    p[1] = -5;
    test_bool(box_particle_collide(p, v, &none), true);
    test_flt(p[1], -5);
    test_flt(v[1], -1);
    test_flt(v[2], 1);
}
