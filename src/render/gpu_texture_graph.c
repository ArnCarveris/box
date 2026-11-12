/**
 * @file render/gpu_texture_graph.c
 * @brief Evaluation of the texture graphs on the webgpu backend: stages, code and programs.
 *
 * A graph is cut into stages that each fuse pointwise nodes into one compute shader ending in
 * a materialized texture; programs of stages are shared by the graphs of one structure.
 *
 * Parameters live in one storage buffer per graph, so editing one only writes the buffer. Graphs
 * evaluate before the mip levels and passes of their packet, and report their first outcome.
 */

#include "gpu_texture_graph.h"

#ifdef BOX_RENDER_WGPU

/** Add bytes to a 64 bit FNV-1a hash.
 *
 * @param hash The hash so far
 * @param data The bytes
 * @param size Number of bytes
 * @return The hash with the bytes
 */
static uint64_t box_gpu_tg_hash(
    uint64_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    size_t i;
    for (i = 0; i < size; i ++) {
        hash ^= bytes[i];
        hash *= BOX_GPU_TG_FNV_PRIME;
    }

    return hash;
}

/** Get the texture graph state, creating it on first use.
 *
 * @param gpu The backend
 * @return The state
 */
static box_gpu_texgraphs_t* box_gpu_tg_get(
    box_gpu_t *gpu)
{
    box_gpu_texgraphs_t *tg = gpu->texture_graphs;
    if (tg) {
        goto done;
    }

    tg = ecs_os_calloc_t(box_gpu_texgraphs_t);
    ecs_map_init(&tg->kinds, NULL);
    ecs_map_init(&tg->graphs, NULL);
    ecs_map_init(&tg->programs, NULL);
    ecs_map_init(&tg->stages, NULL);
    ecs_vec_init_t(NULL, &tg->layouts, box_gpu_tg_layout_t*, 0);
    ecs_vec_init_t(NULL, &tg->pool, box_gpu_tg_pooled_t*, 0);
    ecs_vec_init_t(NULL, &tg->reads, box_gpu_tg_read_t*, 0);
    tg->next_shader = BOX_GPU_TEXTURE_GRAPH_SHADER_FIRST;
    tg->none = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = WGPUTextureFormat_RGBA16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    tg->none_view = wgpuTextureCreateView(tg->none, NULL);
    gpu->texture_graphs = tg;
done:
    return tg;
}

/** Get a node kind by id.
 *
 * @param tg The graph state
 * @param id Id of the kind
 * @return The kind, NULL when the backend has none of the id
 */
static box_gpu_tg_kind_t* box_gpu_tg_kind(
    box_gpu_texgraphs_t *tg,
    uint32_t id)
{
    return ecs_map_get_deref(&tg->kinds, box_gpu_tg_kind_t, id);
}

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

/** Queue the outcome of a graph for the main thread, once per serial.
 *
 * @param gpu The backend
 * @param graph The graph
 * @param error Why it failed, NULL when its image was written
 * @param node Node the error came from, -1 for none
 */
static void box_gpu_tg_report(
    box_gpu_t *gpu,
    box_gpu_tg_graph_t *graph,
    const char *error,
    int32_t node)
{
    if (graph->reported == graph->desc.serial) {
        goto done;
    }

    graph->reported = graph->desc.serial;
    ecs_os_mutex_lock(gpu->lock);
    *ecs_vec_append_t(NULL, &gpu->graph_results, box_render_texture_graph_result_t) =
        (box_render_texture_graph_result_t){
            .handle = graph->handle,
            .serial = graph->desc.serial,
            .node = node,
            .error = error ? ecs_os_strdup(error) : NULL
        };
    ecs_os_mutex_unlock(gpu->lock);
done:
    return;
}

/** Get the layouts of the stages with a number of inputs and an output format, creating them on
 * first use.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param inputs Materialized inputs
 * @param format Format of the storage texture
 * @return The layouts
 */
static box_gpu_tg_layout_t* box_gpu_tg_layout(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    int32_t inputs,
    WGPUTextureFormat format)
{
    WGPUBindGroupLayoutEntry entries[BOX_GPU_TG_FIRST_INPUT + BOX_GPU_TG_INPUTS];
    box_gpu_tg_layout_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&tg->layouts); i ++) {
        box_gpu_tg_layout_t *l = *ecs_vec_get_t(&tg->layouts, box_gpu_tg_layout_t*, i);
        if (l->inputs == inputs && l->format == format) {
            result = l;
            goto done;
        }
    }

    ecs_os_memset(entries, 0, ECS_SIZEOF(entries));
    entries[0] = (WGPUBindGroupLayoutEntry){
        .binding = 0,
        .visibility = WGPUShaderStage_Compute,
        .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
    };
    entries[1] = (WGPUBindGroupLayoutEntry){
        .binding = 1,
        .visibility = WGPUShaderStage_Compute,
        .buffer = { .type = WGPUBufferBindingType_Uniform }
    };
    entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Compute,
        .storageTexture = {
            .access = WGPUStorageTextureAccess_WriteOnly,
            .format = format,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
    for (i = 3; i < BOX_GPU_TG_FIRST_INPUT + inputs; i ++) {
        entries[i] = (WGPUBindGroupLayoutEntry){
            .binding = (uint32_t)i,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        };
    }

    result = ecs_os_calloc_t(box_gpu_tg_layout_t);
    result->inputs = inputs;
    result->format = format;
    result->group = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = (size_t)(BOX_GPU_TG_FIRST_INPUT + inputs),
            .entries = entries
        });
    result->pipeline = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &result->group
        });
    *ecs_vec_append_t(NULL, &tg->layouts, box_gpu_tg_layout_t*) = result;
