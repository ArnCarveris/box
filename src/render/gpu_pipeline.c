/**
 * @file render/gpu_pipeline.c
 * @brief Render and compute pipelines of the webgpu backend, created in the background.
 *
 * The first request of a descriptor starts the creation, on a worker thread natively (wgpu-native
 * does not implement the asynchronous calls) and with the asynchronous calls in the browser.
 * Requests get NULL until the outcome is delivered at the start of a packet, then a reference.
 *
 * A created pipeline stays in the cache BOX_GPU_PIPELINE_KEEP packets after its last request,
 * a failed one until fini, so it is neither created nor reported again.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Worker threads that create pipelines natively, so that a slow pipeline does not hold back
 * the others */
#define BOX_GPU_PIPELINE_WORKERS (4)

/** Packets a created pipeline stays in the cache after its last request */
#define BOX_GPU_PIPELINE_KEEP (120)

/** Packets between two sweeps of the cache for pipelines whose time is up */
#define BOX_GPU_PIPELINE_SWEEP (30)

#if defined(_MSC_VER)
/** Storage of a variable of which every thread has its own */
#define BOX_GPU_THREAD_LOCAL __declspec(thread)
#else
/** Storage of a variable of which every thread has its own */
#define BOX_GPU_THREAD_LOCAL _Thread_local
#endif

typedef struct box_gpu_pipeline_t box_gpu_pipeline_t;

/** Where the creation of a pipeline of the cache is */
typedef enum box_gpu_pipeline_state_t {
    BOX_GPU_PIPELINE_BUILDING,      /**< Being created, its outcome was not delivered */
    BOX_GPU_PIPELINE_READY,         /**< Created, every request gets a reference */
    BOX_GPU_PIPELINE_FAILED         /**< The creation failed, every request gets NULL */
} box_gpu_pipeline_state_t;

/** A pipeline of the cache with the copy of the descriptor it is created from */
struct box_gpu_pipeline_t {
    box_gpu_pipeline_t *next;       /**< Next pipeline of the cache with the same hash */
    box_gpu_pipeline_t *queued;     /**< Next pipeline of the queue or of the arrived outcomes */
    box_gpu_pipelines_t *cache;     /**< The cache, NULL once it is gone while the browser
                                     *   creates the pipeline, whose callback frees it */
    uint64_t hash;                  /**< Hash of the key */
    ecs_vec_t key;                  /**< uint8_t, the descriptor as values */
    ecs_vec_t owned;                /**< void*, the copies the descriptor points into */
    bool compute;                   /**< A compute pipeline, else a render pipeline */
    WGPURenderPipelineDescriptor render; /**< Descriptor of a render pipeline */
    WGPUComputePipelineDescriptor compute_desc; /**< Descriptor of a compute pipeline */
    WGPUDepthStencilState depth;    /**< Depth state render.depthStencil points to */
    WGPUFragmentState fragment;     /**< Fragment state render.fragment points to */
    box_gpu_pipeline_state_t state; /**< Where the creation is, render thread only */
    WGPURenderPipeline render_pipeline; /**< The render pipeline once created */
    WGPUComputePipeline compute_pipeline; /**< The compute pipeline once created */
    char *message;                  /**< Error of a failed creation */
    const char *shader;             /**< Label of the module of the first stage, owned, NULL
                                     *   when the module is not the module of a shader */
    ecs_time_t start;               /**< When the asynchronous creation started */
    double seconds;                 /**< Time the creation took */
    bool failed;                    /**< The creation failed */
    bool arrived;                   /**< The outcome arrived and waits for its delivery */
    bool quiet;                     /**< A failure is not reported, see pipeline_quiet */
    int64_t used;                   /**< Packet of the last request */
};

/** The pipelines of a backend and the workers that create them */
struct box_gpu_pipelines_t {
    ecs_map_t entries;              /**< Hash to the first box_gpu_pipeline_t of the hash */
    ecs_vec_t scratch;              /**< uint8_t, the key of a request */
    WGPUDevice device;              /**< Device the pipelines are created on */
    ecs_os_mutex_t lock;            /**< Protects the queues, arrived, active and stop */
    ecs_os_cond_t wake;             /**< Wakes the workers for a queued creation or the stop */
    ecs_os_cond_t done;             /**< Signals an outcome that arrived */
    box_gpu_pipeline_t *queue[2];   /**< Creations no worker started, newest first: render
                                     *   pipelines before compute and forward check ones, see
                                     *   box_gpu_pipeline_start */
    box_gpu_pipeline_t *arrived;    /**< Outcomes that wait for their delivery */
    int32_t active;                 /**< Creations running on the workers */
    int32_t workers;                /**< Worker threads started */
    ecs_os_thread_t threads[BOX_GPU_PIPELINE_WORKERS]; /**< The worker threads */
    bool stop;                      /**< The workers stop */
    bool hold;                      /**< box_render_gpu_compile_delivery holds the outcomes in
                                     *   this packet */
    int64_t packets;                /**< Packets delivered, the clock of the cache */
    int32_t building;               /**< Pipelines whose outcome was not delivered */
};

/** Capture of the creation that runs on this thread, which device errors are reported to */
static BOX_GPU_THREAD_LOCAL box_gpu_error_capture_t *box_gpu_capturing = NULL;

/** Append bytes to a key.
 *
 * @param key The key
 * @param data The bytes
 * @param size Bytes to append
 */
static void box_gpu_key_bytes(
    ecs_vec_t *key,
    const void *data,
    ecs_size_t size)
{
    if (size > 0) {
        ecs_os_memcpy(ecs_vec_grow_t(NULL, key, uint8_t, size), data, size);
    }
}

