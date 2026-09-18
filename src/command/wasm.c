/**
 * @file command/wasm.c
 * @brief The box_request export that routes requests through the command endpoints.
 */

#include "command_internal.h"
#include <string.h>

#ifdef BOX_PLATFORM_WEB
#include <emscripten.h>
#endif

/** Content type whose body is base64 encoded in a reply */
#define BOX_REQUEST_BINARY "image/png"

/** Content type of a reply that the endpoints did not type */
#define BOX_REQUEST_TEXT "text/plain"

/** World that box_request routes through, a wasm image runs exactly one world */
static ecs_world_t *box_wasm_world;

void box_wasm_init(
    ecs_world_t *world)
{
    box_wasm_world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
}

char* box_request_format(
    ecs_world_t *world,
    const char *method,
    const char *path,
    const char *body)
{
    HttpEndpoint *ep = box_http_endpoint(world);
    ecs_assert(ep != NULL, ECS_INVALID_OPERATION, "no command endpoint");
    ecs_os_free(ep->reply);
    ep->reply = NULL;

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    reply.content_type = BOX_REQUEST_TEXT;
    if (ep->server) {
        ecs_http_server_request(ep->server, method, path, body, &reply);
    } else {
        reply.code = 503;
        ecs_strbuf_appendstr(&reply.body, "no command server");
    }

    int32_t size = ecs_strbuf_written(&reply.body);
    char *content = ecs_strbuf_get(&reply.body);
    ecs_strbuf_reset(&reply.headers);

    const char *type = reply.content_type ? reply.content_type : BOX_REQUEST_TEXT;
    char *encoded = NULL;
    if (!strcmp(type, BOX_REQUEST_BINARY)) {
        encoded = box_base64_encode(content, size);
    }

    const char *out = encoded ? encoded : content;
    ep->reply = flecs_asprintf("%d\n%s\n%s", reply.code, type, out ? out : "");
    ecs_os_free(encoded);
    ecs_os_free(content);

    return ep->reply;
}

#ifdef BOX_PLATFORM_WEB
/** Route a request through the command endpoints from JavaScript.
 *
 * @param method The HTTP method
 * @param path The request path with its query parameters
 * @param body The request body, NULL or empty for none
 * @return The reply, owned by the module and valid until the next call
 */
EMSCRIPTEN_KEEPALIVE
char* box_request(
    const char *method,
    const char *path,
    const char *body)
{
    ecs_assert(box_wasm_world != NULL, ECS_INVALID_OPERATION, "box.command not imported");

    return box_request_format(box_wasm_world, method, path, body);
}
#endif