done:
    return result;
}

/** Compute the key of the program a graph needs: its structure, the kinds of its nodes, the
 * format its output stage writes, its forced boundaries and the shared code.
 *
 * @param tg The graph state
 * @param graph The graph
 * @return The key, never 0
 */
static uint64_t box_gpu_tg_key(
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph)
{
    uint64_t result = BOX_GPU_TG_FNV_BASIS;
    WGPUTextureFormat format = box_gpu_tg_output_format(graph->desc.format);
    uint32_t i;
    result = box_gpu_tg_hash(result, &graph->desc.structure_hash, sizeof(uint64_t));
    result = box_gpu_tg_hash(result, &graph->desc.node_count, sizeof(uint32_t));
    result = box_gpu_tg_hash(result, &format, sizeof(format));
    result = box_gpu_tg_hash(result, &tg->prelude_hash, sizeof(uint64_t));
    for (i = 0; i < graph->desc.node_count; i ++) {
        const box_gpu_tg_kind_t *kind = box_gpu_tg_kind(tg, graph->nodes[i].kind);
        uint64_t h = kind ? kind->hash : 0;
        result = box_gpu_tg_hash(result, &h, sizeof(uint64_t));
        result = box_gpu_tg_hash(result, graph->nodes[i].inputs,
            sizeof(graph->nodes[i].inputs));
        result = box_gpu_tg_hash(result, &graph->nodes[i].params, sizeof(uint32_t));
    }

    result = box_gpu_tg_hash(result, ecs_vec_first(&graph->forced),
        (size_t)ecs_vec_count(&graph->forced) * sizeof(int32_t));

    return result ? result : 1;
}

/** Partition the nodes of a graph into stages: mark the materialized nodes and give every node
 * the materialized node whose stage computes it.
 *
 * The output, the forced boundaries and the inputs of neighbourhood nodes are materialized,
 * then every node that the stages of two materialized nodes reach, until none is.
 *
 * @param tg The graph state
 * @param graph The graph
 * @param mat Receives whether each node is materialized
 * @param owner Receives the materialized node whose stage computes each node
 */
