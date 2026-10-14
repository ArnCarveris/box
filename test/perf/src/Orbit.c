/**
 * @file Orbit.c
 * @brief Tests for the camera a performance run orbits around the bounds of the scene.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

void Orbit_fits_bounds(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {-5, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {5, 2, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 2, .camera = camera });
    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->shapes, 2);
    test_assert(fabsf(status->center[0]) < 1e-4f);
    test_assert(fabsf(status->center[1] - 1.0f) < 1e-4f);
    test_assert(fabsf(status->center[2]) < 1e-4f);
    float radius = 0.5f * sqrtf(11 * 11 + 3 * 3 + 1);
    test_assert(fabsf(status->radius - radius) < 1e-4f);
    test_assert(status->distance * sinf(glm_rad(30)) >= status->radius);

    const Position *p = ecs_get(world, camera, Position);
    test_assert(p != NULL);
    vec3 eye = {p->x, p->y, p->z};
    test_assert(fabsf(glm_vec3_distance(eye, (float*)status->center) - status->distance) < 1e-3f);

    ecs_fini(world);
}

void Orbit_looks_at_center(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {10, 3, -4});
    ecs_set(world, a, Box, {4, 6, 2});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 45});

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 20, .period = 1, .camera = camera });
    for (int f = 0; f < 8; f ++) {
        ecs_progress(world, 0);
        if (f < 1) {
            continue;
        }

        const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
        const WorldTransform *wt = ecs_get(world, camera, WorldTransform);
        const Position *p = ecs_get(world, camera, Position);
        vec3 eye = {p->x, p->y, p->z};
        vec3 to;
        glm_vec3_sub((float*)status->center, eye, to);
        glm_vec3_normalize(to);
        vec3 forward = {-wt->m[2][0], -wt->m[2][1], -wt->m[2][2]};
        glm_vec3_normalize(forward);
        test_assert(glm_vec3_dot(forward, to) > 0.9999f);
        test_assert(fabsf(wt->m[0][1]) < 1e-4f);
        test_assert(wt->m[1][1] > 0);
        test_assert(fabsf(wt->m[3][0] - eye[0]) < 1e-4f);
        test_assert(fabsf(wt->m[3][2] - eye[2]) < 1e-4f);
    }

    ecs_fini(world);
}

void Orbit_moves_every_frame(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {2, 2, 2});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});

    ecs_singleton_set(world, PerfRun, {
        .fps = 60, .warmup = 2, .frames = 10, .period = 2, .camera = camera });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    Position prev = *ecs_get(world, camera, Position);
    for (int f = 0; f < 6; f ++) {
        ecs_progress(world, 0);
        Position cur = *ecs_get(world, camera, Position);
        float dx = cur.x - prev.x, dz = cur.z - prev.z;
        test_assert(sqrtf(dx * dx + dz * dz) > 0.01f);
        test_assert(fabsf(cur.y - prev.y) < 1e-4f);
        vec3 eye = {cur.x, cur.y, cur.z};
        const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
        test_assert(fabsf(glm_vec3_distance(eye, (float*)status->center) -
            status->distance) < 1e-3f);
        prev = cur;
    }

    ecs_fini(world);
}

void Orbit_first_camera_when_unset(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.fov = 60});

    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 2, .frames = 2 });
    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const Position *p = ecs_get(world, camera, Position);
    vec3 eye = {p->x, p->y, p->z};
    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_assert(fabsf(glm_vec3_distance(eye, (float*)status->center) - status->distance) < 1e-3f);
    test_assert(status->distance < 10);

    ecs_fini(world);
}

void Orbit_elevation(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 2, .elevation = 45, .camera = camera });
    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    const Position *p = ecs_get(world, camera, Position);
    const WorldTransform *wt = ecs_get(world, camera, WorldTransform);
    test_assert(fabsf(p->y - status->distance * sinf(glm_rad(45))) < 1e-3f);
    test_assert(fabsf(wt->m[2][1] - sinf(glm_rad(45))) < 1e-4f);

    ecs_fini(world);
}

void Orbit_no_shapes(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});

    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 2, .frames = 2 });
    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->shapes, 0);
    test_assert(status->state != PerfInvalid);
    test_assert(ecs_get(world, camera, Position) != NULL);

    ecs_fini(world);
}
