/**
 * @file Orbit.c
 * @brief Tests for the camera a performance run orbits around the bounds of the scene.
 */

#include <box.h>
#include <bake_test.h>
#include "fake.h"
#include <math.h>

void Orbit_fits_bounds(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {-5, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {5, 2, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});

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
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_looks_at_center(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {10, 3, -4});
    ecs_set(world, a, Box, {4, 6, 2});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 45, .near_ = 0.1f});

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
        vec3 forward = {-wt->basis[2][0], -wt->basis[2][1], -wt->basis[2][2]};
        glm_vec3_normalize(forward);
        test_assert(glm_vec3_dot(forward, to) > 0.9999f);
        test_assert(fabsf(wt->basis[0][1]) < 1e-4f);
        test_assert(wt->basis[1][1] > 0);
        test_assert(fabs(wt->position[0] - eye[0]) < 1e-4f);
        test_assert(fabs(wt->position[2] - eye[2]) < 1e-4f);
    }

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_moves_every_frame(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {2, 2, 2});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});

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
        test_assert(fabs(cur.y - prev.y) < 1e-4f);
        vec3 eye = {cur.x, cur.y, cur.z};
        const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
        test_assert(fabsf(glm_vec3_distance(eye, (float*)status->center) -
            status->distance) < 1e-3f);
        prev = cur;
    }

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_first_camera_when_unset(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});

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
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_elevation(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 2, .elevation = 45, .camera = camera });
    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    const Position *p = ecs_get(world, camera, Position);
    const WorldTransform *wt = ecs_get(world, camera, WorldTransform);
    test_assert(fabs(p->y - status->distance * sinf(glm_rad(45))) < 1e-3f);
    test_assert(fabsf(wt->basis[2][1] - sinf(glm_rad(45))) < 1e-4f);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_no_shapes(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});

    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 2, .frames = 2 });
    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->shapes, 0);
    test_assert(status->state != PerfInvalid);
    test_assert(ecs_get(world, camera, Position) != NULL);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_period(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {4, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 400, .period = 1, .camera = camera });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    Position start = *ecs_get(world, camera, Position);
    double t0 = fake_time;
    float cx = status->center[0], cz = status->center[2];
    int32_t frames = 0;
    while (fake_time - t0 < 0.5) {
        ecs_progress(world, 0);
        frames ++;
    }

    Position half = *ecs_get(world, camera, Position);
    test_assert(frames >= 59 && frames <= 61);
    test_assert(fabs((half.x - cx) + (start.x - cx)) < 0.1f);
    test_assert(fabs((half.z - cz) + (start.z - cz)) < 0.1f);

    while (fake_time - t0 < 1.0) {
        ecs_progress(world, 0);
    }

    Position full = *ecs_get(world, camera, Position);
    test_assert(fabs(full.x - start.x) < 0.1f);
    test_assert(fabs(full.z - start.z) < 0.1f);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_cull_ranges(void) {
    // testing/perf_orbit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 6, .period = 1, .camera = camera });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    int32_t visible[6] = {10, 12, 12, 7, 7, 15};
    int64_t frame[6] = {1, 2, 2, 3, 4, 5};
    for (int i = 0; i < 6; i ++) {
        ecs_set(world, view, CullStatus, {
            .frame = frame[i],
            .instances = 20,
            .visible = visible[i],
            .frustum_culled = 20 - visible[i] - 2,
            .occlusion_culled = 2,
            .batches_skipped = 4,
            .casters_skipped = 9 });
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->views, 1);
    test_int(status->cull_samples, 5);
    test_int(status->cull_changes, 3);
    test_int(status->visible.min, 7);
    test_int(status->visible.max, 15);
    test_assert(fabs(status->visible.mean - 51.0 / 5.0) < 1e-6);
    test_int(status->frustum_culled.min, 3);
    test_int(status->frustum_culled.max, 11);
    test_int(status->occlusion_culled.min, 2);
    test_int(status->occlusion_culled.max, 2);
    test_int(status->batches_skipped.min, 4);
    test_int(status->batches_skipped.max, 4);
    test_int(status->casters_skipped.min, 9);
    test_int(status->casters_skipped.max, 9);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_range_culled_sampled(void) {
    // testing/perf_orbit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 4, .period = 1, .camera = camera });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    int32_t ranged[4] = {5, 8, 8, 3};
    for (int i = 0; i < 4; i ++) {
        ecs_set(world, view, CullStatus, {
            .frame = i + 1,
            .instances = 20,
            .visible = 20 - ranged[i],
            .range_culled = ranged[i] });
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->cull_samples, 4);
    test_int(status->cull_changes, 2);
    test_int(status->range_culled.min, 3);
    test_int(status->range_culled.max, 8);
    test_assert(fabs(status->range_culled.mean - 24.0 / 4.0) < 1e-6);
    test_int(status->frustum_culled.max, 0);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_views_of_orbit(void) {
    // testing/perf_orbit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t lens = ecs_new_w_pair(world, EcsChildOf, camera);
    ecs_set(world, lens, Position, {0, 0, 0});
    ecs_set(world, lens, Camera, {.fov = 12});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {0, 2, 0});
    ecs_set(world, other, Camera, {.fov = 60});

    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, View, {.camera = lens});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, View, {.camera = other});
    ecs_set(world, far, CullStatus, {.frame = 1, .instances = 1, .visible = 1});

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 3, .period = 1, .camera = camera });
    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->views, 1);
    test_int(status->cull_samples, 0);

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 3, .period = 1, .camera = other });
    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->views, 1);
    test_int(status->cull_samples, 1);
    test_int(status->visible.max, 1);

    ecs_delete(world, near);
    ecs_delete(world, far);
    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 3, .period = 1, .camera = camera });
    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->views, 0);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Orbit_revolutions(void) {
    // testing/perf_orbit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});

    ecs_singleton_set(world, PerfRun, {
        .fps = 60, .warmup = 2, .frames = 90, .period = 0.5f, .camera = camera });
    for (int i = 0; i < 92; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->warmup, 2);
    test_flt(status->period, 0.5f);
    test_assert(fabsf(status->revolutions - 3.0f) < 0.01f);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}
