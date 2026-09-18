/**
 * @file CaptureEndpoint.c
 * @brief Tests for the capture endpoint that renders a camera to the session resources.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"
#include "../../../src/surface/surface_internal.h"

static ecs_world_t *w;
static ecs_http_server_t *srv;
static ecs_entity_t session;

void CaptureEndpoint_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCommand);
    ecs_singleton_set(w, HttpServer, { .port = 27791 });
    srv = box_http_server(w);
    test_assert(srv != NULL);
    session = ecs_entity(w, {
        .name = "agent0", .parent = ecs_lookup(w, "terminals")
    });

    ecs_add(w, session, Terminal);
}

void CaptureEndpoint_teardown(void)
{
    ecs_fini(w);
}

void CaptureEndpoint_unknown_session_gives_404(void)
{
    // inspector/capture_endpoint
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/capture?session=nope", NULL, &reply), -1);
    test_int(reply.code, 404);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "unknown session");
    ecs_os_free(body);
    ecs_strbuf_reset(&reply.headers);
}

void CaptureEndpoint_unknown_camera_gives_400(void)
{
    // inspector/capture_endpoint
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/capture?session=agent0&camera=nope", NULL, &reply), -1);
    test_int(reply.code, 400);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "unknown camera 'nope'");
    test_assert(!ecs_has(w, cam, Capture));
    ecs_os_free(body);
    ecs_strbuf_reset(&reply.headers);
}

void CaptureEndpoint_without_camera_gives_400(void)
{
    // inspector/capture_endpoint
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/capture?session=agent0", NULL, &reply), -1);
    test_int(reply.code, 400);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "no active camera");
    ecs_os_free(body);
    ecs_strbuf_reset(&reply.headers);
}

void CaptureEndpoint_first_request_sets_capture(void)
{
    // inspector/capture_endpoint
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/capture?session=agent0", NULL, &reply), 0);
    test_int(reply.code, 202);
    char *body = ecs_strbuf_get(&reply.body);
    test_assert(body == NULL);

    const Capture *capture = ecs_get(w, cam, Capture);
    test_assert(capture != NULL);
    char *expect = flecs_asprintf("%s/resources/live.png",
        ecs_get(w, session, Terminal)->session_dir);
    test_str(capture->path, expect);
    ecs_os_free(expect);
    ecs_strbuf_reset(&reply.headers);
}

void CaptureEndpoint_second_request_keeps_the_capture(void)
{
    // inspector/capture_endpoint
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});

    ecs_http_reply_t first = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/capture?session=agent0", NULL, &first), 0);
    const Capture *capture = ecs_get(w, cam, Capture);
    test_assert(capture != NULL);
    const char *path = capture->path;

    ecs_http_reply_t second = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/capture?session=agent0", NULL, &second), 0);
    test_int(second.code, 202);
    capture = ecs_get(w, cam, Capture);
    test_assert(capture != NULL);
    test_assert(capture->path == path);
    ecs_strbuf_reset(&first.headers);
    ecs_strbuf_reset(&second.headers);
}

void CaptureEndpoint_named_camera(void)
{
    // inspector/capture_endpoint
    ecs_entity_t a = ecs_entity(w, { .name = "cam0" });
    ecs_set(w, a, Position, {0, 0, -5});
    ecs_set(w, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(w, { .name = "cam1" });
    ecs_set(w, b, Position, {0, 0, 5});
    ecs_set(w, b, Camera, {.fov = 60});

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/capture?session=agent0&camera=cam1", NULL, &reply), 0);
    test_int(reply.code, 202);
    test_assert(!ecs_has(w, a, Capture));
    test_assert(ecs_has(w, b, Capture));
    ecs_strbuf_reset(&reply.headers);
}

void CaptureEndpoint_reply_allows_any_origin(void)
{
    // inspector/capture_endpoint
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *headers = ecs_strbuf_get(&reply.headers);
    test_assert(headers != NULL);
    test_assert(strstr(headers, "Access-Control-Allow-Origin: *") != NULL);
    ecs_os_free(headers);
    ecs_strbuf_reset(&reply.body);
}

void CaptureEndpoint_preflight_lists_methods(void)
{
    // inspector/capture_endpoint
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "OPTIONS", "/capture", NULL, &reply), 0);
    test_int(reply.code, 200);
    char *headers = ecs_strbuf_get(&reply.headers);
    test_assert(headers != NULL);
    test_assert(strstr(headers, "Access-Control-Allow-Origin: *") != NULL);
    test_assert(strstr(headers,
        "Access-Control-Allow-Methods: GET, POST, OPTIONS") != NULL);
    test_assert(strstr(headers, "Access-Control-Allow-Headers: Content-Type") != NULL);
    ecs_os_free(headers);
    ecs_strbuf_reset(&reply.body);
}

void CaptureEndpoint_headless_run_returns_the_png(void)
{
    // inspector/capture_endpoint
    App *app = ecs_singleton_ensure(w, App);
    app->headless = true;
    app->port = 27791;
    ecs_singleton_modified(w, App);
    srv = box_http_server(w);
    test_assert(srv != NULL);

    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 64;
    s->height = 32;
    ecs_singleton_modified(w, Surface);

    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});

    const SurfaceImpl *impl = ecs_get(w, ecs_id(Surface), SurfaceImpl);
    if (impl && impl->renderer) {
        bool written = false;
        int32_t i;
        for (i = 0; i < 200 && !written; i ++) {
            ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
            test_int(ecs_http_server_request(
                srv, "GET", "/capture?session=agent0", NULL, &reply), 0);
            if (reply.code == 200) {
                test_str(reply.content_type, "image/png");
                char *body = ecs_strbuf_get(&reply.body);
                written = body && body[1] == 'P' && body[2] == 'N' && body[3] == 'G';
                ecs_os_free(body);
            } else {
                test_int(reply.code, 202);
                ecs_strbuf_reset(&reply.body);
            }

            ecs_strbuf_reset(&reply.headers);
            ecs_progress(w, 0);
        }

        test_bool(written, true);
    }
}