static void box_gpu_tg_partition(
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    bool *mat,
    int32_t *owner)
{
    int32_t n = (int32_t)graph->desc.node_count, i, j, r;
    int32_t *stack = ecs_os_malloc_n(int32_t, n);
    const int32_t *forced = ecs_vec_first(&graph->forced);
    bool changed;
    for (i = 0; i < n; i ++) {
        mat[i] = i == n - 1;
    }

    for (i = 0; i < ecs_vec_count(&graph->forced); i ++) {
        if (forced[i] >= 0 && forced[i] < n) {
            mat[forced[i]] = true;
        }
    }

    for (i = 0; i < n; i ++) {
        const box_gpu_tg_kind_t *kind = box_gpu_tg_kind(tg, graph->nodes[i].kind);
        for (j = 0; kind && (kind->flags & BOX_TEXTURE_NODE_NEIGHBOURHOOD) &&
            j < BOX_TEXTURE_NODE_INPUTS; j ++)
        {
            if (graph->nodes[i].inputs[j] >= 0) {
                mat[graph->nodes[i].inputs[j]] = true;
            }
        }
    }

    do {
        changed = false;
        for (i = 0; i < n; i ++) {
            owner[i] = -1;
        }

        for (r = 0; r < n; r ++) {
            int32_t top = 0;
            if (!mat[r]) {
                continue;
            }

            owner[r] = r;
            stack[top ++] = r;
            while (top) {
                int32_t cur = stack[-- top];
                for (j = 0; j < BOX_TEXTURE_NODE_INPUTS; j ++) {
                    int32_t in = graph->nodes[cur].inputs[j];
                    if (in < 0 || mat[in]) {
                        continue;
                    }

                    if (owner[in] == -1) {
                        owner[in] = r;
                        stack[top ++] = in;
                    } else if (owner[in] != r) {
                        mat[in] = true;
                        changed = true;
                    }
                }
            }
        }
    } while (changed);

    ecs_os_free(stack);
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

/** Write the WGSL expression of input j of a node in a stage: the value of its node, or the
 * constant in its parameters without one.
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
    } else {
        ecs_strbuf_append(buf, "bitcast<vec4f>(box_texgraph_params[%uu])",
            (n->params + kind->param_size) / 16 + (uint32_t)j);
    }
}

/** Generate the code of a stage: the shared code, the functions of its kinds, its bindings and
 * a main that loads its materialized inputs, calls the functions of its nodes in order and
 * stores the value of its root.
 *
 * A neighbourhood function also gets the texel and the texture of each of its inputs, the
 * texture of an input without a node being a 1x1 zero texture.
 *
 * @param tg The graph state
 * @param graph The graph
 * @param stage The stage
 * @param owner The materialized node whose stage computes each node
 * @return The code, freed by the caller
 */
static char* box_gpu_tg_code(
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    box_gpu_tg_stage_t *stage,
    const int32_t *owner)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t line = 1, i, j, k, first;
    ecs_vec_t used;
    char text[256];
    ecs_vec_init_t(NULL, &used, uint32_t, 0);
    box_gpu_tg_append(&buf, &line, tg->prelude);
    box_gpu_tg_lines(&stage->lines, 1, line - 1, -1);
    box_gpu_tg_append(&buf, &line, "\n");
    for (i = 0; i <= stage->root; i ++) {
        const box_gpu_tg_kind_t *kind;
        bool seen = false;
        if (owner[i] != stage->root) {
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
        first = line;
        box_gpu_tg_append(&buf, &line, kind->source);
        if (kind->source[0] && kind->source[ecs_os_strlen(kind->source) - 1] != '\n') {
            box_gpu_tg_append(&buf, &line, "\n");
        }

        box_gpu_tg_lines(&stage->lines, first, line - 1, i);
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
        ecs_os_snprintf(text, ECS_SIZEOF(text),
            "    let node_%d = textureLoad(box_texgraph_in%d, texel, 0);\n",
            stage->inputs[k], k);
        box_gpu_tg_lines(&stage->lines, line, line, stage->inputs[k]);
        box_gpu_tg_append(&buf, &line, text);
    }

    for (i = 0; i <= stage->root; i ++) {
        const box_render_texture_graph_node_t *n = &graph->nodes[i];
        const box_gpu_tg_kind_t *kind;
        if (owner[i] != stage->root) {
            continue;
        }

        kind = box_gpu_tg_kind(tg, n->kind);
        ecs_strbuf_append(&buf, "    let node_%d = %s(box_texgraph_node_params(%uu), "
            "array<vec4f, 4>(", i, kind->function, n->params / 16);
        for (j = 0; j < BOX_TEXTURE_NODE_INPUTS; j ++) {
            if (j) {
                ecs_strbuf_appendstr(&buf, ", ");
            }

            box_gpu_tg_input(&buf, graph, kind, i, j);
        }

        ecs_strbuf_appendstr(&buf, "), coords");
        if (kind->flags & BOX_TEXTURE_NODE_NEIGHBOURHOOD) {
            ecs_strbuf_appendstr(&buf, ", texel");
            for (j = 0; j < (int32_t)kind->input_count; j ++) {
                int32_t in = n->inputs[j], slot = -1;
                for (k = 0; in >= 0 && k < stage->input_count; k ++) {
                    if (stage->inputs[k] == in) {
                        slot = k;
                    }
                }

                if (slot >= 0) {
                    ecs_strbuf_append(&buf, ", box_texgraph_in%d", slot);
                } else {
                    ecs_strbuf_appendstr(&buf, ", box_texgraph_none");
                }
            }
        }

        ecs_strbuf_appendstr(&buf, ");\n");
        box_gpu_tg_lines(&stage->lines, line, line, i);
        line ++;
    }

    ecs_os_snprintf(text, ECS_SIZEOF(text), stage->output
        ? "    textureStore(box_texgraph_out, texel, box_texgraph_encode(node_%d));\n"
        : "    textureStore(box_texgraph_out, texel, node_%d);\n", stage->root);
    box_gpu_tg_lines(&stage->lines, line, line, stage->root);
    box_gpu_tg_append(&buf, &line, text);
    box_gpu_tg_append(&buf, &line, "}\n");
    ecs_vec_fini_t(NULL, &used, uint32_t);

    return ecs_strbuf_get(&buf);
}

/** Compile the code of a stage under its handle through the shader path.
 *
 * @param gpu The backend
 * @param code The code
 * @param label Label of the module
 * @param handle Shader handle of the stage
 */
static void box_gpu_tg_compile(
    box_gpu_t *gpu,
    const char *code,
    const char *label,
    uint32_t handle)
{
    uint32_t code_size = (uint32_t)ecs_os_strlen(code) + 1;
    uint32_t label_size = (uint32_t)ecs_os_strlen(label) + 1;
    box_render_shader_desc_t *desc = ecs_os_malloc(
        (ecs_size_t)(sizeof(box_render_shader_desc_t) + code_size + label_size));
    desc->code_size = code_size;
    desc->label_size = label_size;
    desc->revision = 1;
    ecs_os_memcpy((char*)(desc + 1), code, (ecs_size_t)code_size);
    ecs_os_memcpy((char*)(desc + 1) + code_size, label, (ecs_size_t)label_size);
    box_gpu_shader_apply(gpu, BOX_RENDER_CREATE, handle, desc);
    ecs_os_free(desc);
}

/** Fail a program before its stages compile.
 *
 * @param program The program
 * @param node Node the error comes from, -1 for none
 * @param error Why it fails
 */
static void box_gpu_tg_program_fail(
    box_gpu_tg_program_t *program,
    int32_t node,
    const char *error)
{
    if (!program->failed) {
        program->failed = true;
        program->error = ecs_os_strdup(error);
        program->error_node = node;
    }
}

/** Make the stages of a program from a graph of its key and compile them.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph
 * @param program The program, its stages are filled
 */
static void box_gpu_tg_program_build(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    box_gpu_tg_program_t *program)
{
    int32_t n = (int32_t)graph->desc.node_count, i, j, k, s;
    bool *mat = ecs_os_calloc_n(bool, n);
    int32_t *owner = ecs_os_calloc_n(int32_t, n);
    for (i = 0; i < n; i ++) {
        if (!box_gpu_tg_kind(tg, graph->nodes[i].kind)) {
            char *error = flecs_asprintf("unknown node kind %u", graph->nodes[i].kind);
            box_gpu_tg_program_fail(program, i, error);
            ecs_os_free(error);
            goto done;
        }
    }

    box_gpu_tg_partition(tg, graph, mat, owner);
    for (i = 0; i < n; i ++) {
        program->stage_count += mat[i];
    }

    program->stages = ecs_os_calloc_n(box_gpu_tg_stage_t, program->stage_count);
    for (i = 0, s = 0; i < n; i ++) {
        box_gpu_tg_stage_t *stage;
        if (!mat[i]) {
            continue;
        }

        stage = &program->stages[s ++];
        ecs_vec_init_t(NULL, &stage->lines, box_gpu_tg_line_t, 0);
        stage->root = i;
        stage->output = i == n - 1;
        stage->format = stage->output ? box_gpu_tg_output_format(graph->desc.format)
            : graph->nodes[i].output == TexScalar ? WGPUTextureFormat_R32Float
            : WGPUTextureFormat_RGBA16Float;
        for (j = 0; j <= i; j ++) {
            if (owner[j] != i) {
                continue;
            }

            for (k = 0; k < BOX_TEXTURE_NODE_INPUTS; k ++) {
                int32_t in = graph->nodes[j].inputs[k], m;
                bool found = false;
                if (in < 0 || !mat[in] || in == i) {
                    continue;
                }

                for (m = 0; m < stage->input_count; m ++) {
                    found = found || stage->inputs[m] == in;
                }

                if (found) {
                    continue;
                }

                if (stage->input_count == BOX_GPU_TG_INPUTS) {
                    char *error = flecs_asprintf("a stage reads more than %d materialized "
                        "nodes", BOX_GPU_TG_INPUTS);
                    box_gpu_tg_program_fail(program, j, error);
                    ecs_os_free(error);
                    goto done;
                }

                stage->inputs[stage->input_count ++] = in;
            }
        }

        stage->layout = box_gpu_tg_layout(gpu, tg, stage->input_count, stage->format);
        stage->shader = tg->next_shader ++;
        ecs_map_insert_ptr(&tg->stages, stage->shader, program);
    }

    for (s = 0; s < program->stage_count && !program->failed; s ++) {
        box_gpu_tg_stage_t *stage = &program->stages[s];
        char *code = box_gpu_tg_code(tg, graph, stage, owner);
        char *label = flecs_asprintf("texture_graph_%016llx_%d",
            (unsigned long long)program->key, s);
        box_gpu_tg_compile(gpu, code, label, stage->shader);
        ecs_os_free(label);
        ecs_os_free(code);
    }

done:
    ecs_os_free(owner);
    ecs_os_free(mat);
}

/** Get the program of a key, creating and compiling it from a graph when it does not exist.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph whose key it is
 * @param key The key
 * @return The program with a reference for the caller, NULL before the shared code arrived
 */
static box_gpu_tg_program_t* box_gpu_tg_program_get(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    uint64_t key)
{
    box_gpu_tg_program_t *result = ecs_map_get_deref(&tg->programs, box_gpu_tg_program_t, key);
    if (result) {
        result->refs ++;
        goto done;
    }

    if (!tg->prelude) {
        goto done;
    }

    result = ecs_os_calloc_t(box_gpu_tg_program_t);
    result->key = key;
    result->refs = 1;
    result->node_count = (int32_t)graph->desc.node_count;
    result->error_node = -1;
    ecs_map_insert_ptr(&tg->programs, key, result);
    gpu->texture_graph_programs ++;
    box_gpu_tg_program_build(gpu, tg, graph, result);
done:
    return result;
}

/** Drop a reference to a program, releasing its modules and pipelines with the last one.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param program The program, may be NULL
 */
static void box_gpu_tg_program_release(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_program_t *program)
{
    int32_t s;
    if (!program || -- program->refs > 0) {
        goto done;
    }

    for (s = 0; s < program->stage_count; s ++) {
        box_gpu_tg_stage_t *stage = &program->stages[s];
        if (stage->pipeline) {
            wgpuComputePipelineRelease(stage->pipeline);
        }

        if (stage->shader) {
            ecs_map_remove(&tg->stages, stage->shader);
            box_gpu_shader_apply(gpu, BOX_RENDER_DESTROY, stage->shader, NULL);
        }

        ecs_vec_fini_t(NULL, &stage->lines, box_gpu_tg_line_t);
    }

    ecs_map_remove(&tg->programs, program->key);
    gpu->texture_graph_programs --;
    ecs_os_free(program->stages);
    ecs_os_free(program->error);
    ecs_os_free(program);
done:
    return;
}

/** Check whether every stage of a program has its pipeline, requesting the ones whose module
 * exists.
 *
 * @param gpu The backend
 * @param program The program
 * @return Whether it can be evaluated with
 */
static bool box_gpu_tg_program_ready(
    box_gpu_t *gpu,
    box_gpu_tg_program_t *program)
{
    bool result = !program->failed && program->stage_count > 0;
    int32_t s;
    for (s = 0; result && s < program->stage_count; s ++) {
        box_gpu_tg_stage_t *stage = &program->stages[s];
        WGPUShaderModule module;
        if (stage->pipeline) {
            continue;
        }

        module = box_gpu_shader_get(gpu, stage->shader);
        if (module) {
            stage->pipeline = box_gpu_compute_pipeline(gpu, &(WGPUComputePipelineDescriptor){
                .layout = stage->layout->pipeline,
                .compute = {
                    .module = module,
                    .entryPoint = { .data = "main", .length = WGPU_STRLEN }
                }
            });
        }

        result = stage->pipeline != NULL;
    }

    return result;
}

void box_gpu_texture_graph_compiled(
    box_gpu_t *gpu,
    uint32_t handle,
    const char *message)
{
    box_gpu_texgraphs_t *tg = gpu->texture_graphs;
    box_gpu_tg_program_t *program = tg
        ? ecs_map_get_deref(&tg->stages, box_gpu_tg_program_t, handle) : NULL;
    box_gpu_tg_stage_t *stage = NULL;
    int32_t s, i, node = -1, line = 0, len;
    if (!program || !message || program->failed) {
        goto done;
    }

    for (s = 0; s < program->stage_count; s ++) {
        if (program->stages[s].shader == handle) {
            stage = &program->stages[s];
        }
    }

    const char *at = strstr(message, "wgsl:");
    const char *text = strstr(message, "error: ");
    line = at ? atoi(at + 5) : 0;
    text = text ? text + 7 : message;
    len = (int32_t)strcspn(text, "\n");
    for (i = 0; stage && line && i < ecs_vec_count(&stage->lines); i ++) {
        const box_gpu_tg_line_t *l = ecs_vec_get_t(&stage->lines, box_gpu_tg_line_t, i);
        if (line >= l->first && line <= l->last) {
            node = l->node;
        }
    }

    program->failed = true;
    program->error_node = node;
    program->error = line
        ? flecs_asprintf("line %d of the generated stage: %.*s", line, len, text)
        : flecs_asprintf("%.*s", len, text);
done:
    return;
}

/** Free the description of a graph.
 *
 * @param graph The graph
 */
static void box_gpu_tg_graph_desc_free(
    box_gpu_tg_graph_t *graph)
{
    ecs_os_free(graph->nodes);
    ecs_os_free(graph->params);
    ecs_os_free(graph->invalid);
    graph->nodes = NULL;
    graph->params = NULL;
    graph->invalid = NULL;
}

/** Release a graph: its programs, buffers and readbacks.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph, freed
 */
static void box_gpu_tg_graph_free(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph)
{
    int32_t i;
    for (i = ecs_vec_count(&tg->reads) - 1; i >= 0; i --) {
        box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i);
        if (r->read.handle == graph->handle && r->state == BOX_GPU_TG_READ_WAITING) {
            box_gpu_tg_read_fail(gpu, tg, i);
        }
    }

    box_gpu_tg_program_release(gpu, tg, graph->current);
    box_gpu_tg_program_release(gpu, tg, graph->pending);
    if (graph->param_buffer) {
        wgpuBufferRelease(graph->param_buffer);
    }

    if (graph->uniforms) {
        wgpuBufferRelease(graph->uniforms);
    }

    box_gpu_tg_graph_desc_free(graph);
    ecs_vec_fini_t(NULL, &graph->forced, int32_t);
    ecs_os_free(graph);
    gpu->texture_graph_count --;
}

