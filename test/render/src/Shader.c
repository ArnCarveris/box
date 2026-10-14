/**
 * @file Shader.c
 * @brief Tests for the WGSL preprocessor, the Shader component and shader modules on the GPU.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "preprocess.h"

/** Read a file from a NULL terminated list of path and contents pairs.
 *
 * @param ctx The list
 * @param path The file
 * @return A copy of the contents, NULL when the list has no such file
 */
static char* test_read(
    void *ctx,
    const char *path)
{
    const char **files = ctx;
    char *result = NULL;
    int i;
    for (i = 0; files[i] && !result; i += 2) {
        if (!strcmp(files[i], path)) {
            result = ecs_os_strdup(files[i + 1]);
        }
    }

    return result;
}

void Shader_plain_source(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "fn a() {}\nfn b() {}", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "fn a() {}\nfn b() {}\n");
    test_assert(out.error == NULL);
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_include_relative(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "shaders/main.wgsl", "a\n#include \"common.wgsl\"\nc\n",
        "shaders/common.wgsl", "b\n",
        NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "shaders/main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "a\nb\nc\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_include_nested(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "s/main.wgsl", "#include \"lib/a.wgsl\"\nmain\n",
        "s/lib/a.wgsl", "#include \"b.wgsl\"\na\n",
        "s/lib/b.wgsl", "b\n",
        NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "s/main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "b\na\nmain\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_include_search_path(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "app/main.wgsl", "#include \"etc/lib.wgsl\"\nmain\n",
        "etc/lib.wgsl", "lib\n",
        NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "app/main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "lib\nmain\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_include_once(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#include \"a.wgsl\"\n#include \"a.wgsl\"\nmain\n",
        "a.wgsl", "#pragma once\na\n",
        NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "a\nmain\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_include_missing(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "a\n#include \"nope.wgsl\"\n", NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_assert(out.code == NULL);
    test_str(out.error, "main.wgsl:2: cannot find include 'nope.wgsl'");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_include_recursive(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#include \"b.wgsl\"\n",
        "b.wgsl", "#include \"main.wgsl\"\n",
        NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "b.wgsl:1: recursive include of 'main.wgsl'");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_include_syntax_error(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "\n#include common.wgsl\n", NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "main.wgsl:2: #include expects \"file\"");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_define_expands(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#define N 4\nvar<private> a: array<f32, N>;\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "var<private> a: array<f32, 4>;\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_define_whole_identifiers(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "#define N 4\nN_MAX + N + aN + N2 + 1N\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "N_MAX + 4 + aN + N2 + 1N\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_define_nested(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "#define A B * 2\n#define B 3\nA\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "3 * 2\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_define_self_reference(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#define X X + 1\n#define P Q\n#define Q P\nX\nP\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "X + 1\nP\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_define_empty(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "#define E\na E b\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "a  b\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_define_in_include(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#include \"defs.wgsl\"\nSIZE\n",
        "defs.wgsl", "#define SIZE 64u\n",
        NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "64u\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_undef(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#define A 1\nA\n#undef A\nA\n#ifdef A\ndefined\n#endif\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "1\nA\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_function_macro_error(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "#define F(x) x\n", NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "main.wgsl:1: function-like macros are not supported");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_ifdef_else(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#ifdef A\na\n#else\nnot a\n#endif\nend\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "not a\nend\n");
    box_shader_output_fini(&out);

    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "A", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "a\nend\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_ifndef(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#ifndef QUALITY\n#define QUALITY 1\n#endif\nQUALITY\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "1\n");
    box_shader_output_fini(&out);

    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "QUALITY=3", .read = test_read, .ctx = files}, &out),
        0);
    test_str(out.code, "3\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_nested_conditionals(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl",
        "#ifdef A\n"
        "  #ifdef B\nab\n  #else\na\n  #endif // B\n"
        "#else\n"
        "  #ifdef B\nb\n  #endif\n"
        "#endif\n",
        NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "A,B", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "ab\n");
    box_shader_output_fini(&out);

    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "A", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "a\n");
    box_shader_output_fini(&out);

    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "B", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "b\n");
    box_shader_output_fini(&out);

    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_pipeline_defines(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "COUNT SCALE\n#ifdef SHADOW\nshadow\n#endif\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = " COUNT=8, SCALE=0.5 ;SHADOW", .read = test_read,
        .ctx = files}, &out), 0);
    test_str(out.code, "8 0.5\nshadow\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_same_file_two_pipelines(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "mesh.wgsl", "#ifdef DEPTH_ONLY\nfn fs() {}\n#else\nfn fs() -> vec4f {}\n#endif\n",
        NULL};
    box_shader_output_t a, b;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "mesh.wgsl", .defines = "DEPTH_ONLY", .read = test_read, .ctx = files}, &a), 0);
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "mesh.wgsl", .read = test_read, .ctx = files}, &b), 0);
    test_str(a.code, "fn fs() {}\n");
    test_str(b.code, "fn fs() -> vec4f {}\n");
    box_shader_output_fini(&a);
    box_shader_output_fini(&b);
    ecs_os_fini();
}

