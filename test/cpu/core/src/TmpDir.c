/**
 * @file TmpDir.c
 * @brief Tests for the temporary folder of a headless process.
 */

#include <box.h>
#include <app.h>
#include <bake_test.h>
#include <stdio.h>
#include "../../../../src/posix.h"
#include <sys/stat.h>

/** Why a case is skipped on Windows: its folder has no POSIX modes, owners or root checks */
#define BOX_TEST_POSIX_ONLY ("11 Nov 2026, POSIX modes and owners only")

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

void TmpDir_folder_private(void) {
    // hardening/tmp_dir
#ifdef _WIN32
    test_quarantine(BOX_TEST_POSIX_ONLY);
#else
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .headless = true, .frames = 1, .cpu_budget = 100 });

    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    test_assert(status->tmp_dir != NULL);

    struct stat st;
    test_int(lstat(status->tmp_dir, &st), 0);
    test_assert(S_ISDIR(st.st_mode));
    test_int(st.st_mode & 07777, 0700);
    test_int(st.st_uid, geteuid());

    ecs_fini(world);
#endif
}

void TmpDir_symlink_inside_not_followed(void) {
    // hardening/tmp_dir
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *target = flecs_asprintf("/tmp/box_core_outside_%d", (int)getpid());
    char *marker = flecs_asprintf("%s/marker", target);
    test_int(mkdir(target, 0700), 0);
    FILE *f = fopen(marker, "w");
    test_assert(f != NULL);
    fclose(f);

    ecs_singleton_set(world, App, { .headless = true, .frames = 1, .cpu_budget = 100 });

    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    test_assert(status->tmp_dir != NULL);
    char *dir = ecs_os_strdup(status->tmp_dir);
    char *link = flecs_asprintf("%s/link", dir);
    test_int(symlink(target, link), 0);

    ecs_fini(world);

    struct stat st;
    test_assert(lstat(dir, &st) != 0);
    test_int(stat(marker, &st), 0);

    unlink(marker);
    rmdir(target);
    ecs_os_free(link);
    ecs_os_free(dir);
    ecs_os_free(marker);
    ecs_os_free(target);
}

void TmpDir_refused_folder_fails_start(void) {
    // hardening/tmp_dir
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/box/%d", (int)getpid());
    char *target = flecs_asprintf("/tmp/box_core_start_%d", (int)getpid());
    char *scene = flecs_asprintf("/tmp/box_core_start_%d.flecs", (int)getpid());
    FILE *f = fopen(scene, "w");
    test_assert(f != NULL);
    fprintf(f, "Loaded {}\n");
    fclose(f);
    mkdir("/tmp/box", 0700);
    test_int(mkdir(target, 0700), 0);
    test_int(symlink(target, path), 0);

    ecs_log_set_level(-4);
    char *argv[] = {"boxrun", "--headless", "--frames", "1", scene};
    test_assert(box_app_parse(world, 5, argv) != 0 || box_app_run(world) != 0);

    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);
    test_assert(ecs_lookup(world, "Loaded") == 0);

    ecs_fini(world);

    struct stat st;
    test_int(lstat(path, &st), 0);
    test_assert(S_ISLNK(st.st_mode));

    unlink(path);
    rmdir(target);
    unlink(scene);
    ecs_os_free(scene);
    ecs_os_free(target);
    ecs_os_free(path);
}

void TmpDir_worlds_share_folder(void) {
    // hardening/tmp_dir
    ecs_world_t *first = ecs_init();
    ECS_IMPORT(first, Box);
    ecs_world_t *second = ecs_init();
    ECS_IMPORT(second, Box);

    ecs_singleton_set(first, App, { .headless = true, .frames = 1, .cpu_budget = 100 });
    ecs_singleton_set(second, App, { .headless = true, .frames = 1, .cpu_budget = 100 });

    char *dir = ecs_os_strdup(ecs_singleton_get(first, AppStatus)->tmp_dir);
    test_assert(dir != NULL);
    test_str(ecs_singleton_get(second, AppStatus)->tmp_dir, dir);

    char *file = flecs_asprintf("%s/kept", dir);
    FILE *f = fopen(file, "w");
    test_assert(f != NULL);
    fclose(f);

    ecs_fini(first);

    struct stat st;
    test_int(stat(dir, &st), 0);
    test_int(stat(file, &st), 0);
    test_str(ecs_singleton_get(second, AppStatus)->tmp_dir, dir);

    ecs_fini(second);

    test_assert(lstat(dir, &st) != 0);

    ecs_os_free(file);
    ecs_os_free(dir);
}

void TmpDir_root_of_user_accepted(void) {
    // hardening/tmp_dir
#ifdef _WIN32
    test_quarantine(BOX_TEST_POSIX_ONLY);
#else
    test_assert(box_app_tmp_root_problem(S_IFDIR | 0700, 501, 501) == NULL);
    test_assert(box_app_tmp_root_problem(S_IFDIR | 01777, 501, 501) == NULL);
#endif
}

void TmpDir_root_of_root_accepted(void) {
    // hardening/tmp_dir
#ifdef _WIN32
    test_quarantine(BOX_TEST_POSIX_ONLY);
#else
    test_assert(box_app_tmp_root_problem(S_IFDIR | 01777, 0, 501) == NULL);
    test_assert(box_app_tmp_root_problem(S_IFDIR | 0755, 0, 501) == NULL);
#endif
}

void TmpDir_root_of_other_user_refused(void) {
    // hardening/tmp_dir
#ifdef _WIN32
    test_quarantine(BOX_TEST_POSIX_ONLY);
#else
    test_assert(box_app_tmp_root_problem(S_IFDIR | 01777, 502, 501) != NULL);
    test_assert(box_app_tmp_root_problem(S_IFDIR | 0700, 502, 501) != NULL);
#endif
}

void TmpDir_root_writable_without_sticky_refused(void) {
    // hardening/tmp_dir
#ifdef _WIN32
    test_quarantine(BOX_TEST_POSIX_ONLY);
#else
    test_assert(box_app_tmp_root_problem(S_IFDIR | 0777, 0, 501) != NULL);
    test_assert(box_app_tmp_root_problem(S_IFDIR | 0770, 501, 501) != NULL);
#endif
}

void TmpDir_root_not_a_folder_refused(void) {
    // hardening/tmp_dir
#ifdef _WIN32
    test_quarantine(BOX_TEST_POSIX_ONLY);
#else
    test_assert(box_app_tmp_root_problem(S_IFLNK | 0777, 501, 501) != NULL);
    test_assert(box_app_tmp_root_problem(S_IFREG | 0600, 501, 501) != NULL);
#endif
}