/** Append an integer to a key.
 *
 * @param key The key
 * @param value The integer
 */
static void box_gpu_key_u64(
    ecs_vec_t *key,
    uint64_t value)
{
    box_gpu_key_bytes(key, &value, ECS_SIZEOF(uint64_t));
}

/** Append a number to a key.
 *
 * @param key The key
 * @param value The number
 */
static void box_gpu_key_f64(
    ecs_vec_t *key,
    double value)
{
    box_gpu_key_bytes(key, &value, ECS_SIZEOF(double));
}

/** Get the length of a string view.
 *
 * @param s The string view
 * @return Its length, 0 without data
 */
static size_t box_gpu_str_len(
    WGPUStringView s)
{
    size_t result = 0;
    if (s.data) {
        result = s.length == WGPU_STRLEN ? strlen(s.data) : s.length;
    }

    return result;
}

/** Append a string to a key, telling a string without data from an empty one.
 *
 * @param key The key
 * @param s The string
 */
static void box_gpu_key_str(
    ecs_vec_t *key,
    WGPUStringView s)
{
    size_t len = box_gpu_str_len(s);
    box_gpu_key_u64(key, s.data ? (uint64_t)len : UINT64_MAX);
    box_gpu_key_bytes(key, s.data, (ecs_size_t)len);
}

/** Append the module, entry point and override constants of a stage to a key.
 *
 * @param key The key
 * @param module The module
 * @param entry The entry point
 * @param constants The constants
 * @param count Constants
 */
static void box_gpu_key_stage(
    ecs_vec_t *key,
    WGPUShaderModule module,
    WGPUStringView entry,
    const WGPUConstantEntry *constants,
    size_t count)
{
    size_t i;
    box_gpu_key_u64(key, (uint64_t)(uintptr_t)module);
    box_gpu_key_str(key, entry);
    box_gpu_key_u64(key, (uint64_t)count);
    for (i = 0; i < count; i ++) {
        box_gpu_key_str(key, constants[i].key);
        box_gpu_key_f64(key, constants[i].value);
    }
}

/** Append the vertex buffers of a render pipeline to a key.
 *
 * @param key The key
 * @param vertex The vertex state
 */
static void box_gpu_key_buffers(
    ecs_vec_t *key,
    const WGPUVertexState *vertex)
{
    size_t i, j;
    box_gpu_key_u64(key, (uint64_t)vertex->bufferCount);
    for (i = 0; i < vertex->bufferCount; i ++) {
        const WGPUVertexBufferLayout *b = &vertex->buffers[i];
        box_gpu_key_u64(key, (uint64_t)b->stepMode);
        box_gpu_key_u64(key, b->arrayStride);
        box_gpu_key_u64(key, (uint64_t)b->attributeCount);
        for (j = 0; j < b->attributeCount; j ++) {
            box_gpu_key_u64(key, (uint64_t)b->attributes[j].format);
            box_gpu_key_u64(key, b->attributes[j].offset);
            box_gpu_key_u64(key, (uint64_t)b->attributes[j].shaderLocation);
        }
    }
}

/** Append a stencil face to a key.
 *
 * @param key The key
 * @param s The stencil face
 */
static void box_gpu_key_stencil(
    ecs_vec_t *key,
    const WGPUStencilFaceState *s)
{
    box_gpu_key_u64(key, (uint64_t)s->compare);
    box_gpu_key_u64(key, (uint64_t)s->failOp);
    box_gpu_key_u64(key, (uint64_t)s->depthFailOp);
    box_gpu_key_u64(key, (uint64_t)s->passOp);
}

/** Append the depth state of a render pipeline to a key.
 *
 * @param key The key
 * @param d The depth state, NULL for none
 */
static void box_gpu_key_depth(
    ecs_vec_t *key,
    const WGPUDepthStencilState *d)
{
    box_gpu_key_u64(key, d != NULL);
    if (d) {
        box_gpu_key_u64(key, (uint64_t)d->format);
        box_gpu_key_u64(key, (uint64_t)d->depthWriteEnabled);
        box_gpu_key_u64(key, (uint64_t)d->depthCompare);
        box_gpu_key_stencil(key, &d->stencilFront);
        box_gpu_key_stencil(key, &d->stencilBack);
        box_gpu_key_u64(key, (uint64_t)d->stencilReadMask);
        box_gpu_key_u64(key, (uint64_t)d->stencilWriteMask);
        box_gpu_key_u64(key, (uint64_t)(int64_t)d->depthBias);
        box_gpu_key_f64(key, (double)d->depthBiasSlopeScale);
        box_gpu_key_f64(key, (double)d->depthBiasClamp);
    }
}

/** Append the fragment state of a render pipeline to a key.
 *
 * @param key The key
 * @param f The fragment state, NULL for none
 */
static void box_gpu_key_fragment(
    ecs_vec_t *key,
    const WGPUFragmentState *f)
{
    size_t i;
    box_gpu_key_u64(key, f != NULL);
    if (f) {
        box_gpu_key_stage(key, f->module, f->entryPoint, f->constants, f->constantCount);
        box_gpu_key_u64(key, (uint64_t)f->targetCount);
        for (i = 0; i < f->targetCount; i ++) {
            const WGPUColorTargetState *t = &f->targets[i];
            const WGPUBlendState *b = t->blend;
            box_gpu_key_u64(key, (uint64_t)t->format);
            box_gpu_key_u64(key, (uint64_t)t->writeMask);
            box_gpu_key_u64(key, b != NULL);
            if (b) {
                box_gpu_key_u64(key, (uint64_t)b->color.operation);
                box_gpu_key_u64(key, (uint64_t)b->color.srcFactor);
                box_gpu_key_u64(key, (uint64_t)b->color.dstFactor);
                box_gpu_key_u64(key, (uint64_t)b->alpha.operation);
                box_gpu_key_u64(key, (uint64_t)b->alpha.srcFactor);
                box_gpu_key_u64(key, (uint64_t)b->alpha.dstFactor);
            }
        }
    }
}

