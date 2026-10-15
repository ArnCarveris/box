/**
 * @file app/tmp.c
 * @brief The private temporary folder of a process under /tmp/box/<pid>.
 */

#include "app.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/** Deepest folder level box_app_rmtree descends into */
#define BOX_APP_RMTREE_DEPTH (64)

/** Flags that open a folder without following a symbolic link */
#define BOX_APP_DIR_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)

/** The temporary folder of the process, shared by the worlds of the process that use it */
typedef struct box_app_tmp_t {
    char *path;         /**< The folder, NULL when the process has none */
    dev_t dev;          /**< Device of the folder this process created */
    ino_t ino;          /**< Inode of the folder this process created */
    int32_t refs;       /**< Worlds that use the folder, removed when the last one is deleted */
} box_app_tmp_t;

/** The temporary folder of the process */
static box_app_tmp_t box_app_tmp;

/** Remove everything in an open folder without following symbolic links.
 *
 * @param fd The folder, which this closes
 * @param depth Folder levels above this one
 */
static void box_app_rmtree_fd(
    int fd,
    int32_t depth)
{
    DIR *dir = fdopendir(fd);
    struct dirent *entry;
    int32_t removed;
    if (!dir) {
        close(fd);
        goto done;
    }

    do {
        removed = 0;
        rewinddir(dir);
        while ((entry = readdir(dir))) {
            struct stat st, child_st;
            int child;
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }

            if (fstatat(dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
                continue;
            }

            if (!S_ISDIR(st.st_mode)) {
                removed += !unlinkat(dirfd(dir), entry->d_name, 0);
                continue;
            }

            child = depth < BOX_APP_RMTREE_DEPTH ?
                openat(dirfd(dir), entry->d_name, BOX_APP_DIR_FLAGS) : -1;
            if (child >= 0 && !fstat(child, &child_st) &&
                child_st.st_dev == st.st_dev && child_st.st_ino == st.st_ino)
            {
                box_app_rmtree_fd(child, depth + 1);
            } else if (child >= 0) {
                close(child);
            }

            removed += !unlinkat(dirfd(dir), entry->d_name, AT_REMOVEDIR);
        }
    } while (removed);

    closedir(dir);
done:
    return;
}

/** Remove a folder and everything in it when it is still the folder that was identified.
 *
 * @param path The folder
 * @param dev Device of the folder, ignored when ino is 0
 * @param ino Inode of the folder, 0 to accept any folder of the current user
 */
static void box_app_rmtree_id(
    const char *path,
    dev_t dev,
    ino_t ino)
{
    int fd = open(path, BOX_APP_DIR_FLAGS);
    struct stat st;
    if (fd < 0) {
        goto done;
    }

    if (fstat(fd, &st) || st.st_uid != geteuid() || (ino && (st.st_dev != dev ||
        st.st_ino != ino)))
    {
        ecs_warn("not removing %s: it is not the folder this process created", path);
        close(fd);
        goto done;
    }

    dev = st.st_dev;
    ino = st.st_ino;
    box_app_rmtree_fd(fd, 0);
    if (!lstat(path, &st) && S_ISDIR(st.st_mode) && st.st_dev == dev && st.st_ino == ino) {
        rmdir(path);
    }

done:
    return;
}

void box_app_rmtree(
    const char *path)
{
    box_app_rmtree_id(path, 0, 0);
}

const char* box_app_tmp_root_problem(
    mode_t mode,
    uid_t owner,
    uid_t user)
{
    const char *result = NULL;
    if (!S_ISDIR(mode)) {
        result = "it is not a folder";
    } else if (owner != user && owner != 0) {
        result = "another user owns it and could replace the folder of this process";
    } else if ((mode & (S_IWGRP | S_IWOTH)) && !(mode & S_ISVTX)) {
        result = "other users can write it and it has no sticky bit";
    }

    return result;
}