/** Get the bytes of the parameter buffer of a graph: its parameters and the padding after them,
 * a multiple of 16.
 *
 * @param desc The description of the graph
 * @return The bytes
 */
static uint64_t box_gpu_tg_param_bytes(
    const box_render_texture_graph_t *desc)
{
    return ((uint64_t)desc->param_size + BOX_GPU_TG_PARAM_PAD + 15) / 16 * 16;
}

/** Check the description of a graph: its nodes come after their inputs and their parameters
 * are within the parameter bytes, 16 byte aligned.
 *
 * @param desc The description
 * @param nodes Its nodes
 * @return Why it cannot be evaluated, freed by the caller, NULL when it is valid
 */
static char* box_gpu_tg_check(
    const box_render_texture_graph_t *desc,
    const box_render_texture_graph_node_t *nodes)
{
    char *result = NULL;
    uint32_t i, j;
    if (!desc->node_count || !desc->width || !desc->height) {
        result = ecs_os_strdup("graph without nodes or of size 0");
        goto done;
    }

    for (i = 0; i < desc->node_count; i ++) {
        if (nodes[i].params % 16 || nodes[i].params + nodes[i].param_size > desc->param_size) {
            result = flecs_asprintf("parameters of node %u out of range", i);
            goto done;
        }

        for (j = 0; j < BOX_TEXTURE_NODE_INPUTS; j ++) {
            if (nodes[i].inputs[j] >= (int32_t)i) {
                result = flecs_asprintf("input %u of node %u is not an earlier node", j, i);
                goto done;
            }
        }
    }

done:
    return result;
}