/** Write the key of a render pipeline descriptor: every value that tells two pipelines apart.
 *
 * @param key The key, cleared first
 * @param desc The descriptor
 */
static void box_gpu_key_render(
    ecs_vec_t *key,
    const WGPURenderPipelineDescriptor *desc)
{
    const WGPUPrimitiveState *p = &desc->primitive;
    ecs_vec_clear(key);
    box_gpu_key_u64(key, 0);
    box_gpu_key_str(key, desc->label);
    box_gpu_key_u64(key, (uint64_t)(uintptr_t)desc->layout);
    box_gpu_key_stage(key, desc->vertex.module, desc->vertex.entryPoint,
        desc->vertex.constants, desc->vertex.constantCount);
    box_gpu_key_buffers(key, &desc->vertex);
    box_gpu_key_u64(key, (uint64_t)p->topology);
    box_gpu_key_u64(key, (uint64_t)p->stripIndexFormat);
    box_gpu_key_u64(key, (uint64_t)p->frontFace);
    box_gpu_key_u64(key, (uint64_t)p->cullMode);
    box_gpu_key_u64(key, (uint64_t)p->unclippedDepth);
    box_gpu_key_depth(key, desc->depthStencil);
    box_gpu_key_u64(key, (uint64_t)desc->multisample.count);
    box_gpu_key_u64(key, (uint64_t)desc->multisample.mask);
    box_gpu_key_u64(key, (uint64_t)desc->multisample.alphaToCoverageEnabled);
    box_gpu_key_fragment(key, desc->fragment);
}

/** Write the key of a compute pipeline descriptor.
 *
 * @param key The key, cleared first
 * @param desc The descriptor
 */
static void box_gpu_key_compute(
    ecs_vec_t *key,
    const WGPUComputePipelineDescriptor *desc)
{
    ecs_vec_clear(key);
    box_gpu_key_u64(key, 1);
    box_gpu_key_str(key, desc->label);
    box_gpu_key_u64(key, (uint64_t)(uintptr_t)desc->layout);
    box_gpu_key_stage(key, desc->compute.module, desc->compute.entryPoint,
        desc->compute.constants, desc->compute.constantCount);
}

/** Hash a key, FNV-1a.
 *
 * @param key The key
 * @return The hash
 */
static uint64_t box_gpu_key_hash(
    const ecs_vec_t *key)
{
    const uint8_t *data = ecs_vec_first(key);
    uint64_t result = 14695981039346656037ull;
    int32_t i, count = ecs_vec_count(key);
    for (i = 0; i < count; i ++) {
        result = (result ^ data[i]) * 1099511628211ull;
    }

    return result;
}

/** Copy memory a descriptor of a pipeline points into, owned by the pipeline.
 *
 * @param p The pipeline
 * @param data The memory, NULL for none
 * @param size Bytes of the memory
 * @return The copy, NULL without memory
 */
static void* box_gpu_pipeline_dup(
    box_gpu_pipeline_t *p,
    const void *data,
    size_t size)
{
    void *result = NULL;
    if (data && size) {
        result = ecs_os_memdup(data, (ecs_size_t)size);
        *ecs_vec_append_t(NULL, &p->owned, void*) = result;
    }

    return result;
}

/** Copy a string a descriptor of a pipeline points to, owned by the pipeline.
 *
 * @param p The pipeline
 * @param s The string
 * @return The copy, terminated, the string itself without data
 */
static WGPUStringView box_gpu_pipeline_str(
    box_gpu_pipeline_t *p,
    WGPUStringView s)
{
    WGPUStringView result = s;
    size_t len = box_gpu_str_len(s);
    if (s.data) {
        char *copy = ecs_os_malloc((ecs_size_t)len + 1);
        ecs_os_memcpy(copy, s.data, (ecs_size_t)len);
        copy[len] = '\0';
        *ecs_vec_append_t(NULL, &p->owned, void*) = copy;
        result.data = copy;
        result.length = len;
    }

    return result;
}

/** Copy the override constants of a stage, owned by the pipeline.
 *
 * @param p The pipeline
 * @param constants The constants
 * @param count Constants
 * @return The copy, NULL without constants
 */
static const WGPUConstantEntry* box_gpu_pipeline_constants(
    box_gpu_pipeline_t *p,
    const WGPUConstantEntry *constants,
    size_t count)
{
    WGPUConstantEntry *result = box_gpu_pipeline_dup(p, constants,
        count * sizeof(WGPUConstantEntry));
    size_t i;
    for (i = 0; result && i < count; i ++) {
        result[i].nextInChain = NULL;
        result[i].key = box_gpu_pipeline_str(p, result[i].key);
    }

    return result;
}

/** Copy the vertex state of a render pipeline into the pipeline.
 *
 * @param p The pipeline
 * @param vertex The vertex state
 */
