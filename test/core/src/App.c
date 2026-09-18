#include <box.h>
#include <bake_test.h>
#include <sys/stat.h>
#include <string.h>

static void QuitAfterTwo(ecs_iter_t *it) {
    if (ecs_singleton_get(it->world, App)->frame == 2) {
        ecs_quit(it->world);
    }
}

void App_stops_after_frames(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    App *app = ecs_singleton_ensure(world, App);
    app->frames = 5;
    test_int(box_run(world), 0);
    test_int(ecs_singleton_get(world, App)->frame, 5);
    ecs_fini(world);
}

void App_stops_after_timeout(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
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

void App_stops_on_quit(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    ECS_SYSTEM(world, QuitAfterTwo, EcsOnUpdate, 0);
    test_int(box_run(world), 0);
    test_int(ecs_singleton_get(world, App)->frame, 2);
    ecs_fini(world);
}

void App_process_dir_created_and_removed(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    const App *app = ecs_singleton_get(world, App);
    test_assert(app->process_dir != NULL);
    test_assert(strncmp(app->process_dir, "/tmp/box/", 9) == 0);
    char *dir = ecs_os_strdup(app->process_dir);
    struct stat st;
    test_int(stat(dir, &st), 0);
    test_assert(S_ISDIR(st.st_mode));
    ecs_fini(world);
    test_assert(stat(dir, &st) != 0);
    ecs_os_free(dir);
}

void App_cpu_budget_slows_loop(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    App *app = ecs_singleton_ensure(world, App);
    app->frames = 200;
    app->cpu_budget = 0.1;
    ecs_time_t start;
    ecs_os_get_time(&start);
    test_int(box_run(world), 0);
    double budgeted = ecs_time_measure(&start);
    ecs_fini(world);

    world = ecs_init();
    ECS_IMPORT(world, BoxCore);
    app = ecs_singleton_ensure(world, App);
    app->frames = 200;
    ecs_os_get_time(&start);
    test_int(box_run(world), 0);
    double uncapped = ecs_time_measure(&start);
    ecs_fini(world);

    test_assert(budgeted > uncapped * 2);
}
