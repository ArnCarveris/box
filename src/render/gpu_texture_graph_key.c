/**
 * @file render/gpu_texture_graph_key.c
 * @brief Interpolated texture graphs of the webgpu backend: keys and their crossfade.
 *
 * An interpolated graph evaluates its keys k and k + 1 of time k / rate into two textures it
 * owns, one key ahead of the clock, and a crossfade of them writes its image every frame.
 */

#include "gpu_texture_graph.h"
#include <math.h>

#ifdef BOX_RENDER_WGPU

/** Release the key images and their buffers of an interpolated graph.
 *
 * @param gpu The backend
 * @param graph The graph
 */
void box_gpu_tg_keyframes_free(
    box_gpu_t *gpu,
    box_gpu_tg_graph_t *graph)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        box_gpu_tg_pooled_t *p = &graph->keys[i];
        if (p->texture) {
            wgpuTextureViewRelease(p->view);
            wgpuTextureRelease(p->texture);
            gpu->texture_graph_key_bytes -= (uint64_t)p->width * p->height *
                box_gpu_tg_texel_size(p->format);
        }

        ecs_os_memset_t(p, 0, box_gpu_tg_pooled_t);
        if (graph->key_uniforms[i]) {
            wgpuBufferRelease(graph->key_uniforms[i]);
            graph->key_uniforms[i] = NULL;
        }
    }

    if (graph->blend_uniforms) {
        wgpuBufferRelease(graph->blend_uniforms);
        graph->blend_uniforms = NULL;
    }

    graph->keys_valid = false;
}

/** Get a key image of an interpolated graph of the size and format its output stage writes,
 * creating it again when it has another.
 *
 * @param gpu The backend
 * @param graph The graph
 * @param slot The key, 0 or 1
 * @return The key image
 */
box_gpu_tg_pooled_t* box_gpu_tg_keyframe(
    box_gpu_t *gpu,
    box_gpu_tg_graph_t *graph,
    int32_t slot)
{
    box_gpu_tg_pooled_t *result = &graph->keys[slot];
    uint32_t w = graph->desc.width, h = graph->desc.height;
    WGPUTextureFormat format = box_gpu_tg_output_format(graph->desc.format);
    if (result->texture && result->width == w && result->height == h &&
        result->format == format)
    {
        goto done;
    }

    if (result->texture) {
        wgpuTextureViewRelease(result->view);
        wgpuTextureRelease(result->texture);
        gpu->texture_graph_key_bytes -= (uint64_t)result->width * result->height *
            box_gpu_tg_texel_size(result->format);
    }

    result->width = w;
    result->height = h;
    result->format = format;
    result->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { w, h, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    result->view = wgpuTextureCreateView(result->texture, NULL);
    gpu->texture_graph_key_bytes += (uint64_t)w * h * box_gpu_tg_texel_size(format);
done:
    return result;
}

/** Release the crossfade shaders and pipelines.
 *
 * @param gpu The backend
 * @param tg The graph state
 */
void box_gpu_tg_blend_release(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        if (tg->blend_pipelines[i]) {
            wgpuComputePipelineRelease(tg->blend_pipelines[i]);
            tg->blend_pipelines[i] = NULL;
        }

        if (tg->blend_shaders[i]) {
            box_gpu_shader_apply(gpu, BOX_RENDER_DESTROY, tg->blend_shaders[i], NULL);
            tg->blend_shaders[i] = 0;
        }
    }
}

/** Get the pipeline of the crossfade of the keys of an interpolated graph into an image of a
 * format, compiling it with the shared code on first use and again when the shared code changed.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param format Format of the image, RGBA8Unorm or RGBA16Float
 * @return The pipeline, NULL while it is created or before the shared code arrived
 */
static WGPUComputePipeline box_gpu_tg_blend_pipeline(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    WGPUTextureFormat format)
{
    int32_t i = format == WGPUTextureFormat_RGBA16Float ? 1 : 0;
    WGPUComputePipeline result = NULL;
    WGPUShaderModule module;
    if (!tg->prelude) {
        goto done;
    }

    if (tg->blend_prelude != tg->prelude_hash) {
        box_gpu_tg_blend_release(gpu, tg);
        tg->blend_prelude = tg->prelude_hash;
    }

    if (!tg->blend_shaders[i]) {
        char *code = box_gpu_tg_blend_code(tg, format);
        tg->blend_shaders[i] = tg->next_shader ++;
        box_gpu_tg_compile(gpu, code, i ? "texture_graph_blend_hdr" : "texture_graph_blend",
            tg->blend_shaders[i]);
        ecs_os_free(code);
    }

    if (!tg->blend_pipelines[i]) {
        module = box_gpu_shader_get(gpu, tg->blend_shaders[i]);
        if (module) {
            tg->blend_pipelines[i] = box_gpu_compute_pipeline(gpu,
                &(WGPUComputePipelineDescriptor){
                    .layout = box_gpu_tg_layout(gpu, tg, 2, 0, format)->pipeline,
                    .compute = {
                        .module = module,
                        .entryPoint = { .data = "main", .length = WGPU_STRLEN }
                    }
                });
        }
    }

    result = tg->blend_pipelines[i];
done:
    return result;
}

/** Check whether a graph is interpolated: animated, with a rate and TextureGraph.interpolate,
 * and an image a crossfade can store.
 *
 * @param graph The graph, its hashes computed
 * @return Whether it is
 */
bool box_gpu_tg_interpolated(
    const box_gpu_tg_graph_t *graph)
{
    return graph->desc.interpolate && graph->desc.rate > 0 && graph->is_animated &&
        graph->desc.format != GraphSingle;
}

