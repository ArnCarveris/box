/**
 * @file render/gpu_texture_graph_code.c
 * @brief WGSL code of the stages of the texture graphs of the webgpu backend.
 *
 * A stage fuses the node functions it computes into one compute shader: it loads the values of
 * its materialized inputs, binds the textures its sampling nodes sample, calls the functions in
 * order and stores the value of its root.
 */

#include "gpu_texture_graph.h"

#ifdef BOX_RENDER_WGPU

/** Get the WGSL name of a storage texture format of a stage.
 *
 * @param format The format
 * @return The name
 */
static const char* box_gpu_tg_format_name(
    WGPUTextureFormat format)
{
    return format == WGPUTextureFormat_R32Float ? "r32float"
        : format == BOX_GPU_TEXTURE_FORMAT ? "rgba8unorm" : "rgba16float";
}

/** Append text to the code of a stage, counting its lines.
 *
 * @param buf The code
 * @param line The line the text starts on, receives the line after it
 * @param text The text
 */
static void box_gpu_tg_append(
    ecs_strbuf_t *buf,
    int32_t *line,
    const char *text)
{
    const char *c;
    ecs_strbuf_appendstr(buf, text);
    for (c = text; *c; c ++) {
        *line += *c == '\n';
    }
}

/** Record the lines of the code of a stage that came from a node.
 *
 * @param lines The lines of the stage
 * @param first First line
 * @param last Last line
 * @param node The node, -1 for the shared code
 */
static void box_gpu_tg_lines(
    ecs_vec_t *lines,
    int32_t first,
    int32_t last,
    int32_t node)
{
    *ecs_vec_append_t(NULL, lines, box_gpu_tg_line_t) = (box_gpu_tg_line_t){
        .first = first, .last = last, .node = node
    };
}

/** Write the WGSL expression of input j of a node in a stage: the value of its node, or without
 * one the domain coordinates for a coords input and the constant in its parameters otherwise.
 *
 * @param buf The code
 * @param graph The graph
 * @param kind The kind of the node
 * @param node The node
 * @param j The input
 */
static void box_gpu_tg_input(
    ecs_strbuf_t *buf,
    const box_gpu_tg_graph_t *graph,
    const box_gpu_tg_kind_t *kind,
    int32_t node,
    int32_t j)
{
    const box_render_texture_graph_node_t *n = &graph->nodes[node];
    if ((uint32_t)j >= kind->input_count) {
        ecs_strbuf_appendstr(buf, "vec4f(0.0)");
    } else if (n->inputs[j] >= 0) {
        ecs_strbuf_append(buf, "node_%d", n->inputs[j]);
    } else if (kind->coords_inputs & (1u << j)) {
        ecs_strbuf_appendstr(buf, "coords");
    } else {
        ecs_strbuf_append(buf, "bitcast<vec4f>(box_texgraph_params[%uu])",
            (n->params + kind->param_size) / 16 + (uint32_t)j);
    }
}

/** Get the WGSL name of the value of a texture slot in a stage: node_i for node i, pass_k for
 * the k-th slot of the earlier passes.
 *
 * @param buf Receives the name
 * @param size Bytes of buf
 * @param n Nodes of the graph
 * @param slot The slot
 */
static void box_gpu_tg_slot_name(
    char *buf,
    ecs_size_t size,
    int32_t n,
    int32_t slot)
{
    if (slot < n) {
        ecs_os_snprintf(buf, size, "node_%d", slot);
    } else {
        ecs_os_snprintf(buf, size, "pass_%d", slot - n);
    }
}

/** Write the texture a stage binds for a slot, the 1x1 zero texture when it reads no such slot.
 *
 * @param buf The code
 * @param stage The stage
 * @param slot The slot, -1 for none
 */
static void box_gpu_tg_texture(
    ecs_strbuf_t *buf,
    const box_gpu_tg_stage_t *stage,
    int32_t slot)
{
    int32_t k, found = -1;
    for (k = 0; slot >= 0 && k < stage->input_count; k ++) {
        if (stage->inputs[k] == slot) {
            found = k;
        }
    }

    if (found >= 0) {
        ecs_strbuf_append(buf, ", box_texgraph_in%d", found);
    } else {
        ecs_strbuf_appendstr(buf, ", box_texgraph_none");
    }
}

