/**
 * @file app/tmp.c
 * @brief The temporary folder of a headless process under /tmp/box/<pid>.
 */

#include "app.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/** Remove a folder and everything in it.
 *
 * @param path The folder
 */
static void box_app_rmtree(
    const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (!dir) {
        goto done;
    }

    while ((entry = readdir(dir))) {
        struct stat st;
        char *child;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        child = flecs_asprintf("%s/%s", path, entry->d_name);
        if (!lstat(child, &st) && S_ISDIR(st.st_mode)) {
            box_app_rmtree(child);
        } else {
            unlink(child);
        }

        ecs_os_free(child);
    }

    closedir(dir);
    rmdir(path);
done:
    return;
}

/** Remove the temporary folder when the world is deleted.
 *
 * @param world The world
 * @param ctx The folder path, which this frees
 */
static void box_app_tmp_fini(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    box_app_rmtree(ctx);
    ecs_os_free(ctx);
}

char* box_app_tmp_create(
    ecs_world_t *world)
{
    char *result = flecs_asprintf("%s/%d", BOX_TMP_ROOT, (int)getpid());
    if (mkdir(BOX_TMP_ROOT, 0777) && errno != EEXIST) {
        ecs_err("cannot create %s: %s", BOX_TMP_ROOT, strerror(errno));
        goto error;
    }

    if (mkdir(result, 0777) && errno != EEXIST) {
        ecs_err("cannot create %s: %s", result, strerror(errno));
        goto error;
    }

    ecs_atfini(world, box_app_tmp_fini, ecs_os_strdup(result));

    return result;
error:
    ecs_os_free(result);

    return NULL;
}
