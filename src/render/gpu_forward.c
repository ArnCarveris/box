/**
 * @file render/gpu_forward.c
 * @brief Forward and depth prepass pipelines of the webgpu backend.
 *
 * Creates the pipelines of the forward pass and the depth prepass for the state of a batch and
 * caches them by key, keeping the pipeline of a replaced module until its new one exists.
 */

#include "gpu_forward.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef BOX_RENDER_WGPU

/** Get the blend state of a blend mode.
 *
 * The fragment shader shapes its output for the equation: multiply and min mix toward
 * white by the alpha, screen and max scale by it.
 *
 * @param blend The BlendMode
 * @param out Receives the blend state
 */
static void box_gpu_blend_state(
    uint32_t blend,
    WGPUBlendState *out)
{
    WGPUBlendComponent color = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_SrcAlpha,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    WGPUBlendComponent alpha = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
    };
    switch (blend) {
    case PremultipliedAlpha:
        color.srcFactor = WGPUBlendFactor_One;
        break;
    case Additive:
        color.dstFactor = WGPUBlendFactor_One;
        alpha.dstFactor = WGPUBlendFactor_One;
        alpha.srcFactor = WGPUBlendFactor_Zero;
        break;
    case Multiply:
        color.srcFactor = WGPUBlendFactor_Dst;
        color.dstFactor = WGPUBlendFactor_Zero;
        break;
    case Screen:
        color.srcFactor = WGPUBlendFactor_One;
        color.dstFactor = WGPUBlendFactor_OneMinusSrc;
        break;
    case Subtractive:
        color.operation = WGPUBlendOperation_ReverseSubtract;
        color.dstFactor = WGPUBlendFactor_One;
        alpha.srcFactor = WGPUBlendFactor_Zero;
        alpha.dstFactor = WGPUBlendFactor_One;
        break;
    case Min:
    case Max:
        color.operation = blend == Min ? WGPUBlendOperation_Min : WGPUBlendOperation_Max;
        color.srcFactor = WGPUBlendFactor_One;
        color.dstFactor = WGPUBlendFactor_One;
        break;
    default:
        break;
    }

    out->color = color;
    out->alpha = alpha;
}

/** Set the overrides of the scene features of a mesh pipeline.
 *
 * @param features BOX_RENDER_FEATURE_OVERRIDES features of the scene the pipeline draws
 * @param out Receives BOX_GPU_FEATURE_OVERRIDES constants, the first 6 those of the mesh shader
 */
static void box_gpu_feature_constants(
    uint32_t features,
    WGPUConstantEntry *out)
{
    static const char *keys[BOX_GPU_FEATURE_OVERRIDES] = {
        "scene_baked", "scene_reflections", "scene_punctual_shadows", "scene_cookies",
        "scene_environment", "scene_aerial", "material_sheen", "material_clear_coat",
        "material_anisotropy", "material_diffuse_transmission", "material_triplanar",
        "material_transmission"
    };
    int32_t i;
    for (i = 0; i < BOX_GPU_FEATURE_OVERRIDES; i ++) {
        out[i] = (WGPUConstantEntry){
            .key = { .data = keys[i], .length = WGPU_STRLEN },
            .value = features & (1u << i) ? 1.0 : 0.0
        };
    }
}