/** Write the call of the function of a node in a stage for one of its passes.
 *
 * A neighbourhood function also gets the texel and the texture of each of its inputs, the
 * texture of an input without a node being a 1x1 zero texture; a function of more than one pass
 * gets the pass and the texture of the pass before it between the two.
 *
 * A sampling function gets the texture it samples last.
 *
 * @param buf The code
 * @param graph The graph
 * @param stage The stage
 * @param kind The kind of the node
 * @param node The node
 * @param pass The pass, the last one for the value of the node
 * @param prev Slot of the pass before, -1 for none
 */
static void box_gpu_tg_call(
    ecs_strbuf_t *buf,
    const box_gpu_tg_graph_t *graph,
    const box_gpu_tg_stage_t *stage,
    const box_gpu_tg_kind_t *kind,
    int32_t node,
    int32_t pass,
    int32_t prev)
{
    const box_render_texture_graph_node_t *n = &graph->nodes[node];
    int32_t j;
    ecs_strbuf_append(buf, "%s(box_texgraph_node_params(%uu), array<vec4f, 4>(",
        kind->function, n->params / 16);
    for (j = 0; j < BOX_TEXTURE_NODE_INPUTS; j ++) {
        if (j) {
            ecs_strbuf_appendstr(buf, ", ");
        }

        box_gpu_tg_input(buf, graph, kind, node, j);
    }

    ecs_strbuf_appendstr(buf, "), coords");
    if ((kind->flags & BOX_TEXTURE_NODE_NEIGHBOURHOOD) || box_gpu_tg_passes(kind) > 1) {
        ecs_strbuf_appendstr(buf, ", texel");
        if (box_gpu_tg_passes(kind) > 1) {
            ecs_strbuf_append(buf, ", %du", pass);
            box_gpu_tg_texture(buf, stage, prev);
        }

        for (j = 0; j < (int32_t)kind->input_count; j ++) {
            box_gpu_tg_texture(buf, stage, n->inputs[j]);
        }
    }

    if (kind->flags & BOX_TEXTURE_NODE_SAMPLES) {
        int32_t slot = 0;
        for (j = 0; j < stage->texture_count; j ++) {
            if (stage->textures[j] == n->texture) {
                slot = j;
            }
        }

        ecs_strbuf_append(buf, ", box_texgraph_tex%d", slot);
    }

    ecs_strbuf_appendstr(buf, ")");
}

