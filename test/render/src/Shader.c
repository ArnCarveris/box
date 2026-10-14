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

void Shader_plain_source(void) {
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

void Shader_include_relative(void) {
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

void Shader_include_nested(void) {
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

void Shader_include_search_path(void) {
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

void Shader_include_once(void) {
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

void Shader_include_missing(void) {
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

void Shader_include_recursive(void) {
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

void Shader_include_syntax_error(void) {
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

void Shader_define_expands(void) {
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

void Shader_define_whole_identifiers(void) {
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

void Shader_define_nested(void) {
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

void Shader_define_self_reference(void) {
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

void Shader_define_empty(void) {
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

void Shader_define_in_include(void) {
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

void Shader_undef(void) {
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

void Shader_function_macro_error(void) {
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

void Shader_ifdef_else(void) {
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

void Shader_ifndef(void) {
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

void Shader_nested_conditionals(void) {
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

void Shader_pipeline_defines(void) {
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

void Shader_same_file_two_pipelines(void) {
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

void Shader_invalid_define(void) {
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

void Shader_error_in_included_file(void) {
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

void Shader_unterminated_ifdef(void) {
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

void Shader_else_without_ifdef(void) {
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

void Shader_endif_without_ifdef(void) {
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

void Shader_double_else(void) {
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

void Shader_skipped_directives_ignored(void) {
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

void Shader_files_listed(void) {
    // renderer/shader
    ecs_os_init();
    const char *files[] = {
        "s/main.wgsl", "#include \"a.wgsl\"\n#include \"b.wgsl\"\n#include \"a.wgsl\"\n",
        "s/a.wgsl", "a\n",
        "s/b.wgsl", "#include \"a.wgsl\"\n",
        NULL};
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "s/main.wgsl", .read = test_read, .ctx = files}, &out), 0);
    test_str(out.code, "a\na\na\n");
    test_int(ecs_vec_count(&out.files), 3);
    test_str(ecs_vec_get_t(&out.files, box_shader_file_t, 0)->path, "s/main.wgsl");
    test_str(ecs_vec_get_t(&out.files, box_shader_file_t, 1)->path, "s/a.wgsl");
    test_str(ecs_vec_get_t(&out.files, box_shader_file_t, 2)->path, "s/b.wgsl");
    box_shader_output_fini(&out);
    ecs_os_fini();
}

void Shader_missing_file(void) {
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

void Shader_engine_shader_from_disk(void) {
    // renderer/shader
    ecs_os_init();
    char *path = box_shader_locate("etc/shaders/blit.wgsl");
    test_assert(path != NULL);
    box_shader_output_t out;
    test_int(box_shader_preprocess(&(box_shader_desc_t){
        .path = "etc/shaders/blit.wgsl"}, &out), 0);
    test_assert(strstr(out.code, "@fragment fn fs") != NULL);
    test_assert(strstr(out.code, "struct FullscreenOut") != NULL);
    test_assert(strstr(out.code, "#") == NULL);
    test_int(ecs_vec_count(&out.files), 2);
    test_str(ecs_vec_get_t(&out.files, box_shader_file_t, 0)->path, path);
    test_assert(ecs_vec_get_t(&out.files, box_shader_file_t, 0)->stamp.sec > 0);
    test_assert(ecs_vec_get_t(&out.files, box_shader_file_t, 1)->stamp.size > 0);
    box_shader_output_fini(&out);
    ecs_os_free(path);
    ecs_os_fini();
}

void Shader_component_sends_code(void) {
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
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->kind, BOX_RENDER_SHADER);
    test_int(r->op, BOX_RENDER_CREATE);
    test_assert(r->handle >= BOX_RENDER_SHADER_FIRST);
    const box_render_shader_desc_t *desc = box_render_packet_data(&null->last, r->data);
    const char *code = (const char*)(desc + 1);
    test_int(desc->code_size, strlen("fn n() -> u32 { return 2; }\n") + 1);
    test_str(code, "fn n() -> u32 { return 2; }\n");
    test_str(code + desc->code_size, path);
    test_int(r->size, sizeof(box_render_shader_desc_t) + desc->code_size + desc->label_size);

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

void Shader_component_defines(void) {
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
    fputs("#ifdef A\na B\n#endif\n", f);
    fclose(f);

    ecs_entity_t e1 = ecs_new(world);
    ecs_set(world, e1, Shader, {.path = path, .defines = "A,B=7"});
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e2, Shader, {.path = path, .defines = "B=7"});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 2);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    int32_t i, with = 0, without = 0;
    for (i = 0; i < 2; i ++) {
        const box_render_shader_desc_t *desc = box_render_packet_data(&null->last, r[i].data);
        with += !strcmp((const char*)(desc + 1), "a 7\n");
        without += !strcmp((const char*)(desc + 1), "");
    }

    test_int(with, 1);
    test_int(without, 1);
    test_assert(r[0].handle != r[1].handle);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_unchanged_not_sent(void) {
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

    test_int(null->packets, 3);
    test_int(null->resources, 1);
    test_int(ecs_vec_count(&null->last.resources), 0);
    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_set_again_recompiles(void) {
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
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    ecs_set(world, e, Shader, {.path = path, .defines = "V=2"});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->kind, BOX_RENDER_SHADER);
    test_int(r->op, BOX_RENDER_UPDATE);
    test_uint(r->handle, handle);
    const box_render_shader_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_str((const char*)(desc + 1), "2\n");
    test_int(ecs_get(world, e, ShaderStatus)->revision, 2);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_file_change_recompiles(void) {
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
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    f = fopen(path, "w");
    fputs("new code\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->op, BOX_RENDER_UPDATE);
    test_uint(r->handle, handle);
    const box_render_shader_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_str((const char*)(desc + 1), "new code\n");
    test_int(ecs_get(world, e, ShaderStatus)->revision, 2);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.resources), 0);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_include_change_recompiles(void) {
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
    test_int(null->resources, 1);

    f = fopen(inc, "w");
    fputs("version 2\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->op, BOX_RENDER_UPDATE);
    const box_render_shader_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_str((const char*)(desc + 1), "version 2\nmain\n");
    test_int(ecs_get(world, e, ShaderStatus)->revision, 2);

    ecs_os_free(path);
    ecs_os_free(inc);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_poll_disabled(void) {
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

    test_int(null->resources, 1);
    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_status_error(void) {
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

    test_int(null->resources, 0);
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

void Shader_error_keeps_code(void) {
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

    test_int(null->resources, 1);
    test_bool(ecs_get(world, e, ShaderStatus)->ok, false);
    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    f = fopen(path, "w");
    fputs("fixed code\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(null->resources, 2);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->op, BOX_RENDER_UPDATE);
    const box_render_shader_desc_t *desc = box_render_packet_data(&null->last, r->data);
    test_str((const char*)(desc + 1), "fixed code\n");
    const ShaderStatus *status = ecs_get(world, e, ShaderStatus);
    test_bool(status->ok, true);
    test_assert(status->error == NULL);
    test_int(status->revision, 2);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_missing_file_appears(void) {
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
    test_int(null->resources, 0);
    test_bool(ecs_get(world, e, ShaderStatus)->ok, false);

    FILE *f = fopen(path, "w");
    fputs("late\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(null->resources, 1);
    test_bool(ecs_get(world, e, ShaderStatus)->ok, true);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_remove_destroys(void) {
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
    box_render_sync(world);
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    ecs_remove(world, e, Shader);
    test_assert(!ecs_has(world, e, ShaderStatus));
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->kind, BOX_RENDER_SHADER);
    test_int(r->op, BOX_RENDER_DESTROY);
    test_uint(r->handle, handle);

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->resources, 2);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_delete_destroys(void) {
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
    box_render_sync(world);
    uint32_t handle = ecs_vec_first_t(&null->last.resources, box_render_resource_t)->handle;

    ecs_delete(world, e);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.resources), 1);
    const box_render_resource_t *r = ecs_vec_first(&null->last.resources);
    test_int(r->op, BOX_RENDER_DESTROY);
    test_uint(r->handle, handle);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_new_backend_resends(void) {
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
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(null->resources, 1);

    box_render_null_t *second = box_render_null_new();
    backend = box_render_null_backend(second);
    box_render_set_backend(world, &backend);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(second->resources, 1);
    const box_render_resource_t *r = ecs_vec_first(&second->last.resources);
    test_int(r->kind, BOX_RENDER_SHADER);
    test_int(r->op, BOX_RENDER_CREATE);
    const box_render_shader_desc_t *desc = box_render_packet_data(&second->last, r->data);
    test_str((const char*)(desc + 1), "a\n");
    test_int(ecs_get(world, e, ShaderStatus)->revision, 1);

    ecs_os_free(path);
    ecs_fini(world);
    box_render_null_free(null);
    box_render_null_free(second);
}

void Shader_engine_shader_not_sent_to_null(void) {
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    test_assert(blit != 0);
    test_str(ecs_get(world, blit, Shader)->path, "etc/shaders/blit.wgsl");
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(null->resources, 0);
    test_assert(!ecs_has(world, blit, ShaderStatus));

    ecs_fini(world);
    box_render_null_free(null);
}

void Shader_copy_owns_strings(void) {
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

void Shader_gpu_compiles_engine_shader(void) {
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

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shaders, 1);
    test_int(stats.compiles, 1);
    test_int(stats.shader_errors, 0);
    test_int(stats.errors, 0);
    test_int(stats.frames, 2);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    test_bool(ecs_get(world, blit, ShaderStatus)->ok, true);

    ecs_fini(world);
}

void Shader_gpu_compiles_component(void) {
    // renderer/shader
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
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
    ecs_set(world, e, Surface, {.width = 16, .height = 16});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shaders, 3);
    test_int(stats.compiles, 3);
    test_int(stats.shader_errors, 0);
    test_int(stats.errors, 0);

    ecs_delete(world, s2);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_gpu_stats(world, &stats);
    test_int(stats.shaders, 2);

    ecs_os_free(path);
    ecs_fini(world);
}

void Shader_gpu_invalid_code(void) {
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
    ecs_set(world, e, Surface, {.width = 16, .height = 16});
    ecs_log_set_level(-4);
    ecs_progress(world, 0);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shaders, 1);
    test_int(stats.compiles, 1);
    test_int(stats.shader_errors, 1);
    test_int(stats.errors, 0);
    test_int(stats.frames, 2);

    ecs_os_free(path);
    ecs_fini(world);
}

void Shader_gpu_recompiles_on_change(void) {
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
    ecs_set(world, e, Surface, {.width = 16, .height = 16});
    ecs_progress(world, 0);
    box_render_sync(world);

    f = fopen(path, "w");
    fputs("const A = 2.0;\nconst B = 3.0;\n", f);
    fclose(f);
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    test_assert(box_render_gpu_stats(world, &stats));
    test_int(stats.shaders, 2);
    test_int(stats.compiles, 3);
    test_int(stats.shader_errors, 0);
    test_int(ecs_get(world, s, ShaderStatus)->revision, 2);

    ecs_os_free(path);
    ecs_fini(world);
}
