/**
 * @file shader/layers.c
 * @brief Generated include that composes the material layers into the mesh shader.
 *
 * `#include "generated/layers.wgsl"` is answered from the layer registry instead
 * of from a file, which is how the runtime recomposition of the mesh shader
 * became an ordinary preprocessor run.
 */

#include "shader_internal.h"
#include <stdio.h>

/** Longest generated line, the dispatch of one layer */
#define BOX_SHADER_LINE_MAX (128)

#define BOX_LAYER_MACRO_MAX (512)

static void box_shader_layer_body(
    ecs_world_t *world,
    const MaterialLayer *layer,
    int32_t index,
    ecs_strbuf_t *buf)
{
    if (layer && layer->shader) {
        box_shader_result_t result = { NULL, NULL };
        const Shader *component = ecs_get(world, layer->shader, Shader);
        if (component && component->path && component->path[0]) {
            const char *extra = component->macros ? component->macros : "";
            char macros[BOX_LAYER_MACRO_MAX];
            snprintf(macros, sizeof(macros),
                "BOX_LAYER_SPLICE=1,BOX_LAYER_INDEX=%d,box_material_layer=layer_%d%s%s",
                index, index, extra[0] ? "," : "", extra);
            box_shader_preprocess(NULL, component->path, macros, &result);
        }

        if (result.source) {
            ecs_strbuf_appendstr(buf, result.source);
            ecs_strbuf_appendstr(buf, "\n");
            box_shader_result_fini(&result);
            return;
        }

        box_shader_result_fini(&result);
    }

    char line[BOX_SHADER_LINE_MAX];
    snprintf(line, sizeof(line),
        "fn layer_%d(s: ptr<function, Surface>, p: LayerParams) {}\n", index);
    ecs_strbuf_appendstr(buf, line);
}

char* box_shader_layers_wgsl(
    ecs_world_t *world)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t count = 0;
    const box_layer_t *layers = NULL;
    if (world) {
        layers = box_render_layers(world, &count);
    }

    char line[BOX_SHADER_LINE_MAX];
    int32_t i;
    for (i = 0; i < count; i ++) {
        const MaterialLayer *layer = ecs_get(world, layers[i].entity, MaterialLayer);
        box_shader_layer_body(world, layer, i, &buf);
    }

    ecs_strbuf_appendstr(&buf,
        "fn apply_layers(s: ptr<function, Surface>, mask: u32) {\n"
        "  for (var slot = 0u; slot < u.material_layer_count; slot++) {\n"
        "    let packed = u.material_layers[slot / 4u];\n"
        "    let index = packed[slot % 4u];\n"
        "    switch index {\n");
    for (i = 0; i < count; i ++) {
        snprintf(line, sizeof(line),
            "      case %du: { if ((mask & %uu) != 0u) { layer_%d(s, layer_params.p[%d]); } }\n",
            i, 1u << i, i, i);
        ecs_strbuf_appendstr(&buf, line);
    }

    ecs_strbuf_appendstr(&buf,
        "      default: {}\n"
        "    }\n"
        "  }\n"
        "}\n");

    return ecs_strbuf_get(&buf);
}

char* box_shader_generated(
    ecs_world_t *world,
    const char *name)
{
    char *result = NULL;
    if (!strcmp(name, "layers.wgsl")) {
        result = box_shader_layers_wgsl(world);
    } else if (!strcmp(name, "light_modulators.wgsl")) {
        result = box_shader_modulators_wgsl(world);
    }

    return result;
}
