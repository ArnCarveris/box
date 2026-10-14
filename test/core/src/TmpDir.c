/**
 * @file TmpDir.c
 * @brief Tests for the temporary folder of a headless process.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

void TmpDir_headless_creates_folder(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .headless = true, .frames = 1, .cpu_budget = 100 });

    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    char *expect = flecs_asprintf("/tmp/box/%d", (int)getpid());
    test_str(status->tmp_dir, expect);

    struct stat st;
    test_int(stat(expect, &st), 0);
    test_assert(S_ISDIR(st.st_mode));

    ecs_os_free(expect);
    ecs_fini(world);
}

void TmpDir_not_headless_no_folder(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .frames = 1, .cpu_budget = 100 });

    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    test_assert(status->tmp_dir == NULL);

    char *path = flecs_asprintf("/tmp/box/%d", (int)getpid());
    struct stat st;
    test_assert(stat(path, &st) != 0);

    ecs_os_free(path);
    ecs_fini(world);
}

void TmpDir_parse_headless_creates_folder(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--headless", "--frames", "1"};
    test_int(box_app_parse(world, 4, argv), 0);

    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    test_assert(status->tmp_dir != NULL);

    struct stat st;
    test_int(stat(status->tmp_dir, &st), 0);
    test_assert(S_ISDIR(st.st_mode));

    ecs_fini(world);
}

void TmpDir_removed_at_fini(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .headless = true, .frames = 1, .cpu_budget = 100 });

    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    char *dir = ecs_os_strdup(status->tmp_dir);
    char *sub = flecs_asprintf("%s/session", dir);
    test_int(mkdir(sub, 0777), 0);
    char *file = flecs_asprintf("%s/capture.png", sub);
    FILE *f = fopen(file, "w");
    test_assert(f != NULL);
    fprintf(f, "data");
    fclose(f);

    ecs_fini(world);

    struct stat st;
    test_assert(stat(dir, &st) != 0);

    ecs_os_free(file);
    ecs_os_free(sub);
    ecs_os_free(dir);
}