static void box_gpu_pipeline_vertex(
    box_gpu_pipeline_t *p,
    const WGPUVertexState *vertex)
{
    WGPUVertexState *v = &p->render.vertex;
    WGPUVertexBufferLayout *buffers;
    size_t i;
    *v = *vertex;
    v->nextInChain = NULL;
    v->entryPoint = box_gpu_pipeline_str(p, vertex->entryPoint);
    v->constants = box_gpu_pipeline_constants(p, vertex->constants, vertex->constantCount);
    buffers = box_gpu_pipeline_dup(p, vertex->buffers,
        vertex->bufferCount * sizeof(WGPUVertexBufferLayout));
    for (i = 0; buffers && i < vertex->bufferCount; i ++) {
        WGPUVertexAttribute *attrs = box_gpu_pipeline_dup(p, buffers[i].attributes,
            buffers[i].attributeCount * sizeof(WGPUVertexAttribute));
        size_t j;
        for (j = 0; attrs && j < buffers[i].attributeCount; j ++) {
            attrs[j].nextInChain = NULL;
        }

        buffers[i].nextInChain = NULL;
        buffers[i].attributes = attrs;
    }

    v->buffers = buffers;
    if (v->module) {
        wgpuShaderModuleAddRef(v->module);
    }
}

/** Copy the fragment state of a render pipeline into the pipeline.
 *
 * @param p The pipeline
 * @param fragment The fragment state
 */
static void box_gpu_pipeline_fragment(
    box_gpu_pipeline_t *p,
    const WGPUFragmentState *fragment)
{
    WGPUFragmentState *f = &p->fragment;
    WGPUColorTargetState *targets;
    size_t i;
    *f = *fragment;
    f->nextInChain = NULL;
    f->entryPoint = box_gpu_pipeline_str(p, fragment->entryPoint);
    f->constants = box_gpu_pipeline_constants(p, fragment->constants, fragment->constantCount);
    targets = box_gpu_pipeline_dup(p, fragment->targets,
        fragment->targetCount * sizeof(WGPUColorTargetState));
    for (i = 0; targets && i < fragment->targetCount; i ++) {
        targets[i].nextInChain = NULL;
        targets[i].blend = box_gpu_pipeline_dup(p, targets[i].blend, sizeof(WGPUBlendState));
    }

    f->targets = targets;
    if (f->module) {
        wgpuShaderModuleAddRef(f->module);
    }

    p->render.fragment = f;
}

/** Create a pipeline of the cache from a copy of its descriptor, holding references of its
 * modules and layout.
 *
 * @param render The descriptor of a render pipeline, NULL for a compute pipeline
 * @param compute The descriptor of a compute pipeline
 * @return The pipeline, not in the cache yet
 */
static box_gpu_pipeline_t* box_gpu_pipeline_new(
    const WGPURenderPipelineDescriptor *render,
    const WGPUComputePipelineDescriptor *compute)
{
    box_gpu_pipeline_t *p = ecs_os_calloc_t(box_gpu_pipeline_t);
    ecs_vec_init_t(NULL, &p->owned, void*, 0);
    ecs_vec_init_t(NULL, &p->key, uint8_t, 0);
    p->compute = render == NULL;
    if (render) {
        ecs_assert(!render->nextInChain, ECS_UNSUPPORTED, "chained render pipeline descriptor");
        p->render = *render;
        p->render.label = box_gpu_pipeline_str(p, render->label);
        box_gpu_pipeline_vertex(p, &render->vertex);
        p->render.primitive.nextInChain = NULL;
        p->render.multisample.nextInChain = NULL;
        if (render->depthStencil) {
            p->depth = *render->depthStencil;
            p->depth.nextInChain = NULL;
            p->render.depthStencil = &p->depth;
        }

        if (render->fragment) {
            box_gpu_pipeline_fragment(p, render->fragment);
        }

        if (render->layout) {
            wgpuPipelineLayoutAddRef(render->layout);
        }
    } else {
        ecs_assert(!compute->nextInChain, ECS_UNSUPPORTED, "chained compute pipeline descriptor");
        p->compute_desc = *compute;
        p->compute_desc.label = box_gpu_pipeline_str(p, compute->label);
        p->compute_desc.compute.nextInChain = NULL;
        p->compute_desc.compute.entryPoint = box_gpu_pipeline_str(p,
            compute->compute.entryPoint);
        p->compute_desc.compute.constants = box_gpu_pipeline_constants(p,
            compute->compute.constants, compute->compute.constantCount);
        if (compute->compute.module) {
            wgpuShaderModuleAddRef(compute->compute.module);
        }

        if (compute->layout) {
            wgpuPipelineLayoutAddRef(compute->layout);
        }
    }

    return p;
}

/** Free a pipeline of the cache: its pipeline, the references of its descriptor and the copies.
 *
 * @param p The pipeline
 */
static void box_gpu_pipeline_free(
    box_gpu_pipeline_t *p)
{
    int32_t i;
    if (p->render_pipeline) {
        wgpuRenderPipelineRelease(p->render_pipeline);
    }

    if (p->compute_pipeline) {
        wgpuComputePipelineRelease(p->compute_pipeline);
    }

    if (!p->compute && p->render.vertex.module) {
        wgpuShaderModuleRelease(p->render.vertex.module);
    }

    if (!p->compute && p->render.fragment && p->fragment.module) {
        wgpuShaderModuleRelease(p->fragment.module);
    }

    if (!p->compute && p->render.layout) {
        wgpuPipelineLayoutRelease(p->render.layout);
    }

    if (p->compute && p->compute_desc.compute.module) {
        wgpuShaderModuleRelease(p->compute_desc.compute.module);
    }

    if (p->compute && p->compute_desc.layout) {
        wgpuPipelineLayoutRelease(p->compute_desc.layout);
    }

    for (i = 0; i < ecs_vec_count(&p->owned); i ++) {
        ecs_os_free(*ecs_vec_get_t(&p->owned, void*, i));
    }

    ecs_vec_fini_t(NULL, &p->owned, void*);
    ecs_vec_fini_t(NULL, &p->key, uint8_t);
    ecs_os_free(p->message);
    ecs_os_free(p);
}

