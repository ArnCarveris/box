/**
 * @file shader/modulator.c
 * @brief Generated include that splices the light modulator plugins into the mesh shader.
 *
 * Answers `#include "generated/light_modulators.wgsl"`: each plugin file is
 * preprocessed and its `box_light_modulate` renamed to its slot, plus a dispatch.
 * A plugin that fails to preprocess becomes the identity and reports on ShaderStatus.
 */

#include "shader_internal.h"
#include <stdio.h>

/** Longest generated line, the dispatch of one modulator */
#define BOX_MODULATOR_LINE_MAX (192)

/** Longest macro list a plugin is preprocessed with */
#define BOX_MODULATOR_MACRO_MAX (512)

/** Append the function that scales the light of one registration.
 *
 * @param world The world
 * @param shader Shader entity of the plugin
 * @param index Slot of the registration, which names its function
 * @param buf The generated source
 */
static void box_shader_modulator_body(
    ecs_world_t *world,
    ecs_entity_t shader,
    int32_t index,
    ecs_strbuf_t *buf)
{
    box_shader_result_t result = { NULL, NULL };
    const Shader *component = ecs_get(world, shader, Shader);
    if (component && component->path && component->path[0]) {
        const char *extra = component->macros ? component->macros : "";
        char macros[BOX_MODULATOR_MACRO_MAX];
        snprintf(macros, sizeof(macros),
            "BOX_MODULATOR_SPLICE=1,box_light_modulate=box_light_modulate_%d%s%s",
            index, extra[0] ? "," : "", extra);
        box_shader_preprocess(NULL, component->path, macros, &result);
    }

    if (result.source) {
        ecs_strbuf_appendstr(buf, result.source);
        ecs_strbuf_appendstr(buf, "\n");
    } else {
        char line[BOX_MODULATOR_LINE_MAX];
        snprintf(line, sizeof(line),
            "fn box_light_modulate_%d(p: vec3<f32>, n: vec3<f32>, l: vec3<f32>,\n",
            index);
        ecs_strbuf_appendstr(buf, line);
        ecs_strbuf_appendstr(buf,
            "    time: f32, params: LightModulatorParams) -> vec3<f32> {\n"
            "  return vec3<f32>(1.0);\n}\n");
    }

    box_shader_result_fini(&result);
}

char* box_shader_modulators_wgsl(
    ecs_world_t *world)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t count = 0;
    const box_modulator_t *entries = NULL;
    if (world) {
        entries = box_render_modulators(world, &count);
    }

    int32_t i;
    for (i = 0; i < count; i ++) {
        box_shader_modulator_body(world, entries[i].shader, i, &buf);
    }

    ecs_strbuf_appendstr(&buf,
        "fn box_light_modulate_all(mask: u32, p: vec3<f32>, n: vec3<f32>,\n"
        "    l: vec3<f32>, time: f32) -> vec3<f32> {\n"
        "  var result = vec3<f32>(1.0);\n");

    char line[BOX_MODULATOR_LINE_MAX];
    for (i = 0; i < count; i ++) {
        snprintf(line, sizeof(line),
            "  if ((mask & %uu) != 0u) { result = result *\n"
            "    box_light_modulate_%d(p, n, l, time, light_modulators.p[%d]); }\n",
            1u << i, i, i);
        ecs_strbuf_appendstr(&buf, line);
    }

    ecs_strbuf_appendstr(&buf, "  return result;\n}\n");

    return ecs_strbuf_get(&buf);
}

void box_shader_modulators_changed(
    ecs_world_t *world)
{
    int32_t count;
    box_render_modulators(world, &count);
    ecs_entity_t mesh = ecs_lookup(world, "box.shader.builtin.mesh");
    if (mesh) {
        box_shader_reload(world, mesh);
    }
}