void box_gpu_texture_graph_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_texture_graph_t *desc,
    uint32_t size)
{
    box_gpu_texgraphs_t *tg = op == BOX_RENDER_DESTROY ? gpu->texture_graphs
        : box_gpu_tg_get(gpu);
    box_gpu_tg_graph_t *graph = tg
        ? ecs_map_get_deref(&tg->graphs, box_gpu_tg_graph_t, handle) : NULL;
    uint64_t nodes_size, total;
    if (op == BOX_RENDER_DESTROY || !desc) {
        if (graph) {
            ecs_map_remove(&tg->graphs, handle);
            box_gpu_tg_graph_free(gpu, tg, graph);
        }

        goto done;
    }

    nodes_size = (uint64_t)desc->node_count * sizeof(box_render_texture_graph_node_t);
    total = sizeof(box_render_texture_graph_t) + nodes_size + desc->param_size;
    if (total > size) {
        ecs_err("texture graph %u: description of %u bytes is truncated", handle, size);
        goto done;
    }

    if (!graph) {
        graph = ecs_os_calloc_t(box_gpu_tg_graph_t);
        graph->handle = handle;
        ecs_vec_init_t(NULL, &graph->forced, int32_t, 0);
        ecs_map_insert_ptr(&tg->graphs, handle, graph);
        gpu->texture_graph_count ++;
    }

    if (graph->desc.structure_hash != desc->structure_hash ||
        graph->desc.node_count != desc->node_count)
    {
        ecs_vec_clear(&graph->forced);
    }

    box_gpu_tg_graph_desc_free(graph);
    graph->desc = *desc;
    graph->nodes = ecs_os_memdup(box_render_texture_graph_nodes(desc), (ecs_size_t)nodes_size);
    graph->params = ecs_os_calloc((ecs_size_t)box_gpu_tg_param_bytes(desc));
    if (desc->param_size) {
        ecs_os_memcpy(graph->params, box_render_texture_graph_params(desc),
            (ecs_size_t)desc->param_size);
    }

    graph->invalid = box_gpu_tg_check(desc, graph->nodes);
    graph->key = 0;
    graph->dirty = true;
done:
    return;
}

