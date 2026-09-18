/**
 * @file Base64.c
 * @brief Tests for the base64 encoder of the request export.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/command/command_internal.h"

void Base64_setup(void)
{
    ecs_os_init();
}

void Base64_teardown(void)
{
    ecs_os_fini();
}

void Base64_empty(void)
{
    // inspector/wasm_request
    char *out = box_base64_encode("", 0);
    test_str(out, "");
    ecs_os_free(out);
}

void Base64_one_byte(void)
{
    // inspector/wasm_request
    char *out = box_base64_encode("M", 1);
    test_str(out, "TQ==");
    ecs_os_free(out);
}

void Base64_two_bytes(void)
{
    // inspector/wasm_request
    char *out = box_base64_encode("Ma", 2);
    test_str(out, "TWE=");
    ecs_os_free(out);
}

void Base64_three_bytes(void)
{
    // inspector/wasm_request
    char *out = box_base64_encode("Man", 3);
    test_str(out, "TWFu");
    ecs_os_free(out);
}

void Base64_longer_text(void)
{
    // inspector/wasm_request
    const char *in = "any carnal pleasure.";
    char *out = box_base64_encode(in, (int32_t)ecs_os_strlen(in));
    test_str(out, "YW55IGNhcm5hbCBwbGVhc3VyZS4=");
    ecs_os_free(out);
}

void Base64_binary_bytes(void)
{
    // inspector/wasm_request
    const unsigned char in[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
    char *out = box_base64_encode(in, 8);
    test_str(out, "iVBORw0KGgo=");
    ecs_os_free(out);
}