char* box_gpu_tg_code(
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    box_gpu_tg_stage_t *stage,
    const int32_t *owner,
    const int32_t *first)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t line = 1, i, k, first_line, n = (int32_t)graph->desc.node_count;
    ecs_vec_t used;
    char text[256], name[32];
    ecs_vec_init_t(NULL, &used, uint32_t, 0);
    box_gpu_tg_append(&buf, &line, tg->prelude);
    box_gpu_tg_lines(&stage->lines, 1, line - 1, -1);
    box_gpu_tg_append(&buf, &line, "\n");
    for (i = 0; i <= stage->root; i ++) {
        const box_gpu_tg_kind_t *kind;
        bool seen = false;
        if (stage->pass >= 0 ? i != stage->root : owner[i] != stage->root) {
            continue;
        }

        for (k = 0; k < ecs_vec_count(&used); k ++) {
            seen = seen || *ecs_vec_get_t(&used, uint32_t, k) == graph->nodes[i].kind;
        }

        if (seen) {
            continue;
        }

        *ecs_vec_append_t(NULL, &used, uint32_t) = graph->nodes[i].kind;
        kind = box_gpu_tg_kind(tg, graph->nodes[i].kind);
        first_line = line;
        box_gpu_tg_append(&buf, &line, kind->source);
        if (kind->source[0] && kind->source[ecs_os_strlen(kind->source) - 1] != '\n') {
            box_gpu_tg_append(&buf, &line, "\n");
        }

        box_gpu_tg_lines(&stage->lines, first_line, line - 1, i);
        box_gpu_tg_append(&buf, &line, "\n");
    }

    ecs_os_snprintf(text, ECS_SIZEOF(text),
        "@group(0) @binding(2) var box_texgraph_out: texture_storage_2d<%s, write>;\n"
        "@group(0) @binding(3) var box_texgraph_none: texture_2d<f32>;\n",
        box_gpu_tg_format_name(stage->format));
    box_gpu_tg_append(&buf, &line, text);
    for (k = 0; k < stage->input_count; k ++) {
        ecs_os_snprintf(text, ECS_SIZEOF(text),
            "@group(0) @binding(%d) var box_texgraph_in%d: texture_2d<f32>;\n",
            BOX_GPU_TG_FIRST_INPUT + k, k);
        box_gpu_tg_append(&buf, &line, text);
    }

    for (k = 0; k < stage->texture_count; k ++) {
        ecs_os_snprintf(text, ECS_SIZEOF(text),
            "@group(0) @binding(%d) var box_texgraph_tex%d: texture_2d<f32>;\n",
            BOX_GPU_TG_FIRST_TEXTURE + k, k);
        box_gpu_tg_append(&buf, &line, text);
    }

    box_gpu_tg_append(&buf, &line,
        "\n"
        "@compute @workgroup_size(8, 8, 1)\n"
        "fn main(@builtin(global_invocation_id) id: vec3u) {\n"
        "    if (id.x >= box_texgraph_stage.size.x || id.y >= box_texgraph_stage.size.y) {\n"
        "        return;\n"
        "    }\n"
        "    let texel = vec2i(id.xy);\n"
        "    let coords = box_texgraph_coords(texel);\n");
    for (k = 0; k < stage->input_count; k ++) {
        int32_t in = stage->inputs[k];
        box_gpu_tg_slot_name(name, ECS_SIZEOF(name), n, in);
        ecs_os_snprintf(text, ECS_SIZEOF(text),
            "    let %s = textureLoad(box_texgraph_in%d, texel, 0);\n", name, k);
        box_gpu_tg_lines(&stage->lines, line, line, in < n ? in : stage->root);
        box_gpu_tg_append(&buf, &line, text);
    }

    if (stage->pass >= 0) {
        const box_gpu_tg_kind_t *kind = box_gpu_tg_kind(tg, graph->nodes[stage->root].kind);
        box_gpu_tg_slot_name(name, ECS_SIZEOF(name), n, stage->slot);
        ecs_strbuf_append(&buf, "    let %s = ", name);
        box_gpu_tg_call(&buf, graph, stage, kind, stage->root, stage->pass,
            stage->pass > 0 ? stage->slot - 1 : -1);
        ecs_strbuf_appendstr(&buf, ";\n");
        box_gpu_tg_lines(&stage->lines, line, line, stage->root);
        line ++;
    }

    for (i = 0; stage->pass < 0 && i <= stage->root; i ++) {
        const box_gpu_tg_kind_t *kind;
        int32_t passes;
        if (owner[i] != stage->root) {
            continue;
        }

        kind = box_gpu_tg_kind(tg, graph->nodes[i].kind);
        passes = box_gpu_tg_passes(kind);
        ecs_strbuf_append(&buf, "    let node_%d = ", i);
        box_gpu_tg_call(&buf, graph, stage, kind, i, passes - 1,
            passes > 1 ? first[i] + passes - 2 : -1);
        ecs_strbuf_appendstr(&buf, ";\n");
        box_gpu_tg_lines(&stage->lines, line, line, i);
        line ++;
    }

    box_gpu_tg_slot_name(name, ECS_SIZEOF(name), n, stage->slot);
    ecs_os_snprintf(text, ECS_SIZEOF(text), stage->output
        ? "    textureStore(box_texgraph_out, texel, box_texgraph_encode(%s));\n"
        : "    textureStore(box_texgraph_out, texel, %s);\n", name);
    box_gpu_tg_lines(&stage->lines, line, line, stage->root);
    box_gpu_tg_append(&buf, &line, text);
    box_gpu_tg_append(&buf, &line, "}\n");
    ecs_vec_fini_t(NULL, &used, uint32_t);

    return ecs_strbuf_get(&buf);
}

#endif
