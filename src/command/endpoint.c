/**
 * @file command/endpoint.c
 * @brief The HTTP server with the terminal and resource endpoints, other paths go to REST.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "command.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/** Path prefix of the resource endpoint */
#define BOX_ENDPOINT_RESOURCE "resource/"

/** A file extension and the content type it is served with */
typedef struct box_endpoint_mime_t {
    const char *ext;    /**< Extension including the dot */
    const char *type;   /**< Content type */
} box_endpoint_mime_t;

/** Content types of the resource endpoint, application/octet-stream otherwise */
static const box_endpoint_mime_t box_endpoint_mimes[] = {
    { ".png", "image/png" },
    { ".jpg", "image/jpeg" },
    { ".txt", "text/plain" },
    { ".log", "text/plain" },
    { ".flecs", "text/plain" },
    { ".json", "application/json" },
    { NULL, NULL }
};

/** Fill a reply with a status code and a text body.
 *
 * @param reply The reply
 * @param code The HTTP status code
 * @param body The body, NULL for none
 */
static void box_endpoint_reply(
    ecs_http_reply_t *reply,
    int code,
    const char *body)
{
    reply->code = code;
    reply->content_type = "text/plain";
    if (code == 200) {
        reply->status = "OK";
    } else if (code == 400) {
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

/** Get the session of the session query parameter, replying 404 when there is none.
 *
 * @param world The world
 * @param req The request
 * @param reply The reply
 * @return The session entity, 0 when it does not exist
 */
static ecs_entity_t box_endpoint_session(
    ecs_world_t *world,
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply)
{
    ecs_entity_t result = box_terminal_lookup(world, ecs_http_get_param(req, "session"));
    if (!result) {
        box_endpoint_reply(reply, 404, BOX_COMMAND_ERROR "unknown session");
    }

    return result;
}

/** Run the command line of a request in its session.
 *
 * @param world The world
 * @param req The request, with the line in the cmd parameter or the body
 * @param reply The reply
 */
static void box_endpoint_exec(
    ecs_world_t *world,
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply)
{
    ecs_entity_t session = box_endpoint_session(world, req, reply);
    const char *cmd = ecs_http_get_param(req, "cmd");
    bool error = false;
    if (!session) {
        goto done;
    }

    if (!cmd && req->body && req->body[0]) {
        cmd = req->body;
    }

    if (!cmd || !cmd[0]) {
        box_endpoint_reply(reply, 400, BOX_COMMAND_ERROR "missing cmd");
        goto done;
    }

    char *out = box_terminal_run(world, session, cmd, &error);
    box_endpoint_reply(reply, error ? 400 : 200, out);
    ecs_os_free(out);
done:
    return;
}

/** Test whether a file resolves to a location inside a folder.
 *
 * @param dir The folder
 * @param path The file
 * @return Whether the file is inside the folder after resolving links
 */
static bool box_endpoint_inside(
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

/** Test whether a relative resource path stays in its folder before links resolve.
 *
 * @param rel The path relative to the resources folder
 * @return Whether the path is relative and has no .. element
 */
static bool box_endpoint_confined(
    const char *rel)
{
    bool result = rel[0] != '/' && rel[0] != '\\';
    const char *p = rel;
    while (result && (p = strstr(p, ".."))) {
        bool start = p == rel || p[-1] == '/';
        bool end = !p[2] || p[2] == '/';
        result = !(start && end);
        p += 2;
    }

    return result;
}

/** Append the content of a file to a reply.
 *
 * @param path The file
 * @param reply The reply
 * @return 0 on success, -1 when the file could not be read
 */
static int box_endpoint_file(
    const char *path,
    ecs_http_reply_t *reply)
{
    char buffer[4096];
    size_t read;
    int result = -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    while ((read = fread(buffer, 1, sizeof(buffer), f))) {
        ecs_strbuf_appendstrn(&reply->body, buffer, (int32_t)read);
    }

    fclose(f);
    result = 0;
done:
    return result;
}

/** Get the content type of a file.
 *
 * @param path The file
 * @return The content type
 */
static const char* box_endpoint_mime(
    const char *path)
{
    const char *ext = strrchr(path, '.');
    const char *result = "application/octet-stream";
    int32_t i;
    for (i = 0; ext && box_endpoint_mimes[i].ext; i ++) {
        if (!strcmp(ext, box_endpoint_mimes[i].ext)) {
            result = box_endpoint_mimes[i].type;
            break;
        }
    }

    return result;
}

/** Serve a file from the resources folder of a session.
 *
 * @param world The world
 * @param req The request
 * @param reply The reply
 */
static void box_endpoint_resource(
    ecs_world_t *world,
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply)
{
    ecs_entity_t session = box_endpoint_session(world, req, reply);
    const char *rel = req->path + strlen(BOX_ENDPOINT_RESOURCE);
    const TerminalSession *s;
    char *dir = NULL, *path = NULL;
    struct stat st;
    if (!session) {
        goto done;
    }

    if (!box_endpoint_confined(rel)) {
        box_endpoint_reply(reply, 403, BOX_COMMAND_ERROR "outside resources");
        goto done;
    }

    s = ecs_get(world, session, TerminalSession);
    dir = flecs_asprintf("%s/%s", s->dir, BOX_TERMINAL_RESOURCES);
    path = flecs_asprintf("%s/%s", dir, rel);
    if (!rel[0] || stat(path, &st) || !S_ISREG(st.st_mode)) {
        box_endpoint_reply(reply, 404, BOX_COMMAND_ERROR "no resource");
    } else if (!box_endpoint_inside(dir, path)) {
        box_endpoint_reply(reply, 403, BOX_COMMAND_ERROR "outside resources");
    } else if (box_endpoint_file(path, reply)) {
        box_endpoint_reply(reply, 404, BOX_COMMAND_ERROR "no resource");
    } else {
        box_endpoint_reply(reply, 200, NULL);
        reply->content_type = box_endpoint_mime(path);
    }

done:
    ecs_os_free(dir);
    ecs_os_free(path);
}

/** Append a URL encoded query parameter value.
 *
 * @param buf The buffer
 * @param value The decoded value
 */
static void box_endpoint_encode(
    ecs_strbuf_t *buf,
    const char *value)
{
    const char *p;
    for (p = value; p && p[0]; p ++) {
        unsigned char c = (unsigned char)p[0];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            strchr("-_.~", c))
        {
            ecs_strbuf_appendch(buf, (char)c);
        } else {
            ecs_strbuf_append(buf, "%%%02X", c);
        }
    }
}

/** Get the name of a request method.
 *
 * @param method The method
 * @return The name, such as "GET"
 */
static const char* box_endpoint_method(
    ecs_http_method_t method)
{
    const char *result = "GET";
    if (method == EcsHttpPost) {
        result = "POST";
    } else if (method == EcsHttpPut) {
        result = "PUT";
    } else if (method == EcsHttpDelete) {
        result = "DELETE";
    } else if (method == EcsHttpOptions) {
        result = "OPTIONS";
    }

    return result;
}

/** Forward a request to the REST server of the world.
 *
 * @param host The host
 * @param req The request
 * @param reply The reply
 */
static void box_endpoint_rest(
    box_terminal_host_t *host,
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply)
{
    ecs_strbuf_t url = ECS_STRBUF_INIT;
    int32_t i;
    if (!host->rest) {
        host->rest = ecs_rest_server_init(host->world, NULL);
    }

    ecs_strbuf_append(&url, "/%s", req->path);
    for (i = 0; i < req->param_count; i ++) {
        ecs_strbuf_appendch(&url, i ? '&' : '?');
        box_endpoint_encode(&url, req->params[i].key);
        ecs_strbuf_appendch(&url, '=');
        box_endpoint_encode(&url, req->params[i].value);
    }

    char *str = ecs_strbuf_get(&url);
    ecs_http_server_request(host->rest, box_endpoint_method(req->method), str,
        req->body && req->body[0] ? req->body : NULL, reply);
    if (reply->code < 400 && reply->status && !strcmp(reply->status, "Resource not found")) {
        reply->code = 404;
    }

    ecs_os_free(str);
}

/** Serve one request of the server.
 *
 * @param req The request
 * @param reply The reply
 * @param ctx The host
 * @return Always true, every endpoint sets its status code
 */
static bool box_endpoint_request(
    const ecs_http_request_t *req,
    ecs_http_reply_t *reply,
    void *ctx)
{
    box_terminal_host_t *host = ctx;
    ecs_world_t *world = host->world;
    const char *path = req->path ? req->path : "";
    ecs_entity_t session;
    if (!strcmp(path, "terminal/new")) {
        session = box_terminal_new(world);
        box_endpoint_reply(reply, session ? 200 : 400,
            session ? ecs_get_name(world, session) : BOX_COMMAND_ERROR "no session folder");
    } else if (!strcmp(path, "terminal/close")) {
        session = box_endpoint_session(world, req, reply);
        if (session) {
            ecs_delete(world, session);
            box_endpoint_reply(reply, 200, NULL);
        }
    } else if (!strcmp(path, "terminal/exec")) {
        box_endpoint_exec(world, req, reply);
    } else if (!strncmp(path, BOX_ENDPOINT_RESOURCE, strlen(BOX_ENDPOINT_RESOURCE))) {
        box_endpoint_resource(world, req, reply);
    } else {
        box_endpoint_rest(host, req, reply);
    }

    return true;
}

/** Create the server object, listening on a port when it is not 0.
 *
 * @param host The host
 * @param port The port
 */
static void box_endpoint_start(
    box_terminal_host_t *host,
    int32_t port)
{
    host->server = ecs_http_server_init(&(ecs_http_server_desc_t){
        .callback = box_endpoint_request,
        .ctx = host,
        .port = (uint16_t)port
    });

    host->port = 0;
    if (!host->server) {
        ecs_err("cannot create the terminal server");
    } else if (port && ecs_http_server_start(host->server)) {
        ecs_err("cannot listen on port %d", port);
    } else {
        host->port = port;
        ecs_os_get_time(&host->clock);
    }
}

/** Stop and delete the server object.
 *
 * @param host The host
 */
static void box_endpoint_stop(
    box_terminal_host_t *host)
{
    if (host->server) {
        ecs_http_server_fini(host->server);
        host->server = NULL;
    }

    host->port = 0;
}

/** TerminalServer was set: remember the port, the serve system applies it.
 *
 * @param it Iterator over the singleton
 */
static void box_endpoint_on_set(
    ecs_iter_t *it)
{
    box_terminal_host_t *host = it->ctx;
    const TerminalServer *s = ecs_field(it, TerminalServer, 0);
    host->port_wanted = s->port > 0 ? s->port : 0;
}

/** Restart the server when its port changed and run the requests it received.
 *
 * @param it Iterator of the system
 */
static void BoxTerminalServe(
    ecs_iter_t *it)
{
    box_terminal_host_t *host = it->ctx;
    if (host->port != host->port_wanted) {
        box_endpoint_stop(host);
        box_endpoint_start(host, host->port_wanted);
    }

    if (host->port && host->server) {
        double elapsed = ecs_time_measure(&host->clock);
        ecs_http_server_dequeue(host->server, (ecs_ftime_t)elapsed);
    }
}

int box_command_request(
    ecs_world_t *world,
    const char *method,
    const char *path,
    const char *body,
    ecs_http_reply_t *reply)
{
    box_terminal_host_t *host = box_terminal_host(world);
    ecs_http_server_request(host->server, method, path, body, reply);

    return reply->code;
}

void box_endpoint_fini(
    box_terminal_host_t *host)
{
    box_endpoint_stop(host);
    if (host->rest) {
        ecs_rest_server_fini(host->rest);
        host->rest = NULL;
    }
}

void box_endpoint_init(
    ecs_world_t *world,
    box_terminal_host_t *host)
{
    box_endpoint_start(host, 0);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TerminalServerOnSet" }),
        .query.terms = {{ .id = ecs_id(TerminalServer) }},
        .events = { EcsOnSet },
        .callback = box_endpoint_on_set,
        .ctx = host
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TerminalServe" }),
        .phase = EcsOnLoad,
        .run = BoxTerminalServe,
        .ctx = host,
        .immediate = true
    });
}
