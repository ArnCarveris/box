/**
 * @file Http.c
 * @brief Tests for the terminal and resource endpoints of the command server.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#ifdef _WIN32
/** A client socket, as Winsock returns it */
typedef SOCKET http_socket_t;
#define HTTP_SOCKET_NONE INVALID_SOCKET
#else
/** A client socket, as POSIX returns it */
typedef int http_socket_t;
#define HTTP_SOCKET_NONE (-1)
#endif

static ecs_world_t *w;
static ecs_http_server_t *srv;

/** Close a client socket.
 *
 * @param sock The socket
 */
static void http_close(
    http_socket_t sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

/** Make a receive on a client socket give up after a while.
 *
 * @param sock The socket
 * @param ms Milliseconds a receive waits
 */
static void http_recv_timeout(
    http_socket_t sock,
    int32_t ms)
{
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
#else
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
#endif
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
}

/** Connect to a local server, retrying while it starts to listen.
 *
 * @param port Port the server listens on
 * @return The connected socket, HTTP_SOCKET_NONE when it never accepted
 */
static http_socket_t http_connect(
    uint16_t port)
{
    http_socket_t result = HTTP_SOCKET_NONE;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int32_t i;
    for (i = 0; i < 50 && result == HTTP_SOCKET_NONE; i ++) {
        ecs_os_sleep(0, 20000000);
        http_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == HTTP_SOCKET_NONE) {
            continue;
        }

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            result = sock;
        } else {
            http_close(sock);
        }
    }

    return result;
}

void Http_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_singleton_set(w, HttpServer, { .port = 27781 });
    srv = box_http_server(w);
    test_assert(srv != NULL);
}

void Http_teardown(void)
{
    ecs_fini(w);
}

void Http_new_returns_session(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    test_int(reply.code, 200);
    test_str(reply.content_type, "text/plain");
    char *name = ecs_strbuf_get(&reply.body);
    test_str(name, "s1");
    ecs_entity_t t = ecs_lookup(w, "terminals.s1");
    test_assert(t != 0);
    test_assert(ecs_has(w, t, Terminal));
    ecs_os_free(name);
}

void Http_new_names_are_unique(void)
{
    // command/http
    ecs_http_reply_t first = ECS_HTTP_REPLY_INIT;
    ecs_http_reply_t second = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &first), 0);
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &second), 0);
    char *a = ecs_strbuf_get(&first.body);
    char *b = ecs_strbuf_get(&second.body);
    test_assert(strcmp(a, b) != 0);
    test_assert(ecs_lookup_child(w, ecs_lookup(w, "terminals"), a) != 0);
    test_assert(ecs_lookup_child(w, ecs_lookup(w, "terminals"), b) != 0);
    ecs_os_free(a);
    ecs_os_free(b);
}

void Http_close_deletes_session(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    ecs_entity_t t = ecs_lookup_child(w, ecs_lookup(w, "terminals"), name);
    test_assert(t != 0);
    char *req = flecs_asprintf("/terminal/close?session=%s", name);
    ecs_http_reply_t closed = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &closed), 0);
    test_int(closed.code, 200);
    test_assert(!ecs_is_alive(w, t));
    ecs_strbuf_reset(&closed.body);
    ecs_os_free(req);
    ecs_os_free(name);
}

void Http_close_unknown_session(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/terminal/close?session=nope", NULL, &reply), -1);
    test_int(reply.code, 404);
    ecs_strbuf_reset(&reply.body);
}

void Http_exec_returns_output(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    char *req = flecs_asprintf("/terminal/exec?session=%s&cmd=help", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), 0);
    test_int(out.code, 200);
    test_str(out.content_type, "text/plain");
    char *body = ecs_strbuf_get(&out.body);
    test_assert(strstr(body, "help - ") != NULL);
    ecs_os_free(body);
    ecs_os_free(req);
    ecs_os_free(name);
}

void Http_exec_url_encoded_cmd(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    char *first = flecs_asprintf("/terminal/exec?session=%s&cmd=help", name);
    ecs_http_reply_t ran = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", first, NULL, &ran), 0);
    ecs_strbuf_reset(&ran.body);
    char *req = flecs_asprintf("/terminal/exec?session=%s&cmd=commands%%2020", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), 0);
    test_int(out.code, 200);
    char *body = ecs_strbuf_get(&out.body);
    test_assert(strstr(body, "$ help") != NULL);
    ecs_os_free(body);
    ecs_os_free(req);
    ecs_os_free(first);
    ecs_os_free(name);
}

void Http_exec_post_body(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    char *req = flecs_asprintf("/terminal/exec?session=%s", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "POST", req, "help", &out), 0);
    test_int(out.code, 200);
    char *body = ecs_strbuf_get(&out.body);
    test_assert(strstr(body, "help - ") != NULL);
    ecs_os_free(body);
    ecs_os_free(req);
    ecs_os_free(name);
}

void Http_exec_unknown_session(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(
        srv, "GET", "/terminal/exec?session=nope&cmd=help", NULL, &reply), -1);
    test_int(reply.code, 404);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "unknown session");
    ecs_os_free(body);
}

