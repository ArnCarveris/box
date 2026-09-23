/**
 * @file Path.c
 * @brief Tests for the shader search path: the bake environment, the source tree and overrides.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/shader/shader_internal.h"

static ecs_world_t *w;
static box_shader_result_t result;
static char path_bake_home[BOX_SHADER_PATH_MAX];
static char path_home[BOX_SHADER_PATH_MAX];

void Path_setup(void)
{
    w = ecs_init();
    const char *bake_home = getenv("BAKE_HOME");
    const char *home = getenv("HOME");
    snprintf(path_bake_home, sizeof(path_bake_home), "%s",
        bake_home ? bake_home : "");
    snprintf(path_home, sizeof(path_home), "%s", home ? home : "");
    result.source = NULL;
    result.error = NULL;
}

void Path_teardown(void)
{
    if (path_bake_home[0]) {
        setenv("BAKE_HOME", path_bake_home, 1);
    } else {
        unsetenv("BAKE_HOME");
    }

    if (path_home[0]) {
        setenv("HOME", path_home, 1);
    } else {
        unsetenv("HOME");
    }

    box_shader_path_init();
    box_shader_result_fini(&result);
    ecs_fini(w);
}

void Path_bake_home_holds_the_engine_shaders(void)
{
    // engine_core/framework_etc
    char resolved[BOX_SHADER_PATH_MAX];
    setenv("BAKE_HOME", "etc/bake_home", 1);
    box_shader_path_init();

    test_bool(box_locate("etc/fixture/engine.wgsl", resolved,
        sizeof(resolved)), true);
    test_str(resolved, "etc/bake_home/etc/box/fixture/engine.wgsl");
}

void Path_source_tree_answers_when_the_bake_home_is_empty(void)
{
    // engine_core/framework_etc
    char resolved[BOX_SHADER_PATH_MAX];
    setenv("BAKE_HOME", "etc/no_home", 1);
    box_shader_path_init();

    test_bool(box_locate("etc/shaders/mesh.wgsl", resolved,
        sizeof(resolved)), true);
    test_assert(strstr(resolved, "/etc/shaders/mesh.wgsl") != NULL);
    test_assert(strstr(resolved, "no_home") == NULL);
    test_assert(strstr(resolved, "apps/box_app") == NULL);
}

void Path_global_bake_home_is_used_without_the_variable(void)
{
    // engine_core/framework_etc
    char resolved[BOX_SHADER_PATH_MAX];
    unsetenv("BAKE_HOME");
    setenv("HOME", "etc/home", 1);
    box_shader_path_init();

    test_bool(box_locate("etc/fixture/home.wgsl", resolved,
        sizeof(resolved)), true);
    test_str(resolved, "etc/home/bake3/etc/box/fixture/home.wgsl");
}

void Path_working_directory_is_searched_before_the_bake_home(void)
{
    // engine_core/framework_etc
    char resolved[BOX_SHADER_PATH_MAX];
    setenv("BAKE_HOME", "etc/bake_home", 1);
    box_shader_path_init();

    test_bool(box_locate("etc/fixture/both.wgsl", resolved,
        sizeof(resolved)), true);
    test_str(resolved, "etc/fixture/both.wgsl");
}

void Path_app_file_overrides_an_engine_include(void)
{
    // engine_core/framework_etc
    setenv("BAKE_HOME", "etc/bake_home", 1);
    box_shader_path_init();

    box_shader_preprocess(w, "etc/fixture/engine.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    test_assert(result.source != NULL);
    test_assert(strstr(result.source, "fn engine()") != NULL);
    test_assert(strstr(result.source, "app part") != NULL);
    test_assert(strstr(result.source, "engine part") == NULL);
}

void Path_engine_include_is_used_when_the_app_has_none(void)
{
    // engine_core/framework_etc
    setenv("BAKE_HOME", "etc/bake_home", 1);
    box_shader_path_init();

    box_shader_preprocess(w, "etc/fixture/engine_only.wgsl", NULL, &result);

    test_assert(result.error == NULL);
    test_assert(result.source != NULL);
    test_assert(strstr(result.source, "fn engine_only()") != NULL);
    test_assert(strstr(result.source, "engine only part") != NULL);
}
