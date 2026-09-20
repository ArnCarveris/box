/**
 * @file Cpu.c
 * @brief Tests for the CpuMonitor singleton and the budget that shares its sampling.
 */

#include <box.h>
#include <bake_test.h>

static void BoxTestBurn(
    ecs_iter_t *it)
{
    (void)it;
    ecs_time_t start;
    ecs_os_get_time(&start);
    volatile double sink = 0;
    double elapsed = 0;
    while (elapsed < 0.003) {
        int32_t i;
        for (i = 0; i < 4096; i ++) {
            sink += (double)i;
        }

        ecs_time_t now;
        ecs_os_get_time(&now);
        elapsed = (double)(now.sec - start.sec) +
            ((double)now.nanosec - (double)start.nanosec) * 1e-9;
    }
}

static void BoxTestLoad(
    ecs_iter_t *it)
{
    (void)it;
    ecs_time_t start;
    ecs_os_get_time(&start);
    volatile double sink = 0;
    double elapsed = 0;
    while (elapsed < 0.003) {
        int32_t i;
        for (i = 0; i < 4096; i ++) {
            sink += (double)i;
        }

        ecs_time_t now;
        ecs_os_get_time(&now);
        elapsed = (double)(now.sec - start.sec) +
            ((double)now.nanosec - (double)start.nanosec) * 1e-9;
    }
}

static void BoxTestIdle(
    ecs_iter_t *it)
{
    (void)it;
    ecs_os_sleep(0, 3000000);
}

void Cpu_absent_monitor_is_not_sampled(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.01f);
    }

    test_assert(ecs_singleton_get(world, CpuMonitor) == NULL);
    ecs_fini(world);
}

void Cpu_sample_reports_the_cores(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, CpuMonitor);
    test_flt(ecs_singleton_get(world, CpuMonitor)->window_s, 1.0);
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    const CpuMonitor *m = ecs_singleton_get(world, CpuMonitor);
    test_assert(m->cores >= 1);
    test_assert(m->cores <= 1024);
    ecs_fini(world);
}

void Cpu_busy_loop_raises_the_process_percent(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CpuMonitor, {.window_s = 0.02});
    ECS_SYSTEM(world, BoxTestIdle, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 50; i ++) {
        ecs_progress(world, 0.003f);
    }

    double idle = ecs_singleton_get(world, CpuMonitor)->process_percent;
    ecs_enable(world, ecs_lookup(world, "BoxTestIdle"), false);
    ECS_SYSTEM(world, BoxTestBurn, EcsOnUpdate, 0);
    for (i = 0; i < 50; i ++) {
        ecs_progress(world, 0.003f);
    }

    double busy = ecs_singleton_get(world, CpuMonitor)->process_percent;
    test_assert(idle < 50.0);
    test_assert(busy > idle + 20.0);
    ecs_fini(world);
}

void Cpu_system_percent_stays_in_range(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, CpuMonitor);
    ECS_SYSTEM(world, BoxTestBurn, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 20; i ++) {
        ecs_progress(world, 0.003f);
    }

    const CpuMonitor *m = ecs_singleton_get(world, CpuMonitor);
    test_assert(m->system_percent >= 0.0);
    test_assert(m->system_percent <= 100.0);
    test_assert(m->core_percent_max >= 0.0);
    test_assert(m->core_percent_max <= 100.0);
    test_assert(m->core_percent_max >= m->system_percent - 0.001);
    ecs_fini(world);
}

void Cpu_short_window_follows_the_load(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CpuMonitor, {.window_s = 0.005});
    ECS_SYSTEM(world, BoxTestBurn, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 30; i ++) {
        ecs_progress(world, 0.003f);
    }

    test_assert(ecs_singleton_get(world, CpuMonitor)->process_percent > 50.0);
    ecs_fini(world);
}

void Cpu_long_window_lags_the_load(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CpuMonitor, {.window_s = 1000.0});
    ECS_SYSTEM(world, BoxTestBurn, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 30; i ++) {
        ecs_progress(world, 0.003f);
    }

    test_assert(ecs_singleton_get(world, CpuMonitor)->process_percent < 20.0);
    ecs_fini(world);
}

void Cpu_removing_the_monitor_stops_sampling(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CpuMonitor, {.window_s = 0.005});
    ECS_SYSTEM(world, BoxTestBurn, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 30; i ++) {
        ecs_progress(world, 0.003f);
    }

    test_assert(ecs_singleton_get(world, CpuMonitor)->process_percent > 50.0);
    ecs_singleton_remove(world, CpuMonitor);
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0.003f);
    }

    test_assert(ecs_singleton_get(world, CpuMonitor) == NULL);
    ecs_singleton_add(world, CpuMonitor);
    const CpuMonitor *m = ecs_singleton_get(world, CpuMonitor);
    test_flt(m->process_percent, 0.0);
    test_flt(m->window_s, 1.0);
    ecs_fini(world);
}

void Cpu_budget_sleep_is_published(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, CpuMonitor);
    ECS_SYSTEM(world, BoxTestBurn, EcsOnUpdate, 0);
    App *app = ecs_singleton_ensure(world, App);
    app->frames = 20;
    app->cpu_budget = 0.1;
    test_int(box_run(world), 0);
    test_assert(ecs_singleton_get(world, CpuMonitor)->budget_sleep_ms > 0.0);
    ecs_fini(world);
}

void Cpu_budget_slows_a_synthetic_load(void)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, CpuMonitor);
    ECS_SYSTEM(world, BoxTestBurn, EcsOnUpdate, 0);
    App *app = ecs_singleton_ensure(world, App);
    app->frames = 20;
    app->cpu_budget = 0.1;
    ecs_time_t start;
    ecs_os_get_time(&start);
    test_int(box_run(world), 0);
    double budgeted = ecs_time_measure(&start);
    ecs_fini(world);

    world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, CpuMonitor);
    ECS_SYSTEM(world, BoxTestLoad, EcsOnUpdate, 0);
    app = ecs_singleton_ensure(world, App);
    app->frames = 20;
    ecs_os_get_time(&start);
    test_int(box_run(world), 0);
    double uncapped = ecs_time_measure(&start);
    ecs_fini(world);

    test_assert(budgeted > uncapped * 2);
}
