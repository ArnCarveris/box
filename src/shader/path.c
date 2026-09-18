/**
 * @file shader/path.c
 * @brief Resolves shader paths against the search path of the engine.
 *
 * The entries are the working directory, `apps/box_app` under the working
 * directory or the nearest ancestor of it that has one, and the directory of
 * the executable. The first two make a checkout work from the repository root
 * and from a test project, the last one makes an installed game work from any
 * working directory. On wasm the files are embedded under `etc`, which the
 * working directory entry finds.
 */

#include "shader_internal.h"
#include <stdio.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/** Project folder the engine ships its shaders in */
#define BOX_SHADER_APP "apps/box_app"

/** The `apps/box_app` entry of the search path, empty when there is none */
static char box_shader_app_dir[BOX_SHADER_PATH_MAX];

/** The executable entry of the search path, empty when it is unknown */
static char box_shader_exe_dir[BOX_SHADER_PATH_MAX];

/** The search path was built */
static bool box_shader_paths_ready;

/** Check whether a path names a readable file.
 *
 * @param path The path
 * @return true when the file can be opened for reading
 */
static bool box_shader_readable(
    const char *path)
{
    FILE *f = fopen(path, "rb");
    bool result = f != NULL;
    if (f) {
        fclose(f);
    }

    return result;
}

/** Cut the last component off a directory path.
 *
 * @param dir The path, shortened in place
 * @return true when a component was removed
 */
static bool box_shader_parent(
    char *dir)
{
    char *slash = strrchr(dir, '/');
    bool result = slash != NULL && slash != dir;
    if (result) {
        *slash = '\0';
    }

    return result;
}

/** Find the `apps/box_app` folder at or above the working directory.
 *
 * @param out Receives the folder, empty when there is none
 * @param size Bytes of the output buffer
 */
static void box_shader_find_app(
    char *out,
    size_t size)
{
    char dir[BOX_SHADER_PATH_MAX];
    char probe[BOX_SHADER_PATH_MAX];
    out[0] = '\0';
    if (!getcwd(dir, sizeof(dir))) {
        goto done;
    }

    do {
        snprintf(probe, sizeof(probe), "%s/%s/etc/shaders", dir, BOX_SHADER_APP);
        if (!access(probe, R_OK)) {
            snprintf(out, size, "%s/%s", dir, BOX_SHADER_APP);
            goto done;
        }
    } while (box_shader_parent(dir));

done:
    return;
}

/** Find the folder the running executable sits in.
 *
 * @param out Receives the folder, empty when it is unknown
 * @param size Bytes of the output buffer
 */
static void box_shader_find_exe(
    char *out,
    size_t size)
{
    out[0] = '\0';
#ifdef __APPLE__
    uint32_t len = (uint32_t)size;
    if (!_NSGetExecutablePath(out, &len)) {
        box_shader_parent(out);
    } else {
        out[0] = '\0';
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", out, size - 1);
    if (len > 0) {
        out[len] = '\0';
        box_shader_parent(out);
    } else {
        out[0] = '\0';
    }
#else
    (void)size;
#endif
}

/** Build the search path once.
 */
static void box_shader_paths(void)
{
    if (box_shader_paths_ready) {
        goto done;
    }

    box_shader_paths_ready = true;
    box_shader_find_app(box_shader_app_dir, sizeof(box_shader_app_dir));
    box_shader_find_exe(box_shader_exe_dir, sizeof(box_shader_exe_dir));
done:
    return;
}

bool box_shader_resolve(
    const char *path,
    char *out,
    size_t size)
{
    bool result = false;
    if (!path || !path[0]) {
        goto done;
    }

    box_shader_paths();
    snprintf(out, size, "%s", path);
    result = box_shader_readable(out);
    if (result || path[0] == '/') {
        goto done;
    }

    if (box_shader_app_dir[0]) {
        snprintf(out, size, "%s/%s", box_shader_app_dir, path);
        result = box_shader_readable(out);
    }

    if (!result && box_shader_exe_dir[0]) {
        snprintf(out, size, "%s/%s", box_shader_exe_dir, path);
        result = box_shader_readable(out);
    }

    if (!result) {
        snprintf(out, size, "%s", path);
    }

done:
    return result;
}

char* box_shader_read(
    const char *path)
{
    char *result = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        goto done;
    }

    result = ecs_os_malloc((ecs_size_t)size + 1);
    size_t read = fread(result, 1, (size_t)size, f);
    result[read] = '\0';
    fclose(f);
done:
    return result;
}