/** Create the root of the temporary folders unless it exists, and check that no other user
 * can rename, replace or delete the folder of this process in it.
 *
 * @return 0 when the root may hold the folder, -1 when it may not
 */
static int box_app_tmp_root(void)
{
    struct stat st;
    const char *problem;
    int result = -1;
    if (mkdir(BOX_TMP_ROOT, 0700) && errno != EEXIST) {
        ecs_err("cannot create %s: %s", BOX_TMP_ROOT, strerror(errno));
        goto done;
    }

    if (lstat(BOX_TMP_ROOT, &st)) {
        ecs_err("cannot use %s: %s", BOX_TMP_ROOT, strerror(errno));
        goto done;
    }

    problem = box_app_tmp_root_problem(st.st_mode, st.st_uid, geteuid());
    if (problem) {
        ecs_err("cannot use %s: %s; remove it or let root own it with mode 1777",
            BOX_TMP_ROOT, problem);
        goto done;
    }

    result = 0;
done:
    return result;
}

/** Remove the folder that an earlier process with the same pid left behind when it crashed.
 * The pid now belongs to this process, so that process has exited: a real folder of the
 * current user at the path is stale. Anything else at the path is kept.
 *
 * @param path The folder
 * @return 0 when a stale folder was removed, -1 when the path holds something else
 */
static int box_app_tmp_reclaim(
    const char *path)
{
    struct stat st;
    int result = -1;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
        goto done;
    }

    ecs_trace("removing %s, left behind by an earlier process with this pid", path);
    box_app_rmtree_id(path, st.st_dev, st.st_ino);
    result = 0;
done:
    return result;
}

/** Create the folder of the process, after removing a stale folder of the current user.
 *
 * @param path The folder
 * @return 0 when the folder was created and is private to the current user, -1 otherwise
 */
static int box_app_tmp_mkdir(
    const char *path)
{
    struct stat st;
    int fd = -1, result = -1;
    if (mkdir(path, 0700) && (errno != EEXIST || box_app_tmp_reclaim(path) ||
        mkdir(path, 0700)))
    {
        ecs_err("cannot create %s: %s", path, errno == EEXIST ?
            "it exists and is not a stale folder of the current user" :
            strerror(errno));
        goto done;
    }

    fd = open(path, BOX_APP_DIR_FLAGS);
    if (fd < 0 || fstat(fd, &st) || st.st_uid != geteuid() || fchmod(fd, 0700)) {
        ecs_err("cannot use %s: it is not a folder of the current user", path);
        goto done;
    }

    box_app_tmp.dev = st.st_dev;
    box_app_tmp.ino = st.st_ino;
    result = 0;
done:
    if (fd >= 0) {
        close(fd);
    }

    return result;
}

/** Check that the folder of the process is still the folder it created.
 *
 * @param path The folder
 * @return Whether the folder is the one the process created
 */
static bool box_app_tmp_same(
    const char *path)
{
    struct stat st;
    bool result = box_app_tmp.path && !strcmp(box_app_tmp.path, path) && !lstat(path, &st) &&
        S_ISDIR(st.st_mode) && st.st_dev == box_app_tmp.dev && st.st_ino == box_app_tmp.ino;

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

    box_app_rmtree_id(box_app_tmp.path, box_app_tmp.dev, box_app_tmp.ino);
    ecs_os_free(box_app_tmp.path);
    ecs_os_zeromem(&box_app_tmp);
done:
    return;
}

char* box_app_tmp_open(
    ecs_world_t *world)
{
    char *result = flecs_asprintf("%s/%d", BOX_TMP_ROOT, (int)getpid());
    if (box_app_tmp.refs && box_app_tmp_same(result)) {
        box_app_tmp.refs ++;
        goto done;
    }

    if (box_app_tmp.refs) {
        ecs_err("cannot use %s: it is not the folder this process created", result);
        goto error;
    }

    if (box_app_tmp_root() || box_app_tmp_mkdir(result)) {
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
