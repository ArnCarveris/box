/**
 * @file core/process.c
 * @brief Per-process temporary folder under /tmp/box/<pid>.
 */

#include "core_internal.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/** Remove a directory tree.
 *
 * @param path The directory
 */
static void box_rmtree(
    const char *path)
{
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }

            char *child = flecs_asprintf("%s/%s", path, entry->d_name);
            struct stat st;
            if (!lstat(child, &st) && S_ISDIR(st.st_mode)) {
                box_rmtree(child);
            } else {
                unlink(child);
            }

            ecs_os_free(child);
        }

        closedir(dir);
    }

    rmdir(path);
}

/** World fini callback that removes the process folder.
 *
 * @param world The world
 * @param ctx The folder path
 */
static void box_process_cleanup(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    box_rmtree(ctx);
    ecs_os_free(ctx);
}

void box_process_init(
    ecs_world_t *world)
{
    mkdir("/tmp/box", 0777);
    char *dir = flecs_asprintf("/tmp/box/%d", (int)getpid());
    if (mkdir(dir, 0777) && errno != EEXIST) {
        ecs_warn("failed to create process folder %s", dir);
    }

    App *app = ecs_singleton_ensure(world, App);
    ecs_os_free(app->process_dir);
    app->process_dir = ecs_os_strdup(dir);
    ecs_atfini(world, box_process_cleanup, dir);
}