char* box_gpu_pipeline_message(
    const char *text,
    int32_t length)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const char *end = text ? text + length : NULL;
    const char *line = text;
    int32_t lines = 0;
    while (line && line < end) {
        const char *next = memchr(line, '\n', (size_t)(end - line));
        const char *stop = next ? next : end;
        while (line < stop && (*line == ' ' || *line == '\t')) {
            line ++;
        }

        bool skip = line == stop || !strncmp(line, "Validation Error", 16) ||
            !strncmp(line, "Caused by:", 10) || !strncmp(line, "In wgpu", 7);
        if (!skip) {
            ecs_strbuf_append(&buf, "%s%.*s", lines ++ ? ": " : "", (int)(stop - line), line);
        }

        line = next ? next + 1 : end;
    }

    if (!lines) {
        ecs_strbuf_appendstr(&buf, "the pipeline cannot be created");
    }

    return ecs_strbuf_get(&buf);
}

void box_gpu_error_capture_begin(
    box_gpu_error_capture_t *capture)
{
    box_gpu_capturing = capture;
}

void box_gpu_error_capture_end(void)
{
    box_gpu_capturing = NULL;
}

bool box_gpu_error_capture_take(
    WGPUStringView message)
{
    box_gpu_error_capture_t *c = box_gpu_capturing;
    if (c && !c->message) {
        c->message = flecs_asprintf("%.*s", (int)box_gpu_str_len(message),
            message.data ? message.data : "");
    }

    if (c) {
        c->failed = true;
    }

    return c != NULL;
}

/** Create a pipeline of the cache with the synchronous call, on a worker or the render thread;
 * the device errors the call reports on this thread mark it failed.
 *
 * @param device The device
 * @param p The pipeline
 */
static void box_gpu_pipeline_create(
    WGPUDevice device,
    box_gpu_pipeline_t *p)
{
    box_gpu_error_capture_t capture = {0};
    ecs_time_t start = {0};
    ecs_time_measure(&start);
    box_gpu_error_capture_begin(&capture);
    if (p->compute) {
        p->compute_pipeline = wgpuDeviceCreateComputePipeline(device, &p->compute_desc);
    } else {
        p->render_pipeline = wgpuDeviceCreateRenderPipeline(device, &p->render);
    }

    box_gpu_error_capture_end();
    p->seconds = ecs_time_measure(&start);
    p->failed = capture.failed || (!p->compute_pipeline && !p->render_pipeline);
    if (capture.message) {
        p->message = box_gpu_pipeline_message(capture.message, ecs_os_strlen(capture.message));
        ecs_os_free(capture.message);
    }

    if (p->failed && p->compute_pipeline) {
        wgpuComputePipelineRelease(p->compute_pipeline);
        p->compute_pipeline = NULL;
    }

    if (p->failed && p->render_pipeline) {
        wgpuRenderPipelineRelease(p->render_pipeline);
        p->render_pipeline = NULL;
    }
}

#ifdef __EMSCRIPTEN__

/** Queue the outcome of a creation for its delivery, or free the pipeline when its cache is
 * gone.
 *
 * @param p The pipeline
 */
static void box_gpu_pipeline_arrive(
    box_gpu_pipeline_t *p)
{
    box_gpu_pipelines_t *c = p->cache;
    if (!c) {
        box_gpu_pipeline_free(p);
        goto done;
    }

    ecs_os_mutex_lock(c->lock);
    p->arrived = true;
    p->queued = c->arrived;
    c->arrived = p;
    ecs_os_cond_broadcast(c->done);
    ecs_os_mutex_unlock(c->lock);
done:
    return;
}

/** Take the outcome of the creation of a render pipeline, which arrives from the event loop.
 *
 * @param status Outcome of the creation
 * @param pipeline The pipeline on success
 * @param message Error text
 * @param userdata1 The box_gpu_pipeline_t
 * @param userdata2 Unused
 */
static void box_gpu_pipeline_on_render(
    WGPUCreatePipelineAsyncStatus status,
    WGPURenderPipeline pipeline,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_pipeline_t *p = userdata1;
    (void)userdata2;
    p->seconds = ecs_time_measure(&p->start);
    p->failed = status != WGPUCreatePipelineAsyncStatus_Success || !pipeline;
    if (p->failed && pipeline) {
        wgpuRenderPipelineRelease(pipeline);
    } else if (!p->failed) {
        p->render_pipeline = pipeline;
    }

    if (p->failed) {
        p->message = box_gpu_pipeline_message(message.data, (int32_t)box_gpu_str_len(message));
    }

    box_gpu_pipeline_arrive(p);
}

/** Take the outcome of the creation of a compute pipeline, which arrives from the event loop.
 *
 * @param status Outcome of the creation
 * @param pipeline The pipeline on success
 * @param message Error text
 * @param userdata1 The box_gpu_pipeline_t
 * @param userdata2 Unused
 */
static void box_gpu_pipeline_on_compute(
    WGPUCreatePipelineAsyncStatus status,
    WGPUComputePipeline pipeline,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_pipeline_t *p = userdata1;
    (void)userdata2;
    p->seconds = ecs_time_measure(&p->start);
    p->failed = status != WGPUCreatePipelineAsyncStatus_Success || !pipeline;
    if (p->failed && pipeline) {
        wgpuComputePipelineRelease(pipeline);
    } else if (!p->failed) {
        p->compute_pipeline = pipeline;
    }

    if (p->failed) {
        p->message = box_gpu_pipeline_message(message.data, (int32_t)box_gpu_str_len(message));
    }

    box_gpu_pipeline_arrive(p);
}