void box_gpu_texture_node_kind_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_texture_node_kind_t *desc,
    uint32_t size)
{
    box_gpu_texgraphs_t *tg = op == BOX_RENDER_DESTROY ? gpu->texture_graphs
        : box_gpu_tg_get(gpu);
    box_gpu_tg_kind_t *kind = tg ? box_gpu_tg_kind(tg, handle) : NULL;
    uint32_t i;
    if (kind) {
        ecs_map_remove(&tg->kinds, handle);
        ecs_os_free(kind->function);
        ecs_os_free(kind->source);
        ecs_os_free(kind);
        kind = NULL;
    }

    if (op == BOX_RENDER_DESTROY || !desc ||
        (uint64_t)sizeof(*desc) + desc->function_size + desc->source_size > size ||
        !desc->function_size || !desc->source_size)
    {
        goto stale;
    }

    kind = ecs_os_calloc_t(box_gpu_tg_kind_t);
    kind->output = desc->output;
    kind->input_count = desc->input_count > BOX_TEXTURE_NODE_INPUTS
        ? BOX_TEXTURE_NODE_INPUTS : desc->input_count;
    kind->param_size = desc->param_size;
    kind->flags = desc->flags;
    kind->function = ecs_os_strdup(box_render_texture_node_kind_function(desc));
    kind->source = ecs_os_strdup(box_render_texture_node_kind_source(desc));
    kind->hash = box_gpu_tg_hash(BOX_GPU_TG_FNV_BASIS, desc, sizeof(*desc));
    kind->hash = box_gpu_tg_hash(kind->hash, kind->function, strlen(kind->function));
    kind->hash = box_gpu_tg_hash(kind->hash, kind->source, strlen(kind->source));
    ecs_map_insert_ptr(&tg->kinds, handle, kind);
stale:
    if (tg) {
        ecs_map_iter_t it = ecs_map_iter(&tg->graphs);
        while (ecs_map_next(&it)) {
            box_gpu_tg_graph_t *graph = ecs_map_ptr(&it);
            for (i = 0; i < graph->desc.node_count; i ++) {
                if (graph->nodes[i].kind == handle) {
                    graph->key = 0;
                }
            }
        }
    }
}

void box_gpu_texture_graph_prelude(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    const box_render_shader_desc_t *desc)
{
    box_gpu_texgraphs_t *tg = box_gpu_tg_get(gpu);
    const char *code = desc ? (const char*)(desc + 1) : NULL;
    const char *c;
    ecs_os_free(tg->prelude);
    tg->prelude = NULL;
    tg->prelude_lines = 0;
    tg->prelude_hash = 0;
    if (op != BOX_RENDER_DESTROY && code && desc->code_size) {
        tg->prelude = ecs_os_strdup(code);
        tg->prelude_hash = box_gpu_tg_hash(BOX_GPU_TG_FNV_BASIS, code, strlen(code));
        for (c = code; *c; c ++) {
            tg->prelude_lines += *c == '\n';
        }

        box_gpu_shader_report(gpu, BOX_RENDER_SHADER_TEXTURE_GRAPH, desc->revision, NULL,
            code + desc->code_size);
    }

    ecs_map_iter_t it = ecs_map_iter(&tg->graphs);
    while (ecs_map_next(&it)) {
        ((box_gpu_tg_graph_t*)ecs_map_ptr(&it))->key = 0;
    }
}

/** Bring the programs of a graph up to date: compute its key, create the program of the key
 * and make it current once it exists, or report its failure.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph
 * @return Whether the graph can be evaluated with its current program
 */
static bool box_gpu_tg_graph_ready(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph)
{
    bool result = false;
    if (graph->invalid) {
        box_gpu_tg_report(gpu, graph, graph->invalid, -1);
        goto done;
    }

    if (!graph->key) {
        graph->key = box_gpu_tg_key(tg, graph);
    }

    if (graph->current && graph->current->key == graph->key) {
        box_gpu_tg_program_release(gpu, tg, graph->pending);
        graph->pending = NULL;
        result = box_gpu_tg_program_ready(gpu, graph->current);
        goto done;
    }

    if (!graph->pending || graph->pending->key != graph->key) {
        box_gpu_tg_program_release(gpu, tg, graph->pending);
        graph->pending = box_gpu_tg_program_get(gpu, tg, graph, graph->key);
    }

    if (graph->pending && graph->pending->failed) {
        box_gpu_tg_report(gpu, graph, graph->pending->error, graph->pending->error_node);
        box_gpu_tg_node_reads_fail(gpu, tg, graph);
    } else if (graph->pending && box_gpu_tg_program_ready(gpu, graph->pending)) {
        box_gpu_tg_program_release(gpu, tg, graph->current);
        graph->current = graph->pending;
        graph->pending = NULL;
        graph->dirty = true;
        result = true;
    }

done:
    return result;
}

/** Write the parameters and uniforms of a graph for an evaluation.
 *
 * @param gpu The backend
 * @param graph The graph
 */
