/**
 * @file Preprocess.c
 * @brief Tests for the WGSL preprocessor: includes, macros, conditionals and errors.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/shader/shader_internal.h"

static ecs_world_t *w;
static box_shader_result_t result;

void Preprocess_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    result.source = NULL;
    result.error = NULL;
}

void Preprocess_teardown(void)
{
    box_shader_result_fini(&result);
    ecs_fini(w);
}

void Preprocess_include_is_resolved_next_to_its_file(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/uses_include.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    test_assert(result.source != NULL);
    test_assert(strstr(result.source, "fn base() -> i32 {") != NULL);
    test_assert(strstr(result.source, "fn root() -> i32 {") != NULL);
    test_assert(strstr(result.source, "#include") == NULL);
}

void Preprocess_nested_includes_are_expanded(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/nested_a.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "fn base() -> i32 {") != NULL);
    test_assert(strstr(result.source, "fn middle() -> i32 {") != NULL);
    test_assert(strstr(result.source, "fn outer() -> i32 {") != NULL);
    test_assert(strstr(result.source, "fn base()") < strstr(result.source, "fn middle()"));
}

void Preprocess_cyclic_include_reports_the_file_and_line(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/cycle_a.wgsl", NULL, &result);

    test_assert(result.source == NULL);
    test_assert(result.error != NULL);
    test_str(result.error, "etc/cycle_b.wgsl:1: cyclic include");
}

void Preprocess_pragma_once_includes_a_file_once(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/twice.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    const char *first = strstr(result.source, "fn base() -> i32 {");
    test_assert(first != NULL);
    test_assert(strstr(first + 1, "fn base() -> i32 {") == NULL);
}

void Preprocess_macro_is_defined_and_substituted(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/defines.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const level: i32 = 2;") != NULL);
    test_assert(strstr(result.source, "#define") == NULL);
}

void Preprocess_conditional_block_picks_a_branch(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/cond.wgsl", "FLAG=1", &result);
    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const chosen: i32 = 1;") != NULL);
    test_assert(strstr(result.source, "const chosen: i32 = 2;") == NULL);

    box_shader_result_fini(&result);
    box_shader_preprocess(w, "etc/cond.wgsl", "FLAG=0", &result);
    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const chosen: i32 = 1;") == NULL);
    test_assert(strstr(result.source, "const chosen: i32 = 2;") != NULL);
}

void Preprocess_ifdef_follows_a_definition(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/cond.wgsl", "NAMED=0", &result);
    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const named: i32 = 1;") != NULL);

    box_shader_result_fini(&result);
    box_shader_preprocess(w, "etc/cond.wgsl", NULL, &result);
    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const named: i32 = 1;") == NULL);
}

void Preprocess_undefined_macro_is_zero_in_a_condition(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/cond.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const chosen: i32 = 2;") != NULL);
    test_assert(strstr(result.source, "const chosen: i32 = 1;") == NULL);
}

void Preprocess_missing_include_reports_the_file_and_line(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/missing_include.wgsl", NULL, &result);

    test_assert(result.source == NULL);
    test_str(result.error, "etc/missing_include.wgsl:4: include not found");
}

void Preprocess_error_line_counts_inside_the_include(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/bad_outer.wgsl", NULL, &result);

    test_assert(result.source == NULL);
    test_str(result.error, "etc/bad_inner.wgsl:3: unknown directive");
}

void Preprocess_engine_constants_are_predefined(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/constants.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const cascades: i32 = 4;") != NULL);
    test_assert(strstr(result.source, "const layered: i32 = 1;") != NULL);
}

void Preprocess_component_macro_overrides_a_file_define(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/defines.wgsl", "LEVEL=7", &result);

    test_assert(result.error == NULL);
    test_assert(strstr(result.source, "const level: i32 = 7;") != NULL);
    test_assert(strstr(result.source, "const level: i32 = 2;") == NULL);
}

void Preprocess_generated_layers_include_is_empty_without_layers(void)
{
    // effects/shader_component
    char *source = box_shader_layers_wgsl(w);

    test_assert(source != NULL);
    test_str(source, "fn apply_layers(s: ptr<function, Surface>, mask: u32) {\n}\n");
    ecs_os_free(source);
}

void Preprocess_missing_file_is_an_error(void)
{
    // effects/shader_component
    box_shader_preprocess(w, "etc/does_not_exist.wgsl", NULL, &result);

    test_assert(result.source == NULL);
    test_str(result.error, "etc/does_not_exist.wgsl:0: file could not be read");
}
