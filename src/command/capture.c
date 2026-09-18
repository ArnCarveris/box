/**
 * @file command/capture.c
 * @brief The capture command: render the active or named cameras to PNG files.
 */

#include "command_internal.h"
#include "../render/render_internal.h"
#include <string.h>

/** Resolve the comma separated camera paths of a capture command line.
 *
 * Without an argument the list holds the active camera, the camera that the
 * extract system renders.
 *
 * @param world The world
 * @param session The session entity
 * @param arg The camera paths, NULL for the active camera
 * @param out Vector receiving the camera entities
 * @return NULL on success, a one line error the caller frees
 */
static char* box_capture_cameras(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *arg,
    ecs_vec_t *out)
{
    char *result = NULL;
    char *copy = (arg && arg[0]) ? ecs_os_strdup(arg) : NULL;
    char *token = copy;
    do {
        char *next = NULL;
        if (token) {
            next = strchr(token, ',');
            if (next) {
                next[0] = '\0';
                next ++;
            }
        }

        ecs_entity_t camera = 0;
        char *error = box_capture_camera(world, session, token, &camera);
        if (error) {
            result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "%s", error);
            ecs_os_free(error);
            break;
        }

        ecs_vec_append_t(NULL, out, ecs_entity_t)[0] = camera;
        token = next;
    } while (token);

    ecs_os_free(copy);

    return result;
}

/** The capture command: request a PNG of the active or of the named cameras.
 *
 * The path of a request is handed to the Capture component, which owns it until
 * the extract system consumes the request.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_capture_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *arg = *(char**)argv[1].ptr;
    const TerminalSession *s = ecs_get(world, session, TerminalSession);
    ecs_assert(s != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");
    const App *app = ecs_singleton_get(world, App);
    ecs_assert(app != NULL, ECS_INVALID_OPERATION, "import box.core first");

    ecs_vec_t cameras;
    ecs_vec_init_t(NULL, &cameras, ecs_entity_t, 0);
    char *out = box_capture_cameras(world, session, arg, &cameras);
    if (out) {
        goto done;
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, count = ecs_vec_count(&cameras);
    for (i = 0; i < count; i ++) {
        ecs_entity_t camera = ecs_vec_get_t(&cameras, ecs_entity_t, i)[0];
        char *file = flecs_asprintf("capture_%d_%d.png", app->frame, i);
        box_capture_request(world, camera, box_capture_path(world, session, file));
        ecs_strbuf_append(&buf, "%s\n", file);
        ecs_os_free(file);
    }

    out = ecs_strbuf_get(&buf);
done:
    ecs_vec_fini_t(NULL, &cameras, ecs_entity_t);
    *(char**)result->ptr = out;
}

char* box_capture_camera(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    ecs_entity_t *out)
{
    char *result = NULL;
    ecs_entity_t camera = 0;
    if (!path || !path[0]) {
        camera = box_render_active_camera(world);
        if (!camera) {
            result = ecs_os_strdup("no active camera");
        }

        goto done;
    }

    ecs_entity_t cwd = box_path_cwd(world, session);
    if (box_path_lookup(world, cwd, path, &camera) || !camera) {
        result = flecs_asprintf("unknown camera '%s'", path);
        camera = 0;
        goto done;
    }

    if (!ecs_has(world, camera, Camera)) {
        result = flecs_asprintf("'%s' is not a camera", path);
        camera = 0;
    }

done:
    out[0] = camera;

    return result;
}

char* box_capture_path(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *file)
{
    const Terminal *t = ecs_get(world, session, Terminal);
    ecs_assert(t != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");
    ecs_assert(t->session_dir != NULL, ECS_INVALID_OPERATION, "session without folder");

    return flecs_asprintf("%s/resources/%s", t->session_dir, file);
}

void box_capture_request(
    ecs_world_t *world,
    ecs_entity_t camera,
    char *path)
{
    Capture *pending = ecs_get_mut(world, camera, Capture);
    if (pending) {
        ecs_os_free(pending->path);
        pending->path = NULL;
    }

    ecs_set(world, camera, Capture, { .path = path });
}

void box_capture_init(
    ecs_world_t *world)
{
    box_command_param_t params[] = {{
        .name = "cameras", .type = ecs_id(ecs_string_t), .optional = true
    }};

    box_command_register(world, "capture",
        "Write a PNG of the active or of the named cameras to the session resources",
        params, 1, box_capture_command);
}