/** Create the forward pipeline of a shader, color format, blend mode, back face mode, alpha
 * test and depth mode.
 *
 * The 2D shape shader blends its edges in the opaque pass too and draws over equal depths, so
 * coplanar shapes draw in order; it lifts the depth of a fragment by the rank of its instance,
 * so the order holds where coplanar quads interpolate their depths differently.
 *
 * A mesh pipeline sets the scene features as constants, an extended one the material features
 * too; the blend mode, alpha test and transmission of its draws are in the DrawKind of their
 * material group (box_gpu_draw_kind). The 2D shape shader sets its blend_mode constant.
 *
 * @param gpu The backend
 * @param handle Handle of the shader, a mesh shader or the 2D shape shader
 * @param shader The shader module
 * @param format The format of the color target
 * @param blend The BlendMode, Opaque writes depth and does not blend
 * @param back_face The BackFaceMode, BackFaceCull culls the back faces
 * @param transmissive Whether the fragments sample the opaque snapshot (the transmissive
 * constant of mesh.wgsl): no blending, no depth writes, compare Greater; ignored by the 2D
 * shape shader
 * @param equal Whether the pipeline draws after a depth prepass: compare Equal, no depth
 * writes, no alpha test; ignored by the 2D shape shader
 * @param box_shadow Whether the fragments are shadows of 2D shapes, shaded by the fs_shadow
 * entry point of the 2D shape shader; ignored by the mesh shader
 * @param slice Whether the fragments are of Rects with an ImageSlice, shaded by the fs_slice
 * entry point of the 2D shape shader; ignored by the mesh shader and for shadows
 * @param texture_blend Whether the fragments blend the layers of a TextureBlend: the fs_blend
 * entry point of the mesh shader with the blend group (blend_mesh_layout), alpha tested,
 * blended or transmissive as the other arguments say; ignored by the 2D shape shader
 * @param polygon Whether the instances are polygons (Polygon2D): the polygon variant of the
 * vertex stage of the 2D shape shader (its polygon constant) with the fs_polygon entry point,
 * or for shadows the plain vertex stage with fs_shadow_polygon; ignored by the mesh shader and
 * for sliced shapes
 * @param features BOX_RENDER_FEATURE_OVERRIDES features of the scene it draws, the overrides
 * of the mesh shaders; ignored by the 2D shape shader
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_forward_create(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader,
    WGPUTextureFormat format,
    uint32_t blend,
    uint32_t back_face,
    bool transmissive,
    bool equal,
    bool box_shadow,
    bool slice,
    bool texture_blend,
    bool polygon,
    uint32_t features)
{
    bool shape2d = handle == BOX_RENDER_SHADER_SHAPE2D;
    bool extended = handle == BOX_RENDER_SHADER_MESH_EXTENDED;
    box_shadow = box_shadow && shape2d;
    slice = slice && shape2d && !box_shadow;
    polygon = polygon && shape2d && !slice;
    texture_blend = texture_blend && !shape2d;
    const char *entry = box_shadow ? (polygon ? "fs_shadow_polygon" : "fs_shadow") :
        polygon ? "fs_polygon" : slice ? "fs_slice" : texture_blend ? "fs_blend" : "fs";
    transmissive = transmissive && !shape2d;
    equal = equal && !transmissive;
    WGPUCompareFunction compare = WGPUCompareFunction_Greater;
    equal = equal && !shape2d;
    if (shape2d) {
        compare = WGPUCompareFunction_GreaterEqual;
    } else if (equal) {
        compare = WGPUCompareFunction_Equal;
        entry = texture_blend ? "fs_blend_opaque" : "fs_opaque";
    }

    if (handle == BOX_RENDER_SHADER_MESH) {
        entry = "fs";
    }

    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUBlendState blend_state;
    WGPUVertexBufferLayout buffers[2] = {
        {
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(box_vertex_t),
            .attributeCount = 3,
            .attributes = vertex_attrs
        },
        {
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = sizeof(uint32_t),
            .attributeCount = 1,
            .attributes = &slot_attr
        }
    };
    box_gpu_blend_state(shape2d && blend == Opaque ? AlphaBlend : blend, &blend_state);
    WGPUColorTargetState target = {
        .format = format,
        .blend = (blend == Opaque && !shape2d) || transmissive ? NULL : &blend_state,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUConstantEntry constant = {
        .key = { .data = "blend_mode", .length = WGPU_STRLEN },
        .value = (double)blend
    };
    WGPUConstantEntry vertex_constant = {
        .key = { .data = "polygon", .length = WGPU_STRLEN },
        .value = polygon && !box_shadow ? 1.0 : 0.0
    };
    bool mesh = box_render_is_mesh(handle);
    WGPUConstantEntry constants[BOX_GPU_FEATURE_OVERRIDES];
    box_gpu_feature_constants(features, constants);
    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = entry, .length = WGPU_STRLEN },
        .constantCount = shape2d ? 1 : extended ? BOX_GPU_FEATURE_OVERRIDES : mesh ? 6 : 0,
        .constants = shape2d ? &constant : mesh ? constants : NULL,
        .targetCount = 1,
        .targets = &target
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = blend == Opaque && !equal && !transmissive
            ? WGPUOptionalBool_True : WGPUOptionalBool_False,
        .depthCompare = compare,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = texture_blend ? gpu->blend_mesh_layout : gpu->mesh_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN },
            .constantCount = shape2d ? 1 : 0,
            .constants = shape2d ? &vertex_constant : NULL,
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = back_face == BackFaceCull ? WGPUCullMode_Back : WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return box_gpu_render_pipeline(gpu, &desc);
}

/** Create a depth prepass pipeline of a mesh shader and a back face mode.
 *
 * It has no color target, writes the depth with compare Greater as an unlit view does, and has
 * the vs and layout of the forward pass, so a compiler compiles that vs once for both passes.
 *
 * Its fragment stage takes the inputs of fs: fs_depth writes nothing, fs_depth_cutout discards as
 * fs does and fs_depth_cutout_blend as fs_blend does. With a count format the fs_count entry
 * points discard the same and add one to red (plain) or green (alpha tested) per fragment.
 *
 * @param gpu The backend
 * @param shader The module of the mesh shader of the batch, the extended one for an alpha test
 * or a TextureBlend
 * @param back_face The BackFaceMode, BackFaceCull culls the back faces
 * @param cutout Whether the fragments are alpha tested
 * @param texture_blend Whether the batch binds the blend group of a TextureBlend
 * (blend_mesh_layout)
 * @param count Format of the overdraw count target, Undefined for a depth only prepass
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_prepass_create(
    box_gpu_t *gpu,
    WGPUShaderModule shader,
    uint32_t back_face,
    bool cutout,
    bool texture_blend,
    WGPUTextureFormat count)
{
    bool counts = count != WGPUTextureFormat_Undefined;
    const char *entry = !cutout ? (counts ? "fs_count" : "fs_depth")
        : texture_blend ? (counts ? "fs_count_cutout_blend" : "fs_depth_cutout_blend")
        : (counts ? "fs_count_cutout" : "fs_depth_cutout");
    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUVertexBufferLayout buffers[2] = {
        {
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(box_vertex_t),
            .attributeCount = 3,
            .attributes = vertex_attrs
        },
        {
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = sizeof(uint32_t),
            .attributeCount = 1,
            .attributes = &slot_attr
        }
    };
    WGPUBlendState add = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_One
        },
        .alpha = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_One
        }
    };
    WGPUColorTargetState target = {
        .format = count,
        .blend = &add,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = entry, .length = WGPU_STRLEN },
        .targetCount = counts ? 1 : 0,
        .targets = counts ? &target : NULL
    };
    WGPUDepthStencilState depth = {
        .format = BOX_GPU_DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Greater,
        .stencilFront = { .compare = WGPUCompareFunction_Always },
        .stencilBack = { .compare = WGPUCompareFunction_Always }
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = texture_blend ? gpu->blend_mesh_layout : gpu->mesh_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = "vs", .length = WGPU_STRLEN },
            .bufferCount = 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = back_face == BackFaceCull ? WGPUCullMode_Back : WGPUCullMode_None
        },
        .depthStencil = &depth,
        .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        .fragment = &fragment
    };

    return box_gpu_render_pipeline(gpu, &desc);
}

/** Depth state of a pipeline of the forward cache */
typedef enum box_gpu_depth_mode_t {
    BOX_GPU_DEPTH_GREATER,          /**< Forward, compare Greater, opaque draws write depth */
    BOX_GPU_DEPTH_EQUAL,            /**< Forward after a depth prepass, compare Equal */
    BOX_GPU_DEPTH_PREPASS,          /**< Depth prepass, no color target */
    BOX_GPU_DEPTH_COUNT             /**< Depth prepass counting the overdraw of a view */
} box_gpu_depth_mode_t;