/** Start the creation of a pipeline with the asynchronous call of the browser.
 *
 * @param c The cache
 * @param p The pipeline
 */
static void box_gpu_pipeline_start(
    box_gpu_pipelines_t *c,
    box_gpu_pipeline_t *p)
{
    ecs_time_measure(&p->start);
    if (p->compute) {
        wgpuDeviceCreateComputePipelineAsync(c->device, &p->compute_desc,
            (WGPUCreateComputePipelineAsyncCallbackInfo){
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = box_gpu_pipeline_on_compute,
                .userdata1 = p
            });
    } else {
        wgpuDeviceCreateRenderPipelineAsync(c->device, &p->render,
            (WGPUCreateRenderPipelineAsyncCallbackInfo){
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = box_gpu_pipeline_on_render,
                .userdata1 = p
            });
    }
}

#else

/** Create the queued pipelines, newest first, until the cache stops.
 *
 * @param arg The box_gpu_pipelines_t
 * @return NULL
 */
static void* box_gpu_pipeline_worker(
    void *arg)
{
    box_gpu_pipelines_t *c = arg;
    ecs_os_mutex_lock(c->lock);
    while (!c->stop) {
        int32_t level = c->queue[0] ? 0 : 1;
        box_gpu_pipeline_t *p = c->queue[level];
        if (!p) {
            ecs_os_cond_wait(c->wake, c->lock);
            continue;
        }

        c->queue[level] = p->queued;
        p->queued = NULL;
        c->active ++;
        ecs_os_mutex_unlock(c->lock);
        box_gpu_pipeline_create(c->device, p);
        ecs_os_mutex_lock(c->lock);
        c->active --;
        p->arrived = true;
        p->queued = c->arrived;
        c->arrived = p;
        ecs_os_cond_broadcast(c->done);
    }

    ecs_os_mutex_unlock(c->lock);

    return NULL;
}

/** Queue the creation of a pipeline for the workers, starting them on first use.
 *
 * The newest request goes first, as the startup overlay draws last in a frame and its pipelines
 * are the last ones requested. Render pipelines go before compute pipelines and the quiet ones
 * of the forward check, so draws such as those of the overlay are not held back by them.
 *
 * @param c The cache
 * @param p The pipeline
 */
static void box_gpu_pipeline_start(
    box_gpu_pipelines_t *c,
    box_gpu_pipeline_t *p)
{
    int32_t level = p->compute || p->quiet;
    while (c->workers < BOX_GPU_PIPELINE_WORKERS) {
        c->threads[c->workers ++] = ecs_os_thread_new(box_gpu_pipeline_worker, c);
    }

    ecs_os_mutex_lock(c->lock);
    p->queued = c->queue[level];
    c->queue[level] = p;
    ecs_os_cond_signal(c->wake);
    ecs_os_mutex_unlock(c->lock);
}

#endif

/** Get the cache of a backend, created on first use.
 *
 * @param gpu The backend
 * @return The cache
 */
static box_gpu_pipelines_t* box_gpu_pipelines_get(
    box_gpu_t *gpu)
{
    box_gpu_pipelines_t *c = gpu->pipelines;
    if (!c) {
        c = ecs_os_calloc_t(box_gpu_pipelines_t);
        ecs_map_init(&c->entries, NULL);
        ecs_vec_init_t(NULL, &c->scratch, uint8_t, 0);
        c->device = gpu->device;
        c->lock = ecs_os_mutex_new();
        c->wake = ecs_os_cond_new();
        c->done = ecs_os_cond_new();
        ecs_os_mutex_lock(gpu->lock);
        c->hold = gpu->delivery == BOX_RENDER_COMPILE_HOLD;
        ecs_os_mutex_unlock(gpu->lock);
        gpu->pipelines = c;
    }

    return c;
}

/** Count a pipeline creation that starts or ends in the counters the main thread reads.
 *
 * @param gpu The backend
 * @param start Whether the creation starts, else it ended
 * @param created Whether the ended creation made a pipeline
 */
static void box_gpu_pipeline_count(
    box_gpu_t *gpu,
    bool start,
    bool created)
{
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.pipelines_building += start ? 1 : -1;
    gpu->stats.pipelines += created ? 1 : 0;
    ecs_os_mutex_unlock(gpu->lock);
}

/** Log the creation of a pipeline with its shader, entry points and duration.
 *
 * @param p The pipeline, created
 */
static void box_gpu_pipeline_log(
    const box_gpu_pipeline_t *p)
{
    WGPUStringView first = p->compute ? p->compute_desc.compute.entryPoint
        : p->render.vertex.entryPoint;
    WGPUStringView second = {0};
    if (!p->compute && p->render.fragment) {
        second = p->fragment.entryPoint;
    }

    int first_len = (int)box_gpu_str_len(first);
    int second_len = (int)box_gpu_str_len(second);
    ecs_trace("created %s pipeline '%s' (%.*s%s%.*s) in %.2f ms",
        p->compute ? "compute" : "render", p->shader ? p->shader : "unknown",
        first_len, first.data ? first.data : "", second_len ? ", " : "",
        second_len, second.data ? second.data : "", p->seconds * 1000.0);
}

/** Deliver the outcome of a creation: the pipeline is ready, or it failed and is reported
 * like a shader that failed to compile.
 *
 * @param gpu The backend
 * @param p The pipeline
 */
