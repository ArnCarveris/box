/**
 * @file command/http.c
 * @brief The HttpServer singleton, the server lifecycle and the dequeue system.
 */

#include "command_internal.h"

ECS_COMPONENT_DECLARE(HttpEndpoint);

/** Stop and delete the server of the endpoint.
 *
 * @param ep The endpoint state
 */
static void box_http_stop(
    HttpEndpoint *ep)
{
    if (ep->server) {
        ecs_http_server_fini(ep->server);
        ep->server = NULL;
        ep->started = false;
    }
}

/** Create a server on a port and start listening.
 *
 * @param ep The endpoint state
 * @param port The port to listen on
 */
static void box_http_start(
    HttpEndpoint *ep,
    int32_t port)
{
    ecs_http_server_desc_t desc = {
        .callback = box_http_endpoint_request,
        .ctx = ep->world,
        .port = (uint16_t)port
    };

    ep->server = ecs_http_server_init(&desc);
    if (!ep->server) {
        ecs_err("failed to create the command server on port %d", port);
    } else if (!ecs_os_has_threading()) {
        ecs_warn("no threading support, the command server does not listen");
    } else if (ecs_http_server_start(ep->server)) {
        ecs_err("failed to start the command server on port %d", port);
    } else {
        ep->started = true;
        ecs_trace("command server listening on port %d", port);
    }
}

/** HttpServer on_set hook: restart the server on the new port.
 *
 * @param it Iterator over the changed servers
 */
static void box_http_on_set(
    ecs_iter_t *it)
{
    HttpServer *s = ecs_field(it, HttpServer, 0);
    HttpEndpoint *ep = box_http_endpoint(it->world);
    ecs_assert(ep != NULL, ECS_INVALID_OPERATION, "no http endpoint");
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_http_stop(ep);
        if (s[i].port) {
            box_http_start(ep, s[i].port);
        }
    }
}

/** HttpServer on_remove hook: stop the server.
 *
 * @param it Iterator over the removed servers
 */
static void box_http_on_remove(
    ecs_iter_t *it)
{
    HttpEndpoint *ep = box_http_endpoint(it->world);
    if (ep) {
        box_http_stop(ep);
    }
}

/** HttpEndpoint destructor: stop the server.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_http_endpoint_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    HttpEndpoint *ep = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_http_stop(&ep[i]);
    }
}

/** App was set: publish the configured port as the server port.
 *
 * @param it Iterator over the App singleton
 */
static void box_http_app_on_set(
    ecs_iter_t *it)
{
    const App *app = ecs_field(it, App, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        int32_t port = app[i].port;
        const HttpServer *s = ecs_singleton_get(it->world, HttpServer);
        if (s && s->port == port) {
            continue;
        }

        if (s || port) {
            ecs_singleton_set(it->world, HttpServer, { .port = port });
        }
    }
}

/** Dequeue the requests that arrived since the last frame.
 *
 * @param it Iterator over the endpoint singleton
 */
static void BoxHttpDequeue(
    ecs_iter_t *it)
{
    HttpEndpoint *ep = ecs_field(it, HttpEndpoint, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (ep[i].started) {
            ecs_http_server_dequeue(ep[i].server, it->delta_time);
        }
    }
}

HttpEndpoint* box_http_endpoint(
    ecs_world_t *world)
{
    HttpEndpoint *result = NULL;
    ecs_entity_t e = ecs_id(HttpEndpoint);
    if (e && ecs_is_alive(world, e) && ecs_has(world, e, HttpEndpoint)) {
        result = ecs_get_mut(world, e, HttpEndpoint);
    }

    return result;
}

ecs_http_server_t* box_http_server(
    ecs_world_t *world)
{
    HttpEndpoint *ep = box_http_endpoint(world);

    return ep ? ep->server : NULL;
}

void box_http_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, HttpServer);
    ecs_struct(world, {
        .entity = ecs_id(HttpServer),
        .members = {{ .name = "port", .type = ecs_id(ecs_i32_t) }}
    });

    ECS_COMPONENT_DEFINE(world, HttpEndpoint);
    ecs_set_hooks(world, HttpEndpoint, {
        .ctor = flecs_default_ctor,
        .dtor = box_http_endpoint_dtor
    });
    ecs_add_pair(world, ecs_id(HttpEndpoint), EcsOnInstantiate, EcsDontInherit);

    ecs_set_hooks(world, HttpServer, {
        .ctor = flecs_default_ctor,
        .on_set = box_http_on_set,
        .on_remove = box_http_on_remove
    });
    ecs_add_pair(world, ecs_id(HttpServer), EcsOnInstantiate, EcsDontInherit);

    HttpEndpoint *ep = ecs_singleton_ensure(world, HttpEndpoint);
    ep->world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    ecs_singleton_modified(world, HttpEndpoint);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxHttpDequeue" }),
        .phase = EcsOnLoad,
        .query.terms = {{ .id = ecs_id(HttpEndpoint) }},
        .callback = BoxHttpDequeue,
        .immediate = true
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(App) }},
        .events = { EcsOnSet },
        .callback = box_http_app_on_set
    });
}
