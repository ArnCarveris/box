/**
 * @file command/endpoint.c
 * @brief The terminal and resource endpoints served by the command server.
 */

#include "command_internal.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/** Size of the buffer that holds a generated session name */
#define BOX_HTTP_NAME_MAX (32)

/** Path prefix of the resource endpoint */
#define BOX_HTTP_RESOURCE "resource/"

/** Fill a reply with a status code and a text body.
 *
 * @param reply The reply
 * @param code The HTTP status code
 * @param body The body, NULL for an empty body
 */
static void box_http_reply(
    ecs_http_reply_t *reply,
    int code,
    const char *body)
{
    reply->code = code;
    reply->content_type = "text/plain";
    if (code == 400) {
        reply->status = "Bad Request";
    } else if (code == 403) {
        reply->status = "Forbidden";
    } else if (code == 404) {
        reply->status = "Not Found";
    }

    if (body) {
        ecs_strbuf_appendstr(&reply->body, body);
    }
}

/** Get the session entity named by the session query parameter.
 *
 * @param world The world
 * @param req The request
 * @return The session entity, 0 when the parameter is missing or unknown
 */
static ecs_entity_t box_http_session(
    ecs_world_t *world,
    const ecs_http_request_t *req)
{
    ecs_entity_t result = 0;
    const char *name = ecs_http_get_param(req, "session");
    if (!name) {
        goto done;
    }

    ecs_entity_t root = ecs_lookup(world, "terminals");
    ecs_assert(root != 0, ECS_INVALID_OPERATION, "no terminals scope");
    result = ecs_lookup_child(world, root, name);
    if (result && !ecs_has(world, result, Terminal)) {
        result = 0;
    }

done:
    return result;
}

/** Create a session with a unique name and reply with that name.
 *
 * @param world The world
 * @param reply The reply
 */
static void box_http_new(
    ecs_world_t *world,
    ecs_http_reply_t *reply)
{
    HttpEndpoint *ep = box_http_endpoint(world);
    ecs_assert(ep != NULL, ECS_INVALID_OPERATION, "no http endpoint");

    ecs_entity_t root = ecs_lookup(world, "terminals");
    ecs_assert(root != 0, ECS_INVALID_OPERATION, "no terminals scope");

    char name[BOX_HTTP_NAME_MAX];
    do {
        ep->next ++;
        ecs_os_snprintf(name, BOX_HTTP_NAME_MAX, "s%d", ep->next);
    } while (ecs_lookup_child(world, root, name));

    ecs_entity_t session = ecs_entity(world, { .name = name, .parent = root });
    ecs_add(world, session, Terminal);
    box_http_reply(reply, 200, name);
}

/** Delete the session of the request.
 *
 * @param world The world
 * @param req The request
 * @param reply The reply
 */
static void box_http_close(
    ecs_world_t *world,
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply)
{
    ecs_entity_t session = box_http_session(world, req);
    if (!session) {
        box_http_reply(reply, 404, "unknown session");
    } else {
        ecs_delete(world, session);
        box_http_reply(reply, 200, "closed");
    }
}

/** Run the command line of the request on its session.
 *
 * @param world The world
 * @param req The request
 * @param reply The reply
 */
static void box_http_exec(
    ecs_world_t *world,
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply)
{
    ecs_entity_t session = box_http_session(world, req);
    if (!session) {
        box_http_reply(reply, 404, "unknown session");
        goto done;
    }

    const char *cmd = ecs_http_get_param(req, "cmd");
    if (req->method == EcsHttpPost && req->body && req->body[0]) {
        cmd = req->body;
    }

    if (!cmd || !cmd[0]) {
        box_http_reply(reply, 400, "missing cmd parameter");
        goto done;
    }

    bool error = false;
    char *out = box_command_run(world, session, cmd, &error);
    box_http_reply(reply, error ? 400 : 200, out);
    ecs_os_free(out);
done:
    return;
}

/** Test whether a file resolves inside a folder.
 *
 * @param dir The folder
 * @param path The file path
 * @return true when the file is inside the folder
 */
static bool box_http_inside(
    const char *dir,
    const char *path)
{
    char base[PATH_MAX], full[PATH_MAX];
    bool result = false;
    if (realpath(dir, base) && realpath(path, full)) {
        size_t len = strlen(base);
        result = !strncmp(full, base, len) && full[len] == '/';
    }

    return result;
}

/** Append the contents of a file to the reply body.
 *
 * @param path The file
 * @param reply The reply
 * @return 0 on success, -1 when the file could not be read
 */
static int box_http_file(
    const char *path,
    ecs_http_reply_t *reply)
{
    int result = -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size >= 0) {
        char *data = ecs_os_malloc((ecs_size_t)size + 1);
        size_t read = fread(data, 1, (size_t)size, f);
        ecs_strbuf_appendstrn(&reply->body, data, (int32_t)read);
        ecs_os_free(data);
        result = 0;
    }

    fclose(f);
done:
    return result;
}

/** Stream a file from the resources folder of the session.
 *
 * @param world The world
 * @param req The request
 * @param reply The reply
 */
static void box_http_resource(
    ecs_world_t *world,
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply)
{
    ecs_entity_t session = box_http_session(world, req);
    if (!session) {
        box_http_reply(reply, 404, "unknown session");
        goto done;
    }

    const char *rel = req->path + ecs_os_strlen(BOX_HTTP_RESOURCE);
    if (!rel[0]) {
        box_http_reply(reply, 404, "no resource requested");
        goto done;
    }

    if (rel[0] == '/' || strstr(rel, "..")) {
        box_http_reply(reply, 403, "resource outside the session folder");
        goto done;
    }

    const Terminal *t = ecs_get(world, session, Terminal);
    ecs_assert(t != NULL, ECS_INVALID_OPERATION, "session without terminal");
    ecs_assert(t->session_dir != NULL, ECS_INVALID_OPERATION, "session without folder");

    char *dir = flecs_asprintf("%s/resources", t->session_dir);
    char *path = flecs_asprintf("%s/%s", dir, rel);
    const char *ext = strrchr(rel, '.');
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode)) {
        box_http_reply(reply, 404, "resource not found");
    } else if (!box_http_inside(dir, path)) {
        box_http_reply(reply, 403, "resource outside the session folder");
    } else if (box_http_file(path, reply)) {
        box_http_reply(reply, 404, "resource not found");
    } else {
        reply->code = 200;
        reply->content_type = ext && !strcmp(ext, ".png") ? "image/png" : "text/plain";
    }

    ecs_os_free(path);
    ecs_os_free(dir);
done:
    return;
}

bool box_http_endpoint_request(
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply,
    void *ctx)
{
    ecs_world_t *world = ctx;
    const char *path = req->path;
    if (!strcmp(path, "terminal/new")) {
        box_http_new(world, reply);
    } else if (!strcmp(path, "terminal/close")) {
        box_http_close(world, req, reply);
    } else if (!strcmp(path, "terminal/exec")) {
        box_http_exec(world, req, reply);
    } else if (!strncmp(path, BOX_HTTP_RESOURCE, ecs_os_strlen(BOX_HTTP_RESOURCE))) {
        box_http_resource(world, req, reply);
    } else {
        box_http_reply(reply, 404, "unknown endpoint");
    }

    return true;
}