void Shader_invalid_define(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "a\n", NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "A,1B", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "main.wgsl: invalid define '1B'");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_error_in_included_file(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "s/main.wgsl", "a\n\n#include \"inc.wgsl\"\n",
        "s/inc.wgsl", "b\n\n\n#bogus\n",
        NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "s/main.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "s/inc.wgsl:4: unknown directive '#bogus'");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_unterminated_ifdef(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#ifdef A\n#include \"inc.wgsl\"\n#endif\n",
        "inc.wgsl", "x\n#ifndef X\ny\n",
        NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "A", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "inc.wgsl:2: #ifndef without #endif");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_else_without_ifdef(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "a\n#else\n", NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "main.wgsl:2: #else without #ifdef");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_endif_without_ifdef(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#ifdef A\n#include \"inc.wgsl\"\n#endif\n",
        "inc.wgsl", "#endif\n",
        NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .defines = "A", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "inc.wgsl:1: #endif without #ifdef");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_double_else(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {"main.wgsl", "#ifdef A\n#else\n#else\n#endif\n", NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "main.wgsl:3: #else after #else");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_skipped_directives_ignored(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "main.wgsl", "#ifdef A\n#bogus\n#include \"nope.wgsl\"\n#define B\n#endif\nB\n", NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "B\n");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_missing_file(void)
{
    // renderer/shader
    ecs_os_init();
    const char *files[] = {NULL};
    box_shader_output_t out;
    test_assert(box_shader_preprocess(&(box_shader_desc_t){
        .path = "nope.wgsl", .read = test_read, .ctx = files}, &out) != 0);
    test_str(out.error, "nope.wgsl: cannot read file");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_component_preprocessed(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("#define N 2\n#include \"inc.wgsl\"\n", f);
    fclose(f);
    char *inc = flecs_asprintf("%s/inc.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    f = fopen(inc, "w");
    fputs("fn n() -> u32 { return N; }\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_progress(world, 0);

    const ShaderStatus *status = ecs_get(world, e, ShaderStatus);
    test_assert(status != NULL);
    test_bool(status->ok, true);
    test_int(status->revision, 1);
    test_assert(status->error == NULL);

    ecs_os_free(path);
    ecs_os_free(inc);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_component_defines(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("#ifdef A\n#include \"missing.wgsl\"\n#endif\nB\n", f);
    fclose(f);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, Shader, {.path = path, .defines = "A,B=7"});
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Shader, {.path = path, .defines = "B=7"});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_log_set_level(-1);

    const ShaderStatus *with = ecs_get(world, e1, ShaderStatus);
    const ShaderStatus *without = ecs_get(world, e2, ShaderStatus);
    test_bool(with->ok, false);
    test_assert(with->error != NULL);
    test_assert(strstr(with->error, "missing.wgsl") != NULL);
    test_bool(without->ok, true);
    test_assert(without->error == NULL);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_unchanged_not_recompiled(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("a\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_set_again_recompiles(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("V\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path, .defines = "V=1"});
    ecs_progress(world, 0);
    box_render_sync(world);

    ecs_set(world, e, Shader, {.path = path, .defines = "V=2"});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, e, ShaderStatus)->revision, 2);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_file_change_recompiles(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("old\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_progress(world, 0);
    box_render_sync(world);

    f = fopen(path, "w");
    fputs("new code\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, e, ShaderStatus)->revision, 2);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_get(world, e, ShaderStatus)->revision, 2);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_include_change_recompiles(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    char *inc = flecs_asprintf("%s/inc.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("#include \"inc.wgsl\"\nmain\n", f);
    fclose(f);
    f = fopen(inc, "w");
    fputs("v1\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    f = fopen(inc, "w");
    fputs("version 2\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, e, ShaderStatus)->revision, 2);

    ecs_os_free(path);
    ecs_os_free(inc);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_poll_disabled(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("old\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_progress(world, 0);
    f = fopen(path, "w");
    fputs("new code\n", f);
    fclose(f);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_status_error(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/bad.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("a\n#ifdef A\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_log_set_level(-1);
    box_render_sync(world);

    const ShaderStatus *status = ecs_get(world, e, ShaderStatus);
    test_assert(status != NULL);
    test_bool(status->ok, false);
    test_int(status->revision, 0);
    char *expect = flecs_asprintf("%s:2: #ifdef without #endif", path);
    test_str(status->error, expect);

    ecs_os_free(expect);
    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_error_keeps_code(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("good\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_progress(world, 0);

    f = fopen(path, "w");
    fputs("#endif\n", f);
    fclose(f);
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_log_set_level(-1);
    box_render_sync(world);

    test_bool(ecs_get(world, e, ShaderStatus)->ok, false);
    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    f = fopen(path, "w");
    fputs("fixed code\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    const ShaderStatus *status = ecs_get(world, e, ShaderStatus);
    test_bool(status->ok, true);
    test_assert(status->error == NULL);
    test_int(status->revision, 2);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_missing_file_appears(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/late.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_log_set_level(-1);
    box_render_sync(world);
    test_bool(ecs_get(world, e, ShaderStatus)->ok, false);

    FILE *f = fopen(path, "w");
    fputs("late\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_bool(ecs_get(world, e, ShaderStatus)->ok, true);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_remove_removes_status(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("a\n", f);
    fclose(f);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, e, ShaderStatus));

    ecs_remove(world, e, Shader);
    test_assert(!ecs_has(world, e, ShaderStatus));
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, e, ShaderStatus));

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_engine_shaders_are_components(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *names[] = {
        "box.shader.builtin.blit", "box.shader.builtin.mesh", "box.shader.builtin.scatter"
    };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_lookup(world, names[i]);
        test_assert(e != 0);
        const Shader *shader = ecs_get(world, e, Shader);
        test_assert(shader != NULL);
        test_assert(shader->path != NULL);
        test_assert(!strncmp(shader->path, "etc/shaders/", 12));
    }

    ecs_fini(world);
}

void Shader_copy_owns_strings(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    char path[] = "nope.wgsl";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Shader, {.path = path, .defines = "A"});
    const Shader *s = ecs_get(world, e, Shader);
    test_assert(s->path != path);
    test_str(s->path, "nope.wgsl");
    path[0] = 'X';
    test_str(ecs_get(world, e, Shader)->path, "nope.wgsl");

    ecs_entity_t c = ecs_clone(world, 0, e, true);
    const Shader *a = ecs_get(world, e, Shader);
    const Shader *b = ecs_get(world, c, Shader);
    test_str(b->path, "nope.wgsl");
    test_str(b->defines, "A");
    test_assert(a->path != b->path);
    test_assert(a->defines != b->defines);

    ecs_add_id(world, e, ecs_new(world));
    test_str(ecs_get(world, e, Shader)->path, "nope.wgsl");
    ecs_set(world, e, Shader, {.path = "other.wgsl"});
    test_str(ecs_get(world, e, Shader)->path, "other.wgsl");
    test_assert(ecs_get(world, e, Shader)->defines == NULL);
    ecs_delete(world, c);

    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    ecs_log_set_level(-1);
    box_render_sync(world);
    const ShaderStatus *st = ecs_get(world, e, ShaderStatus);
    ecs_entity_t c2 = ecs_clone(world, 0, e, true);
    test_assert(ecs_get(world, c2, ShaderStatus)->error != st->error);
    test_str(ecs_get(world, c2, ShaderStatus)->error, st->error);

    ecs_log_set_level(-4);
    ecs_fini(world);
    ecs_log_set_level(-1);
    box_render_null_free(null);
}

void Shader_gpu_compiles_engine_shader(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 32, .resolution_scale = 2});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    test_bool(ecs_get(world, blit, ShaderStatus)->ok, true);

    ecs_fini(world);
}

void Shader_gpu_compiles_component(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("#ifdef RED\nconst R = 1.0;\n#else\nconst R = 0.0;\n#endif\n"
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(R, 0.0, 0.0, 1.0); }\n", f);
    fclose(f);

    ecs_entity_t s1 = ecs_new(world);
    ecs_set(world, s1, Shader, {.path = path, .defines = "RED"});
    ecs_entity_t s2 = ecs_new(world);
    ecs_set(world, s2, Shader, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_progress(world, 0);

    const ShaderStatus *st1 = ecs_get(world, s1, ShaderStatus);
    const ShaderStatus *st2 = ecs_get(world, s2, ShaderStatus);
    test_bool(st1->ok, true);
    test_assert(st1->error == NULL);
    test_bool(st2->ok, true);
    test_assert(st2->error == NULL);

    ecs_os_free(path);
    ecs_fini(world);
}

void Shader_gpu_invalid_code(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/bad.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("fn broken( -> {\n", f);
    fclose(f);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Shader, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 0);
    box_render_sync(world);

    const ShaderStatus *st = ecs_get(world, s, ShaderStatus);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "bad.wgsl:1: ") != NULL);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    test_bool(ecs_get(world, blit, ShaderStatus)->ok, true);

    ecs_os_free(path);
    ecs_fini(world);
}

void Shader_gpu_recompiles_on_change(void)
{
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/main.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("const A = 1.0;\n", f);
    fclose(f);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Shader, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    f = fopen(path, "w");
    fputs("const A = 2.0;\nconst B = 3.0;\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    ecs_progress(world, 0);
    const ShaderStatus *st = ecs_get(world, s, ShaderStatus);
    test_int(st->revision, 2);
    test_bool(st->ok, true);
    test_assert(st->error == NULL);

    ecs_os_free(path);
    ecs_fini(world);
}

void Shader_gpu_error_in_status(void)
{
    // renderer/shader_errors
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *path = flecs_asprintf("%s/invalid.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f = fopen(path, "w");
    fputs("#define X 1\nconst A = 1.0;\nfn f() -> f32 { return undefined_name; }\n", f);
    fclose(f);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Shader, {.path = path});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 0);

    const ShaderStatus *st = ecs_get(world, s, ShaderStatus);
    test_assert(st != NULL);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "invalid.wgsl:3: ") != NULL);
    test_assert(strstr(st->error, "undefined_name") != NULL);
    test_int(st->revision, 1);

    f = fopen(path, "w");
    fputs("const A = 2.0;\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_progress(world, 0);

    st = ecs_get(world, s, ShaderStatus);
    test_bool(st->ok, true);
    test_assert(st->error == NULL);
    test_int(st->revision, 2);

    ecs_os_free(path);
    ecs_fini(world);
}

void Shader_gpu_error_in_include(void)
{
    // renderer/shader_lines
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *inc = flecs_asprintf("%s/inc_lines.wgsl", ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f_inc = fopen(inc, "w");
    fputs("#pragma once\nconst B = 1.0;\n\nfn g() -> f32 { return undefined_name; }\n", f_inc);
    fclose(f_inc);
    char *main = flecs_asprintf("%s/main_lines.wgsl", ecs_singleton_get(world,
        AppStatus)->tmp_dir);
    FILE *f_main = fopen(main, "w");
    fputs("const A = 1.0;\n#include \"inc_lines.wgsl\"\nconst C = 1.0;\n", f_main);
    fclose(f_main);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Shader, {.path = main});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 0);

    const ShaderStatus *st = ecs_get(world, s, ShaderStatus);
    test_assert(st != NULL);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "inc_lines.wgsl:4: ") != NULL);
    test_assert(strstr(st->error, "undefined_name") != NULL);

    ecs_os_free(inc);
    ecs_os_free(main);
    ecs_fini(world);
}

void Shader_gpu_error_after_directives(void)
{
    // renderer/shader_lines
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = 0});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    char *main = flecs_asprintf("%s/dropped_lines.wgsl",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    FILE *f_main = fopen(main, "w");
    fputs("#define X 1\n#ifdef X\n#define Y 2\n#endif\n#ifndef X\n"
        "const Z = 1.0;\n#endif\nconst A = 1.0;\n"
        "fn f() -> f32 { return undefined_name; }\n", f_main);
    fclose(f_main);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Shader, {.path = main});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 0);

    const ShaderStatus *st = ecs_get(world, s, ShaderStatus);
    test_assert(st != NULL);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "dropped_lines.wgsl:9: ") != NULL);
    test_assert(strstr(st->error, "undefined_name") != NULL);

    ecs_os_free(main);
    ecs_fini(world);
}
