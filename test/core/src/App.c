/**
 * @file App.c
 * @brief Tests for the App singleton loop: stop conditions, process folder and CPU budget.
 */

#include <box.h>
#include <bake_test.h>
#include <sys/stat.h>
#include <string.h>
#include "../../../src/core/core_internal.h"
#include "../../../src/job/job_internal.h"

static void QuitAfterTwo(
    ecs_iter_t *it)
{
    if (ecs_singleton_get(it->world, App)->frame == 2) {
        ecs_quit(it->world);
    }
}

static void JobBusySpin(
    void *ctx)
{
    (void)ctx;
    ecs_time_t t0;
    ecs_os_get_time(&t0);
    ecs_time_t probe = t0;
    while (ecs_time_measure(&probe) < 1.2) {
        probe = t0;
    }
}

static void SubmitBusyJobs(
    ecs_iter_t *it)
{
    const App *app = ecs_singleton_get(it->world, App);
    if (app->frame == 0) {
        int32_t i;
        for (i = 0; i < 8; i ++) {
            box_job_desc_t desc = {0};
            desc.run = JobBusySpin;
            box_job_submit(it->world, &desc);
        }
    }
}

void App_stops_after_frames(void)
{
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_ensure(world, App);
    app->frames = 5;
    test_int(box_run(world), 0);
    test_int(ecs_singleton_get(world, App)->frame, 5);
    ecs_fini(world);
}

void App_stops_after_timeout(void)
{
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_ensure(world, App);
    app->timeout = 0.2;
    ecs_time_t start;
    ecs_os_get_time(&start);
    test_int(box_run(world), 0);
    double elapsed = ecs_time_measure(&start);
    test_assert(elapsed >= 0.2);
    test_assert(elapsed < 2.0);
    test_assert(ecs_singleton_get(world, App)->frame > 0);
    ecs_fini(world);
}

void App_stops_on_quit(void)
{
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_SYSTEM(world, QuitAfterTwo, EcsOnUpdate, 0);
    test_int(box_run(world), 0);
    test_int(ecs_singleton_get(world, App)->frame, 2);
    ecs_fini(world);
}

void App_process_dir_created_and_removed(void)
{
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    const App *app = ecs_singleton_get(world, App);
    test_assert(app->process_dir != NULL);
#ifdef _WIN32
    char tmp[PATH_MAX];
    box_win32_temp_dir(tmp, sizeof(tmp));
    char *root = flecs_asprintf("%s/box/", tmp);
#else
    char *root = ecs_os_strdup("/tmp/box/");
#endif
    test_assert(strncmp(app->process_dir, root, strlen(root)) == 0);
    ecs_os_free(root);
    char *dir = ecs_os_strdup(app->process_dir);
    struct stat st;
    test_int(stat(dir, &st), 0);
    test_assert(S_ISDIR(st.st_mode));
    ecs_fini(world);
    test_assert(stat(dir, &st) != 0);
    ecs_os_free(dir);
}

void App_cpu_budget_slows_loop(void)
{
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_ensure(world, App);
    app->frames = 200;
    app->cpu_budget = 0.1;
    ecs_time_t start;
    ecs_os_get_time(&start);
    test_int(box_run(world), 0);
    double budgeted = ecs_time_measure(&start);
    ecs_fini(world);

    world = ecs_init();
    ECS_IMPORT(world, Box);
    app = ecs_singleton_ensure(world, App);
    app->frames = 200;
    ecs_os_get_time(&start);
    test_int(box_run(world), 0);
    double uncapped = ecs_time_measure(&start);
    ecs_fini(world);

    test_assert(budgeted > uncapped * 2);
}

void App_headless_default_budget_caps_busy_jobs(void)
{
    // ore_engine/headless_cpu_cap
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_SYSTEM(world, SubmitBusyJobs, EcsOnUpdate, 0);
    App *app = ecs_singleton_ensure(world, App);
    app->headless = true;
    app->timeout = 1.0;

    double cpu_start = box_cpu_process_time();
    double wall_start = box_cpu_wall_time();
    test_int(box_run(world), 0);
    double wall_used = box_cpu_wall_time() - wall_start;
    double cpu_used = box_cpu_process_time() - cpu_start;
    ecs_fini(world);

    test_assert(cpu_used < wall_used * 1.5);
}