void Http_exec_error_gives_400(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    char *req = flecs_asprintf("/terminal/exec?session=%s&cmd=nosuchcommand", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), -1);
    test_int(out.code, 400);
    char *body = ecs_strbuf_get(&out.body);
    test_assert(strstr(body, "unknown command") != NULL);
    ecs_os_free(body);
    ecs_os_free(req);
    ecs_os_free(name);
}

void Http_exec_without_cmd(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    char *req = flecs_asprintf("/terminal/exec?session=%s", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), -1);
    test_int(out.code, 400);
    ecs_strbuf_reset(&out.body);
    ecs_os_free(req);
    ecs_os_free(name);
}

void Http_resource_returns_file(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    ecs_entity_t t = ecs_lookup_child(w, ecs_lookup(w, "terminals"), name);
    char *path = flecs_asprintf("%s/resources/hello.txt",
        ecs_get(w, t, Terminal)->session_dir);
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fprintf(f, "hello resources");
    fclose(f);
    char *req = flecs_asprintf("/resource/hello.txt?session=%s", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), 0);
    test_int(out.code, 200);
    test_str(out.content_type, "text/plain");
    char *body = ecs_strbuf_get(&out.body);
    test_str(body, "hello resources");
    ecs_os_free(body);
    ecs_os_free(req);
    ecs_os_free(path);
    ecs_os_free(name);
}

void Http_resource_png_content_type(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    ecs_entity_t t = ecs_lookup_child(w, ecs_lookup(w, "terminals"), name);
    char *path = flecs_asprintf("%s/resources/shot.png",
        ecs_get(w, t, Terminal)->session_dir);
    FILE *f = fopen(path, "wb");
    test_assert(f != NULL);
    fwrite("\x89PNG", 1, 4, f);
    fclose(f);
    char *req = flecs_asprintf("/resource/shot.png?session=%s", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), 0);
    test_int(out.code, 200);
    test_str(out.content_type, "image/png");
    ecs_strbuf_reset(&out.body);
    ecs_os_free(req);
    ecs_os_free(path);
    ecs_os_free(name);
}

void Http_resource_traversal_refused(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    char *req = flecs_asprintf("/resource/../commands.log?session=%s", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), -1);
    test_int(out.code, 403);
    char *body = ecs_strbuf_get(&out.body);
    test_assert(strstr(body, "outside") != NULL);
    ecs_os_free(body);
    ecs_os_free(req);
    ecs_os_free(name);
}

void Http_resource_missing_gives_404(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    char *name = ecs_strbuf_get(&reply.body);
    char *req = flecs_asprintf("/resource/nope.txt?session=%s", name);
    ecs_http_reply_t out = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", req, NULL, &out), -1);
    test_int(out.code, 404);
    ecs_strbuf_reset(&out.body);
    ecs_os_free(req);
    ecs_os_free(name);
}

void Http_unknown_endpoint_gives_404(void)
{
    // command/http
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/nope", NULL, &reply), -1);
    test_int(reply.code, 404);
    ecs_strbuf_reset(&reply.body);
}

void Http_dequeue_system_runs(void)
{
    // command/http
    test_assert(ecs_lookup(w, "box.command.BoxHttpDequeue") != 0);
    test_bool(ecs_progress(w, 0), true);
    test_bool(ecs_progress(w, 0), true);
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "GET", "/terminal/new", NULL, &reply), 0);
    test_int(reply.code, 200);
    ecs_strbuf_reset(&reply.body);
}

void Http_app_port_starts_server(void)
{
    // command/http
    App *app = ecs_singleton_ensure(w, App);
    app->port = 27782;
    ecs_singleton_modified(w, App);
    const HttpServer *s = ecs_singleton_get(w, HttpServer);
    test_assert(s != NULL);
    test_int(s->port, 27782);
    test_assert(box_http_server(w) != NULL);
    test_bool(box_http_endpoint(w)->started, true);
}

void Http_port_zero_stops_server(void)
{
    // command/http
    test_assert(box_http_server(w) != NULL);
    ecs_singleton_set(w, HttpServer, { .port = 0 });
    test_assert(box_http_server(w) == NULL);
}

void Http_a_connection_survives_a_burst_of_fast_frames(void)
{
    // ore_engine/headless_http_purge
    ecs_singleton_set(w, HttpServer, { .port = 27791 });
    http_socket_t sock = http_connect(27791);
    test_assert(sock != HTTP_SOCKET_NONE);
    http_recv_timeout(sock, 100);

    const char *req = "GET /terminal/new HTTP/1.1\r\nHost: localhost\r\n\r\n";
    int len = (int)strlen(req);
    test_int((int)send(sock, req, len, 0), len);
    ecs_os_sleep(0, 2000000);

    int32_t i;
    for (i = 0; i < 1200; i ++) {
        ecs_progress(w, 1.0f / 60.0f);
    }

    char buf[512];
    int got = 0;
    for (i = 0; i < 50 && got <= 0; i ++) {
        ecs_progress(w, 1.0f / 60.0f);
        got = (int)recv(sock, buf, (int)sizeof(buf) - 1, 0);
    }

    http_close(sock);
    test_assert(got > 0);
    buf[got] = '\0';
    test_assert(strstr(buf, "HTTP/1.1 200") == buf);
}