/** Pack the state a forward or depth prepass pipeline is created for into its cache key.
 *
 * @param shader Shader handle, below 2^30
 * @param format Format of the color target, below 2^24, Undefined for a depth prepass, the count
 * format for an overdraw count
 * @param blend BlendMode, below 16
 * @param back_face BackFaceMode, of which the pipeline only tells BackFaceCull from the rest
 * @param cutout For the 2D shape shader, whether the fragments are shadows (box_shadow); false
 * for the mesh shader, whose alpha test is in the DrawKind of the draw
 * @param transmissive Whether the fragments sample the opaque snapshot (Transmission), for the
 * 2D shape shader, which is never transmissive, whether they are sliced (slice)
 * @param texture_blend Whether the fragments blend the layers of a TextureBlend (fs_blend), for
 * the 2D shape shader, which never blends layers, whether they are polygons (polygon)
 * @param depth Depth state of the pipeline
 * @param features BOX_RENDER_FEATURE_OVERRIDES features of the mesh shader overrides, 0 for the
 * 2D shape shader and the depth prepass
 * @return The key, distinct for every distinct pipeline
 */
static uint64_t box_gpu_forward_key(
    uint32_t shader,
    WGPUTextureFormat format,
    uint32_t blend,
    uint32_t back_face,
    bool cutout,
    bool transmissive,
    bool texture_blend,
    box_gpu_depth_mode_t depth,
    uint32_t features)
{
    ecs_assert((uint32_t)format < (1u << 24), ECS_INTERNAL_ERROR, NULL);
    ecs_assert(shader < (1u << 18), ECS_INTERNAL_ERROR, NULL);
    ecs_assert(features <= BOX_RENDER_FEATURE_OVERRIDES, ECS_INTERNAL_ERROR, NULL);

    return ((uint64_t)shader << 46) | ((uint64_t)features << 34) |
        ((uint64_t)texture_blend << 33) |
        ((uint64_t)transmissive << 32) |
        ((uint64_t)format << 8) | ((uint64_t)blend << 4) | ((uint64_t)depth << 2) |
        (cutout ? 2u : 0u) | (back_face == BackFaceCull ? 0u : 1u);
}