static void box_gpu_tg_upload(
    box_gpu_t *gpu,
    box_gpu_tg_graph_t *graph)
{
    uint64_t size = box_gpu_tg_param_bytes(&graph->desc);
    uint32_t encode = graph->desc.format == GraphColor ? 1u
        : graph->desc.format == GraphSingle ? 2u : 0u;
    uint32_t uniforms[4] = {
        graph->desc.width, graph->desc.height, graph->desc.domain, encode
    };
    if (graph->param_capacity < size) {
        if (graph->param_buffer) {
            wgpuBufferRelease(graph->param_buffer);
        }

        graph->param_buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = size
        });
        graph->param_capacity = size;
    }

    if (!graph->uniforms) {
        graph->uniforms = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = BOX_GPU_TG_UNIFORM_SIZE
        });
    }

    wgpuQueueWriteBuffer(gpu->queue, graph->param_buffer, 0, graph->params, (size_t)size);
    wgpuQueueWriteBuffer(gpu->queue, graph->uniforms, 0, uniforms, sizeof(uniforms));
}

/** Record the dispatches of the stages of a graph into its image, with an intermediate of the
 * pool for each other materialized node.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param pass The compute pass
 * @param ev The evaluation, receives the intermediates
 */
static void box_gpu_tg_record(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    WGPUComputePassEncoder pass,
    box_gpu_tg_eval_t *ev)
{
    box_gpu_tg_graph_t *graph = ev->graph;
    box_gpu_tg_program_t *program = graph->current;
    uint32_t w = graph->desc.width, h = graph->desc.height;
    WGPUTextureFormat image = box_gpu_tg_image_format(graph->desc.format);
    WGPUTextureView out = NULL;
    WGPUBindGroupEntry entries[BOX_GPU_TG_FIRST_INPUT + BOX_GPU_TG_INPUTS];
    int32_t s, k;
    box_gpu_texture_t *tex = box_gpu_texture_output(gpu, graph->handle, w, h, image,
        graph->desc.format == GraphColor);
    box_gpu_tg_upload(gpu, graph);
    if (image == BOX_GPU_TEXTURE_SINGLE) {
        ev->temp = box_gpu_tg_pool_take(gpu, tg, w, h, WGPUTextureFormat_RGBA16Float);
    } else {
        out = wgpuTextureCreateView(tex->texture, &(WGPUTextureViewDescriptor){
            .format = image,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        });
    }

    ev->textures = ecs_os_calloc_n(box_gpu_tg_pooled_t*, program->node_count);
    for (s = 0; s < program->stage_count; s ++) {
        const box_gpu_tg_stage_t *stage = &program->stages[s];
        if (!stage->output) {
            ev->textures[stage->root] = box_gpu_tg_pool_take(gpu, tg, w, h, stage->format);
        }
    }

    for (s = 0; s < program->stage_count; s ++) {
        const box_gpu_tg_stage_t *stage = &program->stages[s];
        WGPUTextureView target = stage->output
            ? (ev->temp ? ev->temp->view : out)
            : ev->textures[stage->root]->view;
        entries[0] = (WGPUBindGroupEntry){
            .binding = 0, .buffer = graph->param_buffer, .size = graph->param_capacity
        };
        entries[1] = (WGPUBindGroupEntry){
            .binding = 1, .buffer = graph->uniforms, .size = BOX_GPU_TG_UNIFORM_SIZE
        };
        entries[2] = (WGPUBindGroupEntry){ .binding = 2, .textureView = target };
        entries[3] = (WGPUBindGroupEntry){ .binding = 3, .textureView = tg->none_view };
        for (k = 0; k < stage->input_count; k ++) {
            entries[BOX_GPU_TG_FIRST_INPUT + k] = (WGPUBindGroupEntry){
                .binding = (uint32_t)(BOX_GPU_TG_FIRST_INPUT + k),
                .textureView = ev->textures[stage->inputs[k]]->view
            };
        }

        WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device,
            &(WGPUBindGroupDescriptor){
                .layout = stage->layout->group,
                .entryCount = (size_t)(BOX_GPU_TG_FIRST_INPUT + stage->input_count),
                .entries = entries
            });
        wgpuComputePassEncoderSetPipeline(pass, stage->pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass,
            (w + BOX_GPU_TG_GROUP - 1) / BOX_GPU_TG_GROUP,
            (h + BOX_GPU_TG_GROUP - 1) / BOX_GPU_TG_GROUP, 1);
        wgpuBindGroupRelease(group);
        gpu->pending.texture_graph_dispatches ++;
        gpu->pending.texture_graph_texels += (int64_t)w * h;
    }

    if (out) {
        wgpuTextureViewRelease(out);
    }

    gpu->pending.texture_graph_evaluations ++;
    if (!ev->held) {
        for (s = 0; s < program->node_count; s ++) {
            box_gpu_tg_pool_put(ev->textures[s]);
        }
    }
}

