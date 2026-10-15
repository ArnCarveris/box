/**
 * @file Pacing.c
 * @brief Tests for the sleep a headless process inserts when it exceeds its CPU budget.
 */

#include <box.h>
#include <app.h>
#include <bake_test.h>
#include <stdlib.h>
#include <time.h>

static void Busy(ecs_iter_t *it) {
    (void)it;
    clock_t start = clock();
    while ((double)(clock() - start) < (double)CLOCKS_PER_SEC * 0.02) {
    }
}

/** Process CPU and wall time at a point of a frame */
typedef struct {
    bool marked;      /**< The mark was taken */
    clock_t cpu;      /**< Process CPU time in clock ticks */
    ecs_time_t wall;  /**< Wall time */
} pacing_mark_t;

static void MarkOnce(ecs_iter_t *it) {
    pacing_mark_t *mark = it->ctx;
    if (!mark->marked) {
        mark->marked = true;
        mark->cpu = clock();
        ecs_os_get_time(&mark->wall);
    }
}

void Pacing_sleep_over_budget(void) {
    // engine_core/app
    test_flt(box_app_pace(0.02, 0.02, 50), 0.02);
    test_flt(box_app_pace(0.03, 0.01, 100), 0.02);
}

void Pacing_no_sleep_within_budget(void) {
    // engine_core/app
    test_flt(box_app_pace(0.01, 0.02, 50), 0);
    test_flt(box_app_pace(0.02, 0.02, 100), 0);
}

void Pacing_budget_above_one_core(void) {
    // engine_core/app
    test_flt(box_app_pace(0.04, 0.02, 200), 0);
    test_flt(box_app_pace(0.06, 0.02, 200), 0.01);
}

void Pacing_sleeps_when_over_budget(void) {
    // test_fixes/pacing_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    pacing_mark_t start = {0};
    ecs_system(world, {
        .phase = EcsOnStore,
        .run = MarkOnce,
        .ctx = &start
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 3, .cpu_budget = 10, .unguarded = true });

    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    double wall = ecs_time_measure(&start.wall);
    double cpu = (double)(clock() - start.cpu) / (double)CLOCKS_PER_SEC;
    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    test_assert(cpu >= 0.04);
    test_assert(cpu * 100.0 / 10.0 <= wall * 1.02);
    test_assert(status->slept <= cpu * (100.0 / 10.0 - 1.0) * 1.02);

    ecs_fini(world);
}

void Pacing_unpaced_never_sleeps(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 6, .cpu_budget = 50, .unguarded = true,
        .unpaced = true });

    for (int i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_flt(ecs_singleton_get(world, AppStatus)->slept, 0);

    ecs_fini(world);
}

void Pacing_not_headless_never_sleeps(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_singleton_set(world, App, { .frames = 6, .cpu_budget = 50 });

    for (int i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_flt(ecs_singleton_get(world, AppStatus)->slept, 0);

    ecs_fini(world);
}

void Pacing_env_sets_unpaced(void) {
    // engine_core/app
    setenv("BOX_TEST", "1", 1);
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unpaced, true);

    ecs_fini(world);

    setenv("BOX_TEST", "0", 1);
    world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unpaced, false);

    ecs_fini(world);
}

/** Process CPU and wall time at the start of each frame and the wall time of its work */
typedef struct {
    int32_t count;          /**< Frames marked */
    clock_t cpu[16];        /**< Process CPU time in clock ticks at the start of the frame */
    double wall[16];        /**< Wall seconds at the start of the frame */
    double work[16];        /**< Wall seconds from the start of the frame to its store phase */
} pacing_frames_t;

static void MarkFrame(ecs_iter_t *it) {
    pacing_frames_t *frames = it->ctx;
    ecs_time_t t;
    ecs_os_get_time(&t);
    if (frames->count < 16) {
        frames->cpu[frames->count] = clock();
        frames->wall[frames->count] = (double)t.sec + (double)t.nanosec * 1e-9;
        frames->count ++;
    }
}

static void MarkWork(ecs_iter_t *it) {
    pacing_frames_t *frames = it->ctx;
    ecs_time_t t;
    ecs_os_get_time(&t);
    if (frames->count > 0 && frames->count <= 16) {
        double now = (double)t.sec + (double)t.nanosec * 1e-9;
        frames->work[frames->count - 1] = now - frames->wall[frames->count - 1];
    }
}

void Pacing_budget_share_at_half(void) {
    // core_fixes/budget_sleep_overshoot
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    pacing_frames_t frames = {0};
    ecs_system(world, {
        .phase = EcsOnLoad,
        .run = MarkFrame,
        .ctx = &frames
    });

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_system(world, {
        .phase = EcsOnStore,
        .run = MarkWork,
        .ctx = &frames
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 10, .cpu_budget = 50, .unguarded = true });

    for (int i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    test_int(frames.count, 10);
    double expected = 0;
    for (int i = 1; i < 9; i ++) {
        double cpu = (double)(frames.cpu[i + 1] - frames.cpu[i]) / (double)CLOCKS_PER_SEC;
        double needed = cpu * 100.0 / 50.0;
        expected += needed > frames.work[i] ? needed : frames.work[i];
    }

    double wall = frames.wall[9] - frames.wall[1];
    double cpu = (double)(frames.cpu[9] - frames.cpu[1]) / (double)CLOCKS_PER_SEC;
    test_assert(cpu >= 0.15);
    test_assert(cpu * 100.0 / 50.0 <= wall * 1.02);
    test_assert(wall <= expected * 1.03 + 0.005);

    ecs_fini(world);
}

void Pacing_budget_share_at_quarter(void) {
    // core_fixes/budget_sleep_overshoot
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    pacing_frames_t frames = {0};
    ecs_system(world, {
        .phase = EcsOnLoad,
        .run = MarkFrame,
        .ctx = &frames
    });

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_system(world, {
        .phase = EcsOnStore,
        .run = MarkWork,
        .ctx = &frames
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 10, .cpu_budget = 25, .unguarded = true });

    for (int i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    test_int(frames.count, 10);
    double expected = 0;
    for (int i = 1; i < 9; i ++) {
        double cpu = (double)(frames.cpu[i + 1] - frames.cpu[i]) / (double)CLOCKS_PER_SEC;
        double needed = cpu * 100.0 / 25.0;
        expected += needed > frames.work[i] ? needed : frames.work[i];
    }

    double wall = frames.wall[9] - frames.wall[1];
    double cpu = (double)(frames.cpu[9] - frames.cpu[1]) / (double)CLOCKS_PER_SEC;
    test_assert(cpu >= 0.15);
    test_assert(cpu * 100.0 / 25.0 <= wall * 1.02);
    test_assert(wall <= expected * 1.03 + 0.005);

    ecs_fini(world);
}

void Pacing_idle_earns_no_credit(void) {
    // core_fixes/budget_sleep_overshoot
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 4, .cpu_budget = 25, .unguarded = true });

    ecs_progress(world, 0);
    ecs_sleepf(0.3);
    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, AppStatus)->slept, 0);

    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, AppStatus)->slept > 0.04);

    ecs_fini(world);
}