/** Check whether a batch draws in the depth prepass of a lit view: an opaque mesh of a mesh
 * shader, plain or alpha tested. Its forward pipeline in such a view tests with Equal.
 *
 * @param b The batch
 * @return Whether the prepass draws it
 */
bool box_gpu_prepass_draws(
    const box_render_batch_t *b)
{
    return b->view == -1 && box_render_is_mesh(b->pipeline) && b->blend == Opaque;
}

/** Keep a requested forward pipeline in the cache. Until it exists, a draw of its key uses the
 * pipeline of the module a recompile replaced, which it then releases.
 *
 * @param gpu The backend
 * @param key Key of the pipeline in the cache
 * @param pipeline The pipeline the request returned, NULL while it is being created
 * @return The pipeline, else the previous one of its key, NULL without one
 */
static WGPURenderPipeline box_gpu_forward_cache(
    box_gpu_t *gpu,
    uint64_t key,
    WGPURenderPipeline pipeline)
{
    ecs_map_val_t *previous = ecs_map_get(&gpu->forward_previous, key);
    WGPURenderPipeline result = pipeline;
    if (pipeline) {
        *ecs_map_ensure(&gpu->forward, key) = (ecs_map_val_t)(uintptr_t)pipeline;
    } else if (previous) {
        result = (WGPURenderPipeline)(uintptr_t)*previous;
    }

    if (pipeline && previous) {
        wgpuRenderPipelineRelease((WGPURenderPipeline)(uintptr_t)*previous);
        ecs_map_remove(&gpu->forward_previous, key);
    }

    return result;
}

uint32_t box_gpu_draw_kind(
    const box_render_batch_t *b,
    bool equal)
{
    uint32_t result = 0;
    bool cutout = b->cutout && !b->transmissive && !(equal && box_gpu_prepass_draws(b));
    if (b->pipeline == BOX_RENDER_SHADER_SHAPE2D) {
        goto done;
    }

    result = (b->blend & 15u) | (b->transmissive ? 16u : 0u) | (cutout ? 32u : 0u);
done:
    return result * BOX_GPU_DRAW_KIND_STRIDE;
}

WGPURenderPipeline box_gpu_forward_pipeline(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b,
    bool equal)
{
    WGPURenderPipeline result = NULL;
    uint32_t features = b->features & (b->pipeline == BOX_RENDER_SHADER_MESH_EXTENDED
        ? BOX_RENDER_FEATURE_OVERRIDES : BOX_RENDER_FEATURE_LIGHTING);
    WGPUShaderModule shader;
    ecs_map_val_t *cached;
    uint64_t key;
    bool texture_blend;
    if (b->blend > Max) {
        goto done;
    }

    shader = box_gpu_shader_get(gpu, b->pipeline);
    if (!shader) {
        goto done;
    }

    equal = equal && box_gpu_prepass_draws(b);
    texture_blend = box_gpu_texture_blends(b);
    if (!box_render_is_mesh(b->pipeline)) {
        features = 0;
    }

    key = box_gpu_forward_key(b->pipeline, format, b->blend, b->back_face,
        b->pipeline == BOX_RENDER_SHADER_SHAPE2D && b->box_shadow,
        b->pipeline == BOX_RENDER_SHADER_SHAPE2D ? b->slice : b->transmissive,
        b->pipeline == BOX_RENDER_SHADER_SHAPE2D ? b->polygon : texture_blend,
        equal ? BOX_GPU_DEPTH_EQUAL : BOX_GPU_DEPTH_GREATER, features);
    cached = ecs_map_get(&gpu->forward, key);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    result = box_gpu_forward_cache(gpu, key, box_gpu_forward_create(gpu, b->pipeline, shader,
        format, b->blend, b->back_face, b->transmissive, equal, b->box_shadow, b->slice,
        texture_blend, b->polygon, features));
done:
    return result;
}

