/**
 * @file render/gpu_portal_stencil.c
 * @brief Portal marks of the webgpu backend, see gpu_portal_stencil.h.
 */

#include "gpu_portal_stencil.h"

#ifdef BOX_RENDER_WGPU

/** Color formats the marks have pipelines for: the formats of the targets and the HDR format */
#define BOX_GPU_PORTAL_STENCIL_FORMATS (4)

/** Draws of one mark */
#define BOX_GPU_PORTAL_STENCIL_STEPS (3)

/** Vertices the buffer has room for at least */
#define BOX_GPU_PORTAL_STENCIL_MIN_VERTS (256)

/** Positions the marks in clip space with the matrix that leads the View of the mesh shaders */
static const char *box_gpu_portal_stencil_wgsl =
    "struct View {\n"
    "    view_proj: mat4x4f,\n"
    "}\n"
    "\n"
    "@group(0) @binding(0) var<uniform> view: View;\n"
    "\n"
    "@vertex\n"
    "fn vs(@location(0) position: vec4f) -> @builtin(position) vec4f {\n"
    "    return view.view_proj * vec4f(position.xyz, 1.0);\n"
    "}\n"
    "\n"
    "@fragment\n"
    "fn fs() -> @location(0) vec4f {\n"
    "    return vec4f(0.0);\n"
    "}\n";

/** The pipelines of the marks for one color format */
typedef struct box_gpu_portal_stencil_format_t {
    WGPUTextureFormat format;       /**< Color format, Undefined for an unused slot */
    WGPURenderPipeline steps[BOX_GPU_PORTAL_STENCIL_STEPS]; /**< A, B and C */
} box_gpu_portal_stencil_format_t;

struct box_gpu_portal_stencil_t {
    WGPUShaderModule shader;        /**< The shader of the marks */
    WGPUPipelineLayout layout;      /**< Only the view layout at 0 */
    box_gpu_portal_stencil_format_t formats[BOX_GPU_PORTAL_STENCIL_FORMATS]; /**< Pipelines */
    WGPUBuffer verts;               /**< Vertices of the marks of the packet, vec4 */
    uint64_t size;                  /**< Bytes of the vertex buffer */
    uint64_t used;                  /**< Bytes uploaded in this frame */
};

void box_gpu_portal_stencil_init(
    box_gpu_t *gpu)
{
    box_gpu_portal_stencil_t *s = ecs_os_calloc_t(box_gpu_portal_stencil_t);
    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = box_gpu_portal_stencil_wgsl, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor desc = {
        .nextInChain = &wgsl.chain,
        .label = { .data = "portal_stencil", .length = WGPU_STRLEN }
    };

    s->shader = wgpuDeviceCreateShaderModule(gpu->device, &desc);
    s->layout = wgpuDeviceCreatePipelineLayout(gpu->device, &(WGPUPipelineLayoutDescriptor){
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &gpu->view_layout
    });

    gpu->portal_stencil = s;
}

void box_gpu_portal_stencil_fini(
    box_gpu_t *gpu)
{
    box_gpu_portal_stencil_t *s = gpu->portal_stencil;
    int32_t i, k;

    if (!s) {
        goto done;
    }

    for (i = 0; i < BOX_GPU_PORTAL_STENCIL_FORMATS; i ++) {
        for (k = 0; k < BOX_GPU_PORTAL_STENCIL_STEPS; k ++) {
            if (s->formats[i].steps[k]) {
                wgpuRenderPipelineRelease(s->formats[i].steps[k]);
            }
        }
    }

    if (s->verts) {
        wgpuBufferRelease(s->verts);
    }

    wgpuPipelineLayoutRelease(s->layout);
    wgpuShaderModuleRelease(s->shader);
    ecs_os_free(s);
    gpu->portal_stencil = NULL;
done:
    return;
}

