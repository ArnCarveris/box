/**
 * @file WasmRequest.c
 * @brief Tests for the reply format of the request export of the command module.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;

void WasmRequest_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_singleton_set(w, HttpServer, { .port = 27783 });
    test_assert(box_http_server(w) != NULL);
}

void WasmRequest_teardown(void)
{
    ecs_fini(w);
}

void WasmRequest_help_round_trip(void)
{
    // inspector/wasm_request
    char *reply = box_request_format(w, "GET", "/terminal/new", NULL);
    test_str(reply, "200\ntext/plain\ns1");
    char *out = box_request_format(w, "GET", "/terminal/exec?session=s1&cmd=help", NULL);
    test_assert(!strncmp(out, "200\ntext/plain\n", 15));
    test_assert(strstr(out, "help - ") != NULL);
}

void WasmRequest_unknown_path_gives_404(void)
{
    // inspector/wasm_request
    char *reply = box_request_format(w, "GET", "/nope", NULL);
    test_str(reply, "404\ntext/plain\nunknown endpoint");
}

void WasmRequest_bad_command_gives_error_status(void)
{
    // inspector/wasm_request
    char *reply = box_request_format(w, "GET", "/terminal/new", NULL);
    test_str(reply, "200\ntext/plain\ns1");
    char *out = box_request_format(
        w, "GET", "/terminal/exec?session=s1&cmd=nosuchcommand", NULL);
    test_assert(!strncmp(out, "400\ntext/plain\n", 15));
    test_assert(strstr(out, "unknown command") != NULL);
}

void WasmRequest_png_reply_is_base64(void)
{
    // inspector/wasm_request
    const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x2a };
    char *reply = box_request_format(w, "GET", "/terminal/new", NULL);
    test_str(reply, "200\ntext/plain\ns1");
    ecs_entity_t t = ecs_lookup(w, "terminals.s1");
    test_assert(t != 0);
    char *path = flecs_asprintf("%s/resources/shot.png",
        ecs_get(w, t, Terminal)->session_dir);
    FILE *f = fopen(path, "wb");
    test_assert(f != NULL);
    test_int(fwrite(png, 1, sizeof(png), f), sizeof(png));
    fclose(f);
    char *out = box_request_format(w, "GET", "/resource/shot.png?session=s1", NULL);
    char *encoded = box_base64_encode(png, sizeof(png));
    char *expect = flecs_asprintf("200\nimage/png\n%s", encoded);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(encoded);
    ecs_os_free(path);
}