void box_gpu_texture_graphs_evaluate(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_texgraphs_t *tg = gpu->texture_graphs;
    ecs_vec_t evals;
    WGPUCommandEncoder encoder = NULL;
    int32_t i, count, stamp = -1;
    bool requested;
    (void)packet;
    ecs_os_mutex_lock(gpu->lock);
    requested = ecs_vec_count(&gpu->graph_requests) > 0;
    ecs_os_mutex_unlock(gpu->lock);
    if (!tg && requested) {
        tg = box_gpu_tg_get(gpu);
    }

    if (!tg) {
        goto done;
    }

    tg->frame ++;
    box_gpu_tg_poll(gpu, tg);
    box_gpu_tg_requests(gpu, tg);
    box_gpu_tg_stamps_init(gpu, tg);
    ecs_vec_init_t(NULL, &evals, box_gpu_tg_eval_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&tg->graphs);
    while (ecs_map_next(&it)) {
        box_gpu_tg_graph_t *graph = ecs_map_ptr(&it);
        bool reads = box_gpu_tg_node_reads(tg, graph);
        if (box_gpu_tg_graph_ready(gpu, tg, graph) && (graph->dirty || reads)) {
            *ecs_vec_append_t(NULL, &evals, box_gpu_tg_eval_t) = (box_gpu_tg_eval_t){
                .graph = graph,
                .held = reads || graph->desc.format == GraphSingle
            };
        }
    }

    count = ecs_vec_count(&evals);
    for (i = 0; i < BOX_GPU_TG_STAMPS && count && tg->queries && stamp < 0; i ++) {
        if (tg->stamps[i].state == BOX_GPU_TG_STAMP_FREE) {
            stamp = i;
        }
    }

    if (!count && !box_gpu_tg_busy(tg)) {
        goto sweep;
    }

    encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    box_gpu_tg_stamps_resolve(tg, encoder);
    if (count) {
        box_gpu_tg_eval_t *ev = ecs_vec_first(&evals);
        WGPUPassTimestampWrites writes = {
            .querySet = tg->queries,
            .beginningOfPassWriteIndex = (uint32_t)stamp * 2,
            .endOfPassWriteIndex = (uint32_t)stamp * 2 + 1
        };
        WGPUComputePassEncoder pass = box_gpu_compute_begin(encoder,
            stamp >= 0 ? &writes : NULL);
        for (i = 0; i < count; i ++) {
            box_gpu_tg_record(gpu, tg, pass, &ev[i]);
        }

        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        for (i = 0; i < count; i ++) {
            ev[i].stored = !ev[i].temp ||
                box_gpu_texture_output_blit(gpu, encoder, ev[i].graph->handle, ev[i].temp->view);
        }

        if (stamp >= 0) {
            tg->stamps[stamp].state = BOX_GPU_TG_STAMP_WRITTEN;
        }
    }

    box_gpu_tg_copies(gpu, tg, encoder, ecs_vec_first(&evals), count);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    box_gpu_tg_map(tg);
    for (i = 0; i < count; i ++) {
        box_gpu_tg_eval_t *ev = ecs_vec_get_t(&evals, box_gpu_tg_eval_t, i);
        box_gpu_tg_graph_t *graph = ev->graph;
        int32_t n;
        for (n = 0; ev->held && n < graph->current->node_count; n ++) {
            box_gpu_tg_pool_put(ev->textures[n]);
        }

        box_gpu_tg_pool_put(ev->temp);
        ecs_os_free(ev->textures);
        if (!ev->stored) {
            continue;
        }

        box_gpu_texture_output_written(gpu, graph->handle);
        graph->dirty = false;
        graph->written = true;
        box_gpu_tg_report(gpu, graph, NULL, -1);
    }

sweep:
    ecs_vec_fini_t(NULL, &evals, box_gpu_tg_eval_t);
    box_gpu_tg_pool_sweep(gpu, tg, false);
done:
    return;
}

void box_gpu_texture_graphs_fini(
    box_gpu_t *gpu)
{
    box_gpu_texgraphs_t *tg = gpu->texture_graphs;
    int32_t i;
    if (!tg) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&tg->graphs);
    while (ecs_map_next(&it)) {
        box_gpu_tg_graph_free(gpu, tg, ecs_map_ptr(&it));
    }

    for (i = 0; i < ecs_vec_count(&tg->reads); i ++) {
        box_gpu_tg_read_t *r = *ecs_vec_get_t(&tg->reads, box_gpu_tg_read_t*, i);
        if (r->buffer) {
            wgpuBufferRelease(r->buffer);
        }

        ecs_os_free(r);
    }

    it = ecs_map_iter(&tg->kinds);
    while (ecs_map_next(&it)) {
        box_gpu_tg_kind_t *kind = ecs_map_ptr(&it);
        ecs_os_free(kind->function);
        ecs_os_free(kind->source);
        ecs_os_free(kind);
    }

    for (i = 0; i < ecs_vec_count(&tg->layouts); i ++) {
        box_gpu_tg_layout_t *l = *ecs_vec_get_t(&tg->layouts, box_gpu_tg_layout_t*, i);
        wgpuPipelineLayoutRelease(l->pipeline);
        wgpuBindGroupLayoutRelease(l->group);
        ecs_os_free(l);
    }

    for (i = 0; i < ecs_vec_count(&tg->pool); i ++) {
        (*ecs_vec_get_t(&tg->pool, box_gpu_tg_pooled_t*, i))->used = false;
    }

    box_gpu_tg_pool_sweep(gpu, tg, true);
    for (i = 0; i < BOX_GPU_TG_STAMPS; i ++) {
        if (tg->stamps[i].buffer) {
            wgpuBufferRelease(tg->stamps[i].buffer);
        }
    }

    if (tg->queries) {
        wgpuQuerySetRelease(tg->queries);
    }

    if (tg->resolve) {
        wgpuBufferRelease(tg->resolve);
    }

    wgpuTextureViewRelease(tg->none_view);
    wgpuTextureRelease(tg->none);
    ecs_map_fini(&tg->kinds);
    ecs_map_fini(&tg->graphs);
    ecs_map_fini(&tg->programs);
    ecs_map_fini(&tg->stages);
    ecs_vec_fini_t(NULL, &tg->layouts, box_gpu_tg_layout_t*);
    ecs_vec_fini_t(NULL, &tg->pool, box_gpu_tg_pooled_t*);
    ecs_vec_fini_t(NULL, &tg->reads, box_gpu_tg_read_t*);
    ecs_os_free(tg->prelude);
    ecs_os_free(tg);
    gpu->texture_graphs = NULL;
    gpu->texture_graph_count = 0;
    gpu->texture_graph_programs = 0;
    gpu->texture_graph_pool_bytes = 0;
done:
    return;
}

#endif
