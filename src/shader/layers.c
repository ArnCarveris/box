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
        snprintf(line, sizeof(line),
            "fn layer_%d(s: ptr<function, Surface>, p: LayerParams) {\n", i);
        ecs_strbuf_appendstr(&buf, line);
        ecs_strbuf_appendstr(&buf, layer && layer->wgsl ? layer->wgsl : "");
        ecs_strbuf_appendstr(&buf, "\n}\n");
    }

    ecs_strbuf_appendstr(&buf, "fn apply_layers(s: ptr<function, Surface>, mask: u32) {\n");
    for (i = 0; i < count; i ++) {
        snprintf(line, sizeof(line),
            "  if ((mask & %uu) != 0u) { layer_%d(s, layer_params.p[%d]); }\n",
            1u << i, i, i);
        ecs_strbuf_appendstr(&buf, line);
    }

    ecs_strbuf_appendstr(&buf, "}\n");

    return ecs_strbuf_get(&buf);
}

char* box_shader_generated(
    ecs_world_t *world,
    const char *name)
{
    char *result = NULL;
    if (!strcmp(name, "layers.wgsl")) {
        result = box_shader_layers_wgsl(world);
    }

    return result;
}