static void box_gpu_pipeline_finish(
    box_gpu_t *gpu,
    box_gpu_pipeline_t *p)
{
    const char *label = p->compute ? p->compute_desc.label.data : p->render.label.data;
    p->arrived = false;
    p->state = p->failed ? BOX_GPU_PIPELINE_FAILED : BOX_GPU_PIPELINE_READY;
    p->cache->building --;
    box_gpu_pipeline_count(gpu, false, !p->failed);
    if (!p->failed) {
        box_gpu_pipeline_log(p);
    }

    if (p->failed && !p->quiet) {
        ecs_err("pipeline '%s' failed to create: %s", label ? label : "",
            p->message ? p->message : "the pipeline cannot be created");
        box_gpu_count(gpu, BOX_GPU_STAT_SHADER_ERROR);
    }
}

/** Find the pipeline of a key in the cache.
 *
 * @param c The cache
 * @param hash Hash of the key
 * @param key The key
 * @return The pipeline, NULL when the cache does not have it
 */
static box_gpu_pipeline_t* box_gpu_pipeline_find(
    box_gpu_pipelines_t *c,
    uint64_t hash,
    const ecs_vec_t *key)
{
    box_gpu_pipeline_t *result = ecs_map_get_deref(&c->entries, box_gpu_pipeline_t, hash);
    int32_t size = ecs_vec_count(key);
    while (result && (ecs_vec_count(&result->key) != size ||
        ecs_os_memcmp(ecs_vec_first(&result->key), ecs_vec_first(key), size)))
    {
        result = result->next;
    }

    return result;
}

/** Find or start the pipeline of a descriptor, see box_gpu_render_pipeline.
 *
 * @param gpu The backend
 * @param render The descriptor of a render pipeline, NULL for a compute pipeline
 * @param compute The descriptor of a compute pipeline
 * @return The pipeline of the cache
 */
static box_gpu_pipeline_t* box_gpu_pipeline_request(
    box_gpu_t *gpu,
    const WGPURenderPipelineDescriptor *render,
    const WGPUComputePipelineDescriptor *compute)
{
    box_gpu_pipelines_t *c = box_gpu_pipelines_get(gpu);
    uint64_t hash;
    if (render) {
        box_gpu_key_render(&c->scratch, render);
    } else {
        box_gpu_key_compute(&c->scratch, compute);
    }

    hash = box_gpu_key_hash(&c->scratch);
    box_gpu_pipeline_t *p = box_gpu_pipeline_find(c, hash, &c->scratch);
    if (p) {
        goto done;
    }

    p = box_gpu_pipeline_new(render, compute);
    p->hash = hash;
    p->cache = c;
    p->quiet = gpu->pipeline_quiet;
    const char *shader = box_gpu_shader_label(gpu, render ? render->vertex.module
        : compute->compute.module);
    if (shader) {
        p->shader = box_gpu_pipeline_dup(p, shader, ecs_os_strlen(shader) + 1);
    }

    box_gpu_key_bytes(&p->key, ecs_vec_first(&c->scratch), ecs_vec_count(&c->scratch));
    p->next = ecs_map_get_deref(&c->entries, box_gpu_pipeline_t, hash);
    *ecs_map_ensure(&c->entries, hash) = (ecs_map_val_t)(uintptr_t)p;
    box_gpu_pipeline_count(gpu, true, false);
    c->building ++;
    if (gpu->pipeline_wait && !c->hold) {
        box_gpu_pipeline_create(c->device, p);
        box_gpu_pipeline_finish(gpu, p);
    } else {
        box_gpu_pipeline_start(c, p);
    }

done:
    p->used = c->packets;
    gpu->pipeline_error = p->state == BOX_GPU_PIPELINE_FAILED
        ? (p->message ? p->message : "the pipeline cannot be created")
        : NULL;

    return p;
}

WGPURenderPipeline box_gpu_render_pipeline(
    box_gpu_t *gpu,
    const WGPURenderPipelineDescriptor *desc)
{
    box_gpu_pipeline_t *p = box_gpu_pipeline_request(gpu, desc, NULL);
    WGPURenderPipeline result = NULL;
    if (p->state == BOX_GPU_PIPELINE_READY) {
        result = p->render_pipeline;
        wgpuRenderPipelineAddRef(result);
    }

    return result;
}

WGPUComputePipeline box_gpu_compute_pipeline(
    box_gpu_t *gpu,
    const WGPUComputePipelineDescriptor *desc)
{
    box_gpu_pipeline_t *p = box_gpu_pipeline_request(gpu, NULL, desc);
    WGPUComputePipeline result = NULL;
    if (p->state == BOX_GPU_PIPELINE_READY) {
        result = p->compute_pipeline;
        wgpuComputePipelineAddRef(result);
    }

    return result;
}

int32_t box_gpu_pipelines_building(
    const box_gpu_t *gpu)
{
    return gpu->pipelines ? gpu->pipelines->building : 0;
}

bool box_gpu_render_pipelines_swap(
    WGPURenderPipeline *const *current,
    WGPURenderPipeline *next,
    int32_t count)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < count; i ++) {
        result = result && next[i] != NULL;
    }

    for (i = 0; i < count; i ++) {
        WGPURenderPipeline release = result ? *current[i] : next[i];
        if (release) {
            wgpuRenderPipelineRelease(release);
        }

        if (result) {
            *current[i] = next[i];
        }

        next[i] = NULL;
    }

    return result;
}

bool box_gpu_compute_pipelines_swap(
    WGPUComputePipeline *const *current,
    WGPUComputePipeline *next,
    int32_t count)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < count; i ++) {
        result = result && next[i] != NULL;
    }

    for (i = 0; i < count; i ++) {
        WGPUComputePipeline release = result ? *current[i] : next[i];
        if (release) {
            wgpuComputePipelineRelease(release);
        }

        if (result) {
            *current[i] = next[i];
        }

        next[i] = NULL;
    }

    return result;
}