/** Get the depth prepass pipeline of a batch the prepass draws, created on first use in the
 * forward cache from the mesh shader of the batch, so a recompiled or released mesh shader
 * empties it too.
 *
 * @param gpu The backend
 * @param b The batch, box_gpu_prepass_draws
 * @param count Format of the overdraw count target, Undefined for a depth only prepass
 * @return The pipeline, NULL without a mesh shader
 */
WGPURenderPipeline box_gpu_prepass_pipeline(
    box_gpu_t *gpu,
    const box_render_batch_t *b,
    WGPUTextureFormat count)
{
    WGPURenderPipeline result = NULL;
    WGPUShaderModule shader = box_gpu_shader_get(gpu, b->pipeline);
    ecs_map_val_t *cached;
    uint64_t key;
    bool texture_blend;
    if (!shader) {
        goto done;
    }

    texture_blend = box_gpu_texture_blends(b);
    key = box_gpu_forward_key(b->pipeline, count, Opaque, b->back_face, b->cutout,
        false, texture_blend,
        count != WGPUTextureFormat_Undefined ? BOX_GPU_DEPTH_COUNT : BOX_GPU_DEPTH_PREPASS, 0);
    cached = ecs_map_get(&gpu->forward, key);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    result = box_gpu_forward_cache(gpu, key,
        box_gpu_prepass_create(gpu, shader, b->back_face, b->cutout, texture_blend, count));
done:
    return result;
}

/** Request the pipelines of the extended mesh shader a view of a scene with material features
 * draws with, so they compile before the first shape needs them; other blend modes than
 * AlphaBlend compile on first use.
 *
 * Those are the opaque, alpha tested and TextureBlend ones of both back face modes with the
 * depth states of the view, the alpha blended and the transmissive ones, each when enabled.
 *
 * @param gpu The backend
 * @param format The format of the color target of the view
 * @param v The view
 * @param prepass Whether the view draws a depth prepass, so the opaque draws test Equal
 */
void box_gpu_draw_prewarm(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_view_t *v,
    bool prepass)
{
    uint32_t f = v->scene_features;
    int32_t face, kind;
    if (!(f & BOX_RENDER_FEATURE_MATERIALS) ||
        !box_gpu_shader_get(gpu, BOX_RENDER_SHADER_MESH_EXTENDED))
    {
        goto done;
    }

    for (face = 0; face < 2; face ++) {
        for (kind = 0; kind < 6; kind ++) {
            box_render_batch_t b = {
                .view = -1,
                .scene = v->scene,
                .pipeline = BOX_RENDER_SHADER_MESH_EXTENDED,
                .features = f,
                .blend = kind == 4 || kind == 5 ? AlphaBlend : Opaque,
                .back_face = face ? BackFacePlain : BackFaceCull,
                .cutout = kind == 1 || kind == 3,
                .texture_blend = kind == 2 || kind == 3,
                .transmissive = kind == 5
            };
            if ((b.cutout && !(f & BOX_RENDER_FEATURE_ALPHA_CUTOUT)) ||
                (b.texture_blend && !(f & BOX_RENDER_FEATURE_TEXTURE_BLEND)) ||
                (kind == 4 && !(f & BOX_RENDER_FEATURE_BLEND_MODES)) ||
                (kind == 5 && !(f & BOX_RENDER_FEATURE_TRANSMISSION)))
            {
                continue;
            }

            box_gpu_forward_pipeline(gpu, format, &b, false);
            if (prepass && b.blend == Opaque) {
                box_gpu_forward_pipeline(gpu, format, &b, true);
                box_gpu_prepass_pipeline(gpu, &b, WGPUTextureFormat_Undefined);
            }
        }
    }

done:
    return;
}

#endif