/** Plan the evaluations of an interpolated graph in a packet with key k of the clock: keys k
 * and k + 1 after a change, a readback, a changed source or a jump of the clock, else key
 * k + 1 into the older key once the clock reached the newer one; then the crossfade.
 *
 * The crossfade is skipped while the clock is paused. A graph no draw sampled does nothing
 * unless it changed, and nothing happens while the crossfade pipeline is created.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph, ready and interpolated
 * @param reads Readbacks of its nodes wait
 * @param evals Receives the box_gpu_tg_eval_t of the packet
 */
void box_gpu_tg_plan_keys(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    bool reads,
    ecs_vec_t *evals)
{
    double rate = (double)graph->desc.rate;
    int64_t k = (int64_t)floor(tg->time * rate);
    int32_t lo = graph->key_index[0] <= graph->key_index[1] ? 0 : 1;
    int32_t first = ecs_vec_count(evals), i;
    bool drawn = graph->sampled >= tg->frame - 1;
    bool update = !graph->dirty && !reads;
    if (!graph->dirty && !reads && !drawn) {
        goto done;
    }

    if (!box_gpu_tg_blend_pipeline(gpu, tg,
        box_gpu_tg_image_format(graph->desc.format)))
    {
        goto done;
    }

    if (graph->dirty || reads || !graph->keys_valid ||
        k < graph->key_index[lo] || k > graph->key_index[lo] + 1 ||
        box_gpu_tg_sources_changed(gpu, tg, graph))
    {
        for (i = 0; i < 2; i ++) {
            graph->key_index[i] = k + i;
            *ecs_vec_append_t(NULL, evals, box_gpu_tg_eval_t) = (box_gpu_tg_eval_t){
                .graph = graph, .held = reads, .update = update, .key = i, .key_index = k + i
            };
        }

        graph->keys_valid = true;
    } else if (k == graph->key_index[lo] + 1) {
        graph->key_index[lo] = k + 1;
        *ecs_vec_append_t(NULL, evals, box_gpu_tg_eval_t) = (box_gpu_tg_eval_t){
            .graph = graph, .update = true, .key = lo, .key_index = k + 1
        };
    }

    if (ecs_vec_count(evals) > first || tg->time != graph->blended) {
        *ecs_vec_append_t(NULL, evals, box_gpu_tg_eval_t) = (box_gpu_tg_eval_t){
            .graph = graph, .key = BOX_GPU_TG_BLEND, .key_index = -1
        };
        graph->eval = ecs_vec_count(evals) - 1;
    }

done:
    return;
}

/** Record the crossfade of the two keys of an interpolated graph into the first level of its
 * image, by the fraction of the clock between them, and queue its mip levels.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param pass The compute pass
 * @param ev The evaluation of the crossfade
 */
void box_gpu_tg_blend(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    WGPUComputePassEncoder pass,
    box_gpu_tg_eval_t *ev)
{
    box_gpu_tg_graph_t *graph = ev->graph;
    uint32_t w = graph->desc.width, h = graph->desc.height;
    WGPUTextureFormat image = box_gpu_tg_image_format(graph->desc.format);
    WGPUComputePipeline pipeline = box_gpu_tg_blend_pipeline(gpu, tg, image);
    int32_t lo = graph->key_index[0] <= graph->key_index[1] ? 0 : 1;
    double frac = tg->time * (double)graph->desc.rate - (double)graph->key_index[lo];
    float f = (float)(frac < 0 ? 0 : frac > 1 ? 1 : frac);
    uint32_t uniforms[12] = {
        w, h, graph->desc.domain, graph->desc.format == GraphColor ? 1u : 0u, 0, 0, 0, 0
    };
    box_gpu_texture_t *tex;
    WGPUTextureView out;
    WGPUBindGroup group;
    if (!pipeline || !graph->keys[0].texture || !graph->keys[1].texture ||
        !graph->param_buffer)
    {
        goto done;
    }

    ecs_os_memcpy(&uniforms[4], &f, ECS_SIZEOF(float));
    ecs_os_memcpy(&uniforms[8], graph->desc.region, ECS_SIZEOF(graph->desc.region));
    if (!graph->blend_uniforms) {
        graph->blend_uniforms = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = BOX_GPU_TG_UNIFORM_SIZE
        });
    }

    wgpuQueueWriteBuffer(gpu->queue, graph->blend_uniforms, 0, uniforms, sizeof(uniforms));
    tex = box_gpu_texture_output(gpu, graph->handle, w, h, image,
        graph->desc.format == GraphColor);
    out = wgpuTextureCreateView(tex->texture, &(WGPUTextureViewDescriptor){
        .format = image,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });
    group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = box_gpu_tg_layout(gpu, tg, 2, 0, image)->group,
        .entryCount = 6,
        .entries = (WGPUBindGroupEntry[]){
            { .binding = 0, .buffer = graph->param_buffer, .size = graph->param_capacity },
            { .binding = 1, .buffer = graph->blend_uniforms,
                .size = BOX_GPU_TG_UNIFORM_SIZE },
            { .binding = 2, .textureView = out },
            { .binding = 3, .textureView = tg->none_view },
            { .binding = BOX_GPU_TG_FIRST_INPUT, .textureView = graph->keys[lo].view },
            { .binding = BOX_GPU_TG_FIRST_INPUT + 1,
                .textureView = graph->keys[1 - lo].view }
        }
    });
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,
        (w + BOX_GPU_TG_GROUP - 1) / BOX_GPU_TG_GROUP,
        (h + BOX_GPU_TG_GROUP - 1) / BOX_GPU_TG_GROUP, 1);
    wgpuBindGroupRelease(group);
    wgpuTextureViewRelease(out);
    box_gpu_texture_output_written(gpu, graph->handle);
    graph->blended = tg->time;
    ev->stored = true;
    gpu->pending.texture_graph_blends ++;
done:
    return;
}

#endif
