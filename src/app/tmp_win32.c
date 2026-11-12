/**
 * @file app/tmp_win32.c
 * @brief The private temporary folder of a process under /tmp/box/<pid> on Windows.
 */

#ifdef _WIN32

#include "app.h"
#include "../posix.h"
#include <errno.h>
#include <string.h>

/** The temporary folder of the process, shared by the worlds of the process that use it */
typedef struct box_app_tmp_t {
    char *path;         /**< The folder, NULL when the process has none */
    int32_t refs;       /**< Worlds that use the folder, removed when the last one is deleted */
} box_app_tmp_t;

/** The temporary folder of the process */
static box_app_tmp_t box_app_tmp;

void box_app_rmtree(
    const char *path)
{
    box_posix_rmtree(path, 0);
}

/** Create the folder of the process, after removing a folder that an earlier process with the
 * same pid left behind.
 *
 * @param path The folder
 * @return 0 when the folder was created, -1 otherwise
 */
static int box_app_tmp_mkdir(
    const char *path)
{
    struct stat st;
    int result = -1;
    if (_mkdir("/tmp") && errno != EEXIST) {
        ecs_err("cannot create /tmp: %s", strerror(errno));
        goto done;
    }

    if (_mkdir(BOX_TMP_ROOT) && errno != EEXIST) {
        ecs_err("cannot create %s: %s", BOX_TMP_ROOT, strerror(errno));
        goto done;
    }

    if (_mkdir(path) && errno == EEXIST) {
        if (!lstat(path, &st) && S_ISLNK(st.st_mode)) {
            ecs_err("refusing %s: it is a link, not a folder", path);
            goto done;
        }

        ecs_trace("removing %s, left behind by an earlier process with this pid", path);
        box_app_rmtree(path);
        _mkdir(path);
    }

    if (access(path, 0)) {
        ecs_err("cannot create %s", path);
        goto done;
    }

    result = 0;
done:
    return result;
}

/** Release the temporary folder when a world is deleted, removing it with the last world.
 *
 * @param world The world
 * @param ctx Unused
 */
static void box_app_tmp_fini(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    if (!box_app_tmp.refs || -- box_app_tmp.refs) {
        goto done;
    }

    box_app_rmtree(box_app_tmp.path);
    ecs_os_free(box_app_tmp.path);
    ecs_os_zeromem(&box_app_tmp);
done:
    return;
}

char* box_app_tmp_open(
    ecs_world_t *world)
{
    char *result = flecs_asprintf("%s/%d", BOX_TMP_ROOT, (int)getpid());
    if (box_app_tmp.refs) {
        box_app_tmp.refs ++;
        goto done;
    }

    if (box_app_tmp_mkdir(result)) {
        goto error;
    }

    ecs_os_free(box_app_tmp.path);
    box_app_tmp.path = ecs_os_strdup(result);
    box_app_tmp.refs = 1;
done:
    ecs_atfini(world, box_app_tmp_fini, NULL);

    return result;
error:
    ecs_os_free(result);

    return NULL;
}

char* box_app_tmp_create(
    ecs_world_t *world)
{
    char *result = box_app_tmp_open(world);
    if (!result) {
        ecs_err("headless run stops: it has no temporary folder");
        ecs_quit(world);
    }

    return result;
}

#endif