/** Create the pipeline of one draw of a mark.
 *
 * @param gpu The backend
 * @param format The color format of the pass
 * @param step 0 for A, 1 for B, 2 for C
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_portal_stencil_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    int32_t step)
{
    box_gpu_portal_stencil_t *s = gpu->portal_stencil;
    WGPUStencilFaceState face = {
        .compare = WGPUCompareFunction_Equal,
        .failOp = WGPUStencilOperation_Keep,
        .depthFailOp = WGPUStencilOperation_Keep,
        .passOp = step == 0 ? WGPUStencilOperation_Invert
            : step == 1 ? WGPUStencilOperation_Replace
            : WGPUStencilOperation_Zero
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_False,
        .depthCompare = step == 0
            ? WGPUCompareFunction_GreaterEqual : WGPUCompareFunction_Always,
        .stencilFront = face,
        .stencilBack = face,
        .stencilReadMask = step == 0
            ? BOX_RENDER_STENCIL_REGION | BOX_RENDER_STENCIL_DRY : BOX_RENDER_STENCIL_MARK,
        .stencilWriteMask = step == 1
            ? BOX_RENDER_STENCIL_REGION | BOX_RENDER_STENCIL_DRY : BOX_RENDER_STENCIL_MARK
    };
    WGPUVertexAttribute attribute = {
        .format = WGPUVertexFormat_Float32x4,
        .offset = 0,
        .shaderLocation = 0
    };
    WGPUVertexBufferLayout buffer = {
        .stepMode = WGPUVertexStepMode_Vertex,
        .arrayStride = sizeof(vec4),
        .attributeCount = 1,
        .attributes = &attribute
    };
    WGPUColorTargetState target = {
        .format = format,
        .writeMask = WGPUColorWriteMask_None
    };
    WGPUFragmentState fragment = {
        .module = s->shader,
        .entryPoint = { .data = "fs", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .label = { .data = "portal_stencil", .length = WGPU_STRLEN },
        .layout = s->layout,
        .vertex = {
            .module = s->shader,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN },
            .bufferCount = 1,
            .buffers = &buffer
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return wgpuDeviceCreateRenderPipeline(gpu->device, &desc);
}

/** Get the pipelines of the marks for a color format, created on first use.
 *
 * @param gpu The backend
 * @param format The color format of the pass
 * @return The pipelines, NULL when every slot holds another format
 */
static const box_gpu_portal_stencil_format_t* box_gpu_portal_stencil_format(
    box_gpu_t *gpu,
    WGPUTextureFormat format)
{
    box_gpu_portal_stencil_t *s = gpu->portal_stencil;
    box_gpu_portal_stencil_format_t *result = NULL;
    int32_t i, k;

    for (i = 0; i < BOX_GPU_PORTAL_STENCIL_FORMATS && !result; i ++) {
        box_gpu_portal_stencil_format_t *f = &s->formats[i];

        if (f->format == format) {
            result = f;
        } else if (f->format == WGPUTextureFormat_Undefined) {
            f->format = format;
            for (k = 0; k < BOX_GPU_PORTAL_STENCIL_STEPS; k ++) {
                f->steps[k] = box_gpu_portal_stencil_pipeline(gpu, format, k);
            }

            result = f;
        }
    }

    return result;
}

void box_gpu_portal_stencil_upload(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_portal_stencil_t *s = gpu->portal_stencil;
    int32_t count = ecs_vec_count(&packet->portal_mark_verts);
    uint64_t bytes = (uint64_t)count * sizeof(vec4);

    s->used = 0;
    if (!count) {
        goto done;
    }

    if (bytes > s->size) {
        uint64_t size = BOX_GPU_PORTAL_STENCIL_MIN_VERTS * sizeof(vec4);

        while (size < bytes) {
            size *= 2;
        }

        if (s->verts) {
            wgpuBufferRelease(s->verts);
        }

        s->verts = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .label = { .data = "portal_stencil", .length = WGPU_STRLEN },
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = size
        });
        s->size = size;
    }

    wgpuQueueWriteBuffer(gpu->queue, s->verts, 0,
        ecs_vec_first_t(&packet->portal_mark_verts, vec4), bytes);
    s->used = bytes;
done:
    return;
}

bool box_gpu_portal_stencil_draw(
    box_gpu_t *gpu,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat format,
    const box_render_packet_t *packet,
    const box_render_view_t *v)
{
    box_gpu_portal_stencil_t *s = gpu->portal_stencil;
    const box_gpu_portal_stencil_format_t *f = NULL;
    bool result = false;
    int32_t i;

    if (!s || !v->mark_count || !s->used) {
        goto done;
    }

    f = box_gpu_portal_stencil_format(gpu, format);
    if (!f) {
        goto done;
    }

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, s->verts, 0, s->used);
    for (i = 0; i < v->mark_count; i ++) {
        const box_render_portal_mark_t *mark = ecs_vec_get_t(&packet->portal_marks,
            box_render_portal_mark_t, v->mark_first + i);
        uint32_t first = (uint32_t)mark->first;
        uint32_t count = (uint32_t)mark->count;

        wgpuRenderPassEncoderSetPipeline(pass, f->steps[0]);
        wgpuRenderPassEncoderSetStencilReference(pass, mark->parent);
        wgpuRenderPassEncoderDraw(pass, count, 1, first, 0);

        wgpuRenderPassEncoderSetPipeline(pass, f->steps[1]);
        wgpuRenderPassEncoderSetStencilReference(pass, BOX_RENDER_STENCIL_MARK | mark->child);
        wgpuRenderPassEncoderDraw(pass, count, 1, first, 0);

        wgpuRenderPassEncoderSetPipeline(pass, f->steps[2]);
        wgpuRenderPassEncoderSetStencilReference(pass, BOX_RENDER_STENCIL_MARK);
        wgpuRenderPassEncoderDraw(pass, count, 1, first, 0);
    }

    wgpuRenderPassEncoderSetStencilReference(pass, 0);
    result = true;
done:
    return result;
}

#endif
