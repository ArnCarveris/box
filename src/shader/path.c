/**
 * @file shader/path.c
 * @brief Resolves shader paths against the search path of the engine.
 *
 * Entries: the path as written, `<BAKE_HOME>/etc/box`, the nearest ancestor with
 * `etc/shaders`, `apps/box_app` at or above it, and the executable directory. On
 * wasm the files are embedded under `etc`, which the first entry finds.
 */

#include "shader_internal.h"
#include <stdio.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/** Project folder the applications of the engine ship their assets in */
#define BOX_SHADER_APP "apps/box_app"

/** Folder of a project the engine reads assets from, also the path prefix */
#define BOX_SHADER_ETC "etc"

/** Folder the installed framework assets sit in, under the bake environment */
#define BOX_SHADER_INSTALL "etc/box"

/** Bake environment the framework assets are installed in, without a trailing slash */
static char box_shader_bake_dir[BOX_SHADER_PATH_MAX];

/** Source tree entry, the ancestor of the working directory with an etc/shaders */
static char box_shader_source_dir[BOX_SHADER_PATH_MAX];

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

/** Find the bake environment the framework assets are installed in.
 *
 * @param out Receives the environment root, empty when there is none
 * @param size Bytes of the output buffer
 */
static void box_shader_find_bake(
    char *out,
    size_t size)
{
    const char *home = getenv("BAKE_HOME");
    out[0] = '\0';
    if (home && home[0]) {
        snprintf(out, size, "%s", home);
        goto done;
    }

    home = getenv("HOME");
    if (home && home[0]) {
        snprintf(out, size, "%s/bake3", home);
    }

done:
    return;
}

/** Find the source tree and the app folder at or above the working directory.
 *
 * @param src Receives the folder with an etc/shaders, empty when there is none
 * @param app Receives the app folder, empty when there is none
 * @param size Bytes of each output buffer
 */
static void box_shader_find_source(
    char *src,
    char *app,
    size_t size)
{
    char dir[BOX_SHADER_PATH_MAX];
    char probe[BOX_SHADER_PATH_MAX];
    src[0] = '\0';
    app[0] = '\0';
    if (!getcwd(dir, sizeof(dir))) {
        goto done;
    }

    do {
        snprintf(probe, sizeof(probe), "%s/%s/shaders", dir, BOX_SHADER_ETC);
        if (!src[0] && !access(probe, R_OK)) {
            snprintf(src, size, "%s", dir);
        }

        snprintf(probe, sizeof(probe), "%s/%s/%s", dir, BOX_SHADER_APP,
            BOX_SHADER_ETC);
        if (!app[0] && !access(probe, R_OK)) {
            snprintf(app, size, "%s/%s", dir, BOX_SHADER_APP);
        }
    } while ((!src[0] || !app[0]) && box_shader_parent(dir));

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

/** Try one entry of the search path.
 *
 * @param dir The entry, skipped when it is empty
 * @param path The path as written, already known to be relative
 * @param out Receives the candidate path
 * @param size Bytes of the output buffer
 * @return true when the entry holds the file
 */
static bool box_shader_try(
    const char *dir,
    const char *path,
    char *out,
    size_t size)
{
    bool result = false;
    if (!dir[0]) {
        goto done;
    }

    snprintf(out, size, "%s/%s", dir, path);
    result = box_shader_readable(out);
done:
    return result;
}

/** Try the framework assets bake3 installed in the environment.
 *
 * A path names the `etc` folder of a project, so `etc/shaders/mesh.wgsl` is
 * `<BAKE_HOME>/etc/box/shaders/mesh.wgsl` in the installed copy.
 *
 * @param path The path as written, already known to be relative
 * @param out Receives the candidate path
 * @param size Bytes of the output buffer
 * @return true when the installed copy holds the file
 */
static bool box_shader_try_bake(
    const char *path,
    char *out,
    size_t size)
{
    bool result = false;
    size_t prefix = strlen(BOX_SHADER_ETC);
    if (strncmp(path, BOX_SHADER_ETC "/", prefix + 1)) {
        goto done;
    }

    char dir[BOX_SHADER_PATH_MAX];
    if (!box_shader_bake_dir[0]) {
        goto done;
    }

    snprintf(dir, sizeof(dir), "%s/%s", box_shader_bake_dir,
        BOX_SHADER_INSTALL);
    result = box_shader_try(dir, path + prefix + 1, out, size);
done:
    return result;
}

void box_shader_path_init(void)
{
    box_shader_paths_ready = true;
    box_shader_find_bake(box_shader_bake_dir, sizeof(box_shader_bake_dir));
    box_shader_find_source(box_shader_source_dir, box_shader_app_dir,
        sizeof(box_shader_source_dir));
    box_shader_find_exe(box_shader_exe_dir, sizeof(box_shader_exe_dir));
}

bool box_locate(
    const char *path,
    char *out,
    size_t size)
{
    bool result = false;
    if (!path || !path[0]) {
        goto done;
    }

    if (!box_shader_paths_ready) {
        box_shader_path_init();
    }

    snprintf(out, size, "%s", path);
    result = box_shader_readable(out);
    if (result || path[0] == '/') {
        goto done;
    }

    result = box_shader_try_bake(path, out, size);
    if (!result) {
        result = box_shader_try(box_shader_source_dir, path, out, size);
    }

    if (!result) {
        result = box_shader_try(box_shader_app_dir, path, out, size);
    }

    if (!result) {
        result = box_shader_try(box_shader_exe_dir, path, out, size);
    }

    if (!result) {
        snprintf(out, size, "%s", path);
    }

done:
    return result;
}

void* box_shader_read_bytes(
    const char *path,
    size_t *size)
{
    void *result = NULL;
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (bytes <= 0) {
        fclose(f);
        goto done;
    }

    result = ecs_os_malloc((ecs_size_t)bytes);
    *size = fread(result, 1, (size_t)bytes, f);
    fclose(f);
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