void box_gpu_compute_pipelines_release(
    WGPUComputePipeline *pipelines,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (pipelines[i]) {
            wgpuComputePipelineRelease(pipelines[i]);
            pipelines[i] = NULL;
        }
    }
}

#ifndef __EMSCRIPTEN__
/** Check whether a packet captures a target, which waits for its pipelines like a headless run.
 *
 * @param packet The packet
 * @return Whether a target of the packet has a capture path
 */
static bool box_gpu_pipelines_captures(
    const box_render_packet_t *packet)
{
    bool result = false;
    int32_t i;
    for (i = 0; !result && i < ecs_vec_count(&packet->targets); i ++) {
        result = ecs_vec_get_t(&packet->targets, box_render_target_t, i)->capture_size != 0;
    }

    return result;
}
#endif

/** Remove the pipelines whose time in the cache is up: created ones not requested for
 * BOX_GPU_PIPELINE_KEEP packets. Failed ones stay, so they are not created and reported again.
 *
 * @param c The cache
 */
static void box_gpu_pipelines_sweep(
    box_gpu_pipelines_t *c)
{
    ecs_vec_t empty;
    int32_t i;
    ecs_vec_init_t(NULL, &empty, uint64_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&c->entries);
    while (ecs_map_next(&it)) {
        ecs_map_val_t *val = ecs_map_get(&c->entries, ecs_map_key(&it));
        box_gpu_pipeline_t *p = (box_gpu_pipeline_t*)(uintptr_t)*val, *kept = NULL;
        box_gpu_pipeline_t **tail = &kept;
        while (p) {
            box_gpu_pipeline_t *next = p->next;
            if (p->state == BOX_GPU_PIPELINE_READY &&
                c->packets - p->used > BOX_GPU_PIPELINE_KEEP)
            {
                box_gpu_pipeline_free(p);
            } else {
                *tail = p;
                tail = &p->next;
            }

            p = next;
        }

        *tail = NULL;
        *val = (ecs_map_val_t)(uintptr_t)kept;
        if (!kept) {
            *ecs_vec_append_t(NULL, &empty, uint64_t) = ecs_map_key(&it);
        }
    }

    for (i = 0; i < ecs_vec_count(&empty); i ++) {
        ecs_map_remove(&c->entries, *ecs_vec_get_t(&empty, uint64_t, i));
    }

    ecs_vec_fini_t(NULL, &empty, uint64_t);
}

void box_gpu_pipelines_deliver(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_pipelines_t *c = gpu->pipelines;
    box_gpu_pipeline_t *arrived = NULL;
#ifndef __EMSCRIPTEN__
    gpu->pipeline_wait = packet->headless || box_gpu_pipelines_captures(packet);
#else
    (void)packet;
#endif
    if (!c) {
        goto done;
    }

    c->packets ++;
    ecs_os_mutex_lock(gpu->lock);
    c->hold = gpu->delivery == BOX_RENDER_COMPILE_HOLD;
    ecs_os_mutex_unlock(gpu->lock);
    if (!c->hold) {
        ecs_os_mutex_lock(c->lock);
#ifndef __EMSCRIPTEN__
        while (gpu->pipeline_wait && (c->queue[0] || c->queue[1] || c->active)) {
            ecs_os_cond_wait(c->done, c->lock);
        }
#endif
        arrived = c->arrived;
        c->arrived = NULL;
        ecs_os_mutex_unlock(c->lock);
    }

    while (arrived) {
        box_gpu_pipeline_t *next = arrived->queued;
        arrived->queued = NULL;
        box_gpu_pipeline_finish(gpu, arrived);
        arrived = next;
    }

    if (c->packets % BOX_GPU_PIPELINE_SWEEP == 0) {
        box_gpu_pipelines_sweep(c);
    }

done:
    return;
}

void box_gpu_pipelines_fini(
    box_gpu_t *gpu)
{
    box_gpu_pipelines_t *c = gpu->pipelines;
    int32_t i, building = 0;
    if (!c) {
        goto done;
    }

    ecs_os_mutex_lock(c->lock);
    c->stop = true;
    ecs_os_cond_broadcast(c->wake);
    ecs_os_mutex_unlock(c->lock);
    for (i = 0; i < c->workers; i ++) {
        ecs_os_thread_join(c->threads[i]);
    }

    ecs_map_iter_t it = ecs_map_iter(&c->entries);
    while (ecs_map_next(&it)) {
        box_gpu_pipeline_t *p = ecs_map_ptr(&it);
        while (p) {
            box_gpu_pipeline_t *next = p->next;
            building += p->state == BOX_GPU_PIPELINE_BUILDING;
#ifdef __EMSCRIPTEN__
            if (p->state == BOX_GPU_PIPELINE_BUILDING && !p->arrived) {
                p->cache = NULL;
            } else {
                box_gpu_pipeline_free(p);
            }
#else
            box_gpu_pipeline_free(p);
#endif
            p = next;
        }
    }

    ecs_map_fini(&c->entries);
    ecs_vec_fini_t(NULL, &c->scratch, uint8_t);
    ecs_os_cond_free(c->wake);
    ecs_os_cond_free(c->done);
    ecs_os_mutex_free(c->lock);
    ecs_os_free(c);
    gpu->pipelines = NULL;
    gpu->pipeline_error = NULL;
    ecs_os_mutex_lock(gpu->lock);
    gpu->stats.pipelines_building -= building;
    ecs_os_mutex_unlock(gpu->lock);
done:
    return;
}

#endif
