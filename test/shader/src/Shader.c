/**
 * @file Shader.c
 * @brief Tests for the Shader component, its status and the commands it sends.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/shader/shader_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static int32_t shader_errors;
static ecs_os_api_log_t shader_log_prev;

/** Log callback that counts errors instead of printing them.
 *
 * @param level Log level, -3 for an error
 * @param file Source file of the message
 * @param line Source line of the message
 * @param msg The message
 */
static void shader_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    shader_errors += level == -3;
}

/** Source a shader handle carries in the packet, NULL when it is not in it.
 *
 * @param handle The shader handle
 * @return The source
 */
static const char* shader_source(
    uint32_t handle)
{
    const char *result = NULL;
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        box_shader_cmd_t *cmd = box_vec_at(&packet.shader_create, i);
        if (cmd->handle == handle) {
            result = cmd->source;
        }
    }

    return result;
}

/** Release the sources a packet carries and clear it.
 */
static void shader_drain(void)
{
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_reset(&packet);
}

void Shader_setup(void)
{
    shader_errors = 0;
    w = ecs_init();
    shader_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = shader_log;
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Shader_teardown(void)
{
    shader_drain();
    box_packet_fini(&packet);
    ecs_fini(w);
    ecs_os_api.log_ = shader_log_prev;
}

void Shader_builtin_shaders_are_entities_under_builtin(void)
{
    // effects/shader_component
    ecs_entity_t mesh = ecs_lookup(w, "box.shader.builtin.mesh");
    ecs_entity_t cull = ecs_lookup(w, "box.shader.builtin.cull");
    test_assert(mesh != 0);
    test_assert(cull != 0);

    const Shader *shader = ecs_get(w, mesh, Shader);
    test_assert(shader != NULL);
    test_str(shader->path, "etc/shaders/mesh.wgsl");
    const ShaderStatus *status = ecs_get(w, mesh, ShaderStatus);
    test_assert(status != NULL);
    test_bool(status->ok, true);
    test_str(status->error, "");
    test_int(shader_errors, 0);
}

void Shader_builtin_mesh_shader_reaches_the_packet(void)
{
    // effects/shader_component
    box_render_extract(w, &packet);

    test_int(packet.shader_create.count, BOX_SHADER_BUILTIN_MAX + 1);
    const char *source = shader_source(BOX_SHADER_MESH);
    test_assert(source != NULL);
    test_assert(strstr(source, "@vertex fn vs_main") != NULL);
    test_assert(strstr(source, "fn apply_layers(") != NULL);
    test_assert(strstr(source, "#include") == NULL);
    test_assert(shader_source(BOX_SHADER_BLUR) != NULL);
}

void Shader_setting_a_shader_sends_a_command(void)
{
    // effects/shader_component
    box_render_extract(w, &packet);
    shader_drain();

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Shader, { .path = "etc/simple.wgsl" });
    box_render_extract(w, &packet);

    test_int(packet.shader_create.count, 1);
    box_shader_cmd_t *cmd = box_vec_at(&packet.shader_create, 0);
    test_assert(cmd->handle >= BOX_SHADER_BUILTIN_MAX);
    test_str(cmd->source, "const simple: i32 = 1;\n");
}

void Shader_changing_the_path_sends_the_new_source(void)
{
    // effects/shader_component
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Shader, { .path = "etc/simple.wgsl" });
    box_render_extract(w, &packet);
    uint32_t handle = ecs_get(w, e, ShaderImpl)->handle;
    test_str(shader_source(handle), "const simple: i32 = 1;\n");
    shader_drain();

    ecs_set(w, e, Shader, { .path = "etc/other.wgsl" });
    box_render_extract(w, &packet);

    test_int(packet.shader_create.count, 1);
    test_uint(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, 0))->handle, handle);
    test_str(shader_source(handle), "const other: i32 = 2;\n");
}

void Shader_unchanged_shader_is_sent_once(void)
{
    // effects/shader_component
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Shader, { .path = "etc/simple.wgsl" });
    box_render_extract(w, &packet);
    test_int(packet.shader_create.count, BOX_SHADER_BUILTIN_MAX + 2);
    shader_drain();

    box_render_extract(w, &packet);

    test_int(packet.shader_create.count, 0);
}

void Shader_failing_file_keeps_the_previous_shader(void)
{
    // effects/shader_component
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Shader, { .path = "etc/simple.wgsl" });
    box_render_extract(w, &packet);
    uint32_t handle = ecs_get(w, e, ShaderImpl)->handle;
    shader_drain();

    ecs_set(w, e, Shader, { .path = "etc/broken.wgsl" });
    box_render_extract(w, &packet);

    test_int(packet.shader_create.count, 0);
    test_int(shader_errors, 1);
    const ShaderStatus *status = ecs_get(w, e, ShaderStatus);
    test_bool(status->ok, false);
    test_uint(ecs_get(w, e, ShaderImpl)->handle, handle);
}

void Shader_status_holds_the_error_with_file_and_line(void)
{
    // effects/shader_component
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Shader, { .path = "etc/bad_outer.wgsl" });

    const ShaderStatus *status = ecs_get(w, e, ShaderStatus);
    test_assert(status != NULL);
    test_bool(status->ok, false);
    test_str(status->error, "etc/bad_inner.wgsl:3: unknown directive");
    test_int(shader_errors, 1);
}

void Shader_macros_of_the_component_reach_the_source(void)
{
    // effects/shader_component
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Shader, { .path = "etc/cond.wgsl", .macros = "FLAG=1,NAMED=0" });
    box_render_extract(w, &packet);

    const char *source = shader_source(ecs_get(w, e, ShaderImpl)->handle);
    test_assert(source != NULL);
    test_assert(strstr(source, "const chosen: i32 = 1;") != NULL);
    test_assert(strstr(source, "const named: i32 = 1;") != NULL);
}

void Shader_mesh_shader_carries_the_material_layers(void)
{
    // effects/shader_component
    box_render_extract(w, &packet);
    test_assert(strstr(shader_source(BOX_SHADER_MESH), "fn layer_0(") == NULL);
    shader_drain();

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);

    const char *source = shader_source(BOX_SHADER_MESH);
    test_assert(source != NULL);
    test_assert(strstr(source, "fn layer_0(") != NULL);
    test_assert(strstr(source, "fn layer_3(") != NULL);
    test_assert(strstr(source, "if ((mask & 2u) != 0u) { layer_1(s, layer_params.p[1]); }")
        != NULL);
}

void Shader_macro_change_reloads_the_mesh_shader(void)
{
    // effects/shader_component
    box_render_extract(w, &packet);
    test_assert(strstr(shader_source(BOX_SHADER_MESH), "color = sn * 0.5") == NULL);
    shader_drain();

    ecs_entity_t mesh = ecs_lookup(w, "box.shader.builtin.mesh");
    ecs_set(w, mesh, Shader, { .path = "etc/shaders/mesh.wgsl",
        .macros = "DEBUG_NORMALS=1" });
    box_render_extract(w, &packet);

    test_int(packet.shader_create.count, 1);
    const char *source = shader_source(BOX_SHADER_MESH);
    test_assert(source != NULL);
    test_assert(strstr(source, "color = sn * 0.5") != NULL);
}
