/**
 * @file Patch.c
 * @brief Tests for the unified diff helper that patches script sources.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "../../../src/command/command_internal.h"

void Patch_setup(void)
{
    ecs_os_init();
}

void Patch_teardown(void)
{
    ecs_os_fini();
}

void Patch_single_hunk(void)
{
    // command/script
    char *out = box_patch_apply("a\nb\nc\n", "@@ -2,1 +2,1 @@\n-b\n+B\n", NULL);
    test_str(out, "a\nB\nc\n");
    ecs_os_free(out);
}

void Patch_multiple_hunks(void)
{
    // command/script
    const char *patch =
        "@@ -1,1 +1,1 @@\n"
        "-a\n"
        "+A\n"
        "@@ -5,1 +5,2 @@\n"
        " e\n"
        "+f\n";
    char *out = box_patch_apply("a\nb\nc\nd\ne\n", patch, NULL);
    test_str(out, "A\nb\nc\nd\ne\nf\n");
    ecs_os_free(out);
}

void Patch_addition_at_end(void)
{
    // command/script
    char *out = box_patch_apply("a\nb\n", "@@ -2,1 +2,3 @@\n b\n+c\n+d\n", NULL);
    test_str(out, "a\nb\nc\nd\n");
    ecs_os_free(out);
}

void Patch_deletion(void)
{
    // command/script
    const char *patch =
        "@@ -1,3 +1,1 @@\n"
        " a\n"
        "-b\n"
        "-c\n";
    char *out = box_patch_apply("a\nb\nc\nd\n", patch, NULL);
    test_str(out, "a\nd\n");
    ecs_os_free(out);
}

void Patch_diff_headers_ignored(void)
{
    // command/script
    const char *patch =
        "--- a/scene.flecs\n"
        "+++ b/scene.flecs\n"
        "@@ -1,1 +1,1 @@\n"
        "-a\n"
        "+A\n";
    char *out = box_patch_apply("a\n", patch, NULL);
    test_str(out, "A\n");
    ecs_os_free(out);
}

void Patch_context_mismatch(void)
{
    // command/script
    char *error = NULL;
    const char *patch =
        "@@ -1,2 +1,2 @@\n"
        " a\n"
        "-x\n"
        "+y\n";
    char *out = box_patch_apply("a\nb\n", patch, &error);
    test_assert(out == NULL);
    test_str(error, "hunk 1: context does not match at line 2");
    ecs_os_free(error);
}

void Patch_no_hunks(void)
{
    // command/script
    char *error = NULL;
    char *out = box_patch_apply("a\n", "--- a/x\n+++ b/x\n", &error);
    test_assert(out == NULL);
    test_str(error, "patch contains no hunks");
    ecs_os_free(error);
}
