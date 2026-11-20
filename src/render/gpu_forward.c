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
 * @param overrides The overrides that are true (box_render_mesh_overrides)
 * @param out Receives BOX_RENDER_MESH_OVERRIDES constants, the first
 * BOX_RENDER_MESH_OVERRIDES_PLAIN those of the mesh shader
 */
static void box_gpu_feature_constants(
    uint32_t overrides,
    WGPUConstantEntry *out)
{
    int32_t i;
    for (i = 0; i < BOX_RENDER_MESH_OVERRIDES; i ++) {
        out[i] = (WGPUConstantEntry){
            .key = { .data = box_render_mesh_override_key(i), .length = WGPU_STRLEN },
            .value = overrides & (1u << i) ? 1.0 : 0.0
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
 * Mesh pipelines set the overrides of box_render_mesh_overrides, their vertex stage those it
 * reads; blend mode and alpha test are in the DrawKind of the material group (box_gpu_draw_kind).
 * The 2D shape shader sets blend_mode.
 *
 * @param gpu The backend
 * @param handle Handle of the shader, a mesh shader or the 2D shape shader
 * @param shader The shader module
 * @param format The format of the color target
 * @param blend The BlendMode, Opaque writes depth and does not blend
 * @param back_face The BackFaceMode, BackFaceCull culls the back faces
 * @param cutout Whether the fragment stage alpha tests (the cutout entry points of the mesh
 * shaders); ignored by the 2D shape shader and after a depth prepass
 * @param transmissive Whether the fragments sample the opaque snapshot (the transmissive
 * constant of mesh_extended.wgsl): no blending, no depth writes, compare Greater; ignored by the
 * 2D shape shader
 * @param water Whether the fragments are of Water shapes, transmissive too: the water constant
 * of the extended mesh shader, without a TextureBlend; ignored by the other shaders
 * @param distortion Whether the transmissive fragments of the extended mesh shader offset the
 * snapshot by the noise of their Distortion (the material_distortion constant, Scene.distortion);
 * ignored by the other pipelines
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
 * @param surface Whether the instances are parametric surfaces, placed by the vs_surface entry
 * point of the mesh shaders; ignored by the 2D shape shader
 * @param features BOX_RENDER_OVERRIDE_SCENE overrides of the mesh shaders from the features of
 * the scene it draws (box_render_mesh_overrides); ignored by the 2D shape shader
 * @param mirrored Whether the view is mirrored (a planar reflection), which turns the winding of
 * the front faces to clockwise
 * @param wind Whether the vertex stage moves the vertices by the WindSway of their material
 * (the material_wind constant of the mesh shaders); ignored by the 2D shape shader
 * @param layers Whether the fragments apply TextureLayers: the _layers entry points of the
 * layers module of the mesh shader (shader, box_render_mesh_layers) with the layers layouts
 * (box_gpu_layer_pipeline_layout), fs_water_layers for water; ignored by the 2D shape shader
 * and for the other transmissive pipelines
 * @param data Whether the mesh has per vertex data: the vs_data and vs_surface_data entry points
 * read it from a third vertex buffer (box_gpu_vertex_data); ignored by the 2D shape shader
 * @param range Whether the instances have a DrawRange (the draw_range constant of both stages
 * of the mesh shaders), which dithers their fades; ignored by the 2D shape shader
 * @param impostor Whether the instances are impostor cards: vs_impostor and fs_impostor of the
 * mesh shader with the per vertex data, no face culling and compare GreaterEqual, as their
 * depth comes from the depth atlas in the prepass and in this pass
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_forward_create(
    box_gpu_t *gpu,
    uint32_t handle,
    WGPUShaderModule shader,
    WGPUTextureFormat format,
    uint32_t blend,
    uint32_t back_face,
    bool cutout,
    bool transmissive,
    bool water,
    bool distortion,
    bool equal,
    bool box_shadow,
    bool slice,
    bool texture_blend,
    bool polygon,
    bool surface,
    uint32_t features,
    bool mirrored,
    bool wind,
    bool layers,
    bool data,
    bool range,
    bool impostor)
{
    bool shape2d = handle == BOX_RENDER_SHADER_SHAPE2D;
    bool extended = handle == BOX_RENDER_SHADER_MESH_EXTENDED;
    water = water && extended && transmissive;
    distortion = distortion && extended && transmissive;
    texture_blend = texture_blend && !water;
    box_shadow = box_shadow && shape2d;
    slice = slice && shape2d && !box_shadow;
    polygon = polygon && shape2d && !slice;
    texture_blend = texture_blend && !shape2d;
    transmissive = transmissive && !shape2d;
    equal = equal && !transmissive && !shape2d;
    cutout = cutout && !shape2d && !transmissive && !equal;
    surface = surface && !shape2d;
    data = data && !shape2d;
    const char *entry = box_shadow ? (polygon ? "fs_shadow_polygon" : "fs_shadow") :
        polygon ? "fs_polygon" : slice ? "fs_slice" : cutout ? "fs_cutout" : "fs";
    if (texture_blend) {
        entry = cutout ? "fs_blend_cutout" : "fs_blend";
    }

    layers = layers && !shape2d && (water || !transmissive);
    if (layers) {
        entry = texture_blend ? (cutout ? "fs_blend_cutout_layers" : "fs_blend_layers")
            : (cutout ? "fs_cutout_layers" : "fs_layers");
        entry = water ? "fs_water_layers" : entry;
    }

    impostor = impostor && handle == BOX_RENDER_SHADER_MESH && !layers;
    if (impostor) {
        entry = "fs_impostor";
        data = true;
        equal = false;
        back_face = BackFacePlain;
    }

    WGPUCompareFunction compare = WGPUCompareFunction_Greater;
    if (shape2d || impostor) {
        compare = WGPUCompareFunction_GreaterEqual;
    } else if (equal) {
        compare = WGPUCompareFunction_Equal;
    }

    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUVertexAttribute data_attrs[2];
    WGPUBlendState blend_state;
    WGPUVertexBufferLayout buffers[3] = {
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
    buffers[2] = box_gpu_vertex_data_layout(data_attrs);
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
    WGPUConstantEntry mesh_vertex_constants[6] = {
        {
            .key = { .data = "material_texture_scroll", .length = WGPU_STRLEN },
            .value = features & BOX_RENDER_OVERRIDE_TEXTURE_SCROLL ? 1.0 : 0.0
        },
        {
            .key = { .data = "scene_planet_light", .length = WGPU_STRLEN },
            .value = features & BOX_RENDER_OVERRIDE_PLANET_LIGHT ? 1.0 : 0.0
        },
        {
            .key = { .data = "water", .length = WGPU_STRLEN },
            .value = water ? 1.0 : 0.0
        },
        {
            .key = { .data = "material_fresnel", .length = WGPU_STRLEN },
            .value = features & BOX_RENDER_OVERRIDE_FRESNEL ? 1.0 : 0.0
        },
        {
            .key = { .data = "scene_planar", .length = WGPU_STRLEN },
            .value = features & BOX_RENDER_OVERRIDE_PLANAR ? 1.0 : 0.0
        },
        {
            .key = { .data = "material_distortion", .length = WGPU_STRLEN },
            .value = distortion ? 1.0 : 0.0
        }
    };
    bool mesh = box_render_is_mesh(handle);
    int32_t vertex_count = extended ? 6 : 2;
    WGPUConstantEntry vertex_constants[8];
    WGPUConstantEntry range_constant = {
        .key = { .data = "draw_range", .length = WGPU_STRLEN },
        .value = 1.0
    };
    ecs_os_memcpy_n(vertex_constants, mesh_vertex_constants, WGPUConstantEntry, vertex_count);
    if (mesh && wind) {
        vertex_constants[vertex_count ++] = (WGPUConstantEntry){
            .key = { .data = "material_wind", .length = WGPU_STRLEN },
            .value = 1.0
        };
    }

    range = range && mesh;
    if (range) {
        vertex_constants[vertex_count ++] = range_constant;
    }

    WGPUConstantEntry constants[BOX_RENDER_MESH_OVERRIDES + 1];
    box_gpu_feature_constants((features & BOX_RENDER_OVERRIDE_SCENE) |
        (transmissive && extended ? BOX_RENDER_OVERRIDE_TRANSMISSIVE : 0u) |
        (water ? BOX_RENDER_OVERRIDE_WATER : 0u) |
        (distortion ? BOX_RENDER_OVERRIDE_DISTORTION : 0u), constants);
    size_t fragment_count = shape2d ? 1 : extended ? BOX_RENDER_MESH_OVERRIDES :
        mesh ? BOX_RENDER_MESH_OVERRIDES_PLAIN : 0;
    if (range) {
        constants[fragment_count ++] = range_constant;
    }

    WGPUFragmentState fragment = {
        .module = shader,
        .entryPoint = { .data = entry, .length = WGPU_STRLEN },
        .constantCount = fragment_count,
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
        .layout = layers ? box_gpu_layer_pipeline_layout(gpu, texture_blend)
            : texture_blend ? gpu->blend_mesh_layout : gpu->mesh_layout,
        .vertex = {
            .module = shader,
            .entryPoint = {
                .data = impostor ? "vs_impostor" : surface
                    ? (data ? "vs_surface_data" : "vs_surface")
                    : (data ? "vs_data" : "vs"),
                .length = WGPU_STRLEN
            },
            .constantCount = shape2d ? 1 : mesh ? (size_t)vertex_count : 0,
            .constants = shape2d ? &vertex_constant : mesh ? vertex_constants : NULL,
            .bufferCount = data ? 3 : 2,
            .buffers = buffers
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = mirrored ? WGPUFrontFace_CW : WGPUFrontFace_CCW,
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
 * the layout of the forward pass. Its vertex stage places the vertices as vs does: vs_depth
 * passes on the position only, vs_cutout what the alpha test reads.
 *
 * fs_depth writes nothing, fs_depth_cutout discards as fs_cutout does and fs_depth_cutout_blend
 * as fs_blend_cutout does. With a count format the fs_count entry points discard the same and
 * add one to red (plain) or green (alpha tested) per fragment.
 *
 * @param gpu The backend
 * @param shader The module of the mesh shader of the batch
 * @param back_face The BackFaceMode, BackFaceCull culls the back faces
 * @param cutout Whether the fragments are alpha tested
 * @param texture_blend Whether the batch binds the blend group of a TextureBlend
 * (blend_mesh_layout)
 * @param surface Whether the instances are parametric surfaces, placed by the surface variants
 * of the vertex stages
 * @param count Format of the overdraw count target, Undefined for a depth only prepass
 * @param wind Whether the vertex stage moves the vertices by the WindSway of their material, as
 * the forward pipeline of the batch does (material_wind)
 * @param layers Whether the batch binds the layers group of TextureLayers (the layers layouts);
 * the prepass does not apply the layers, which do not change the alpha
 * @param data Whether the vertex stage reads the per vertex data of the mesh for its
 * VertexReveal (vs_cutout_data, vs_cutout_surface_data) or the pivots of its WindSway
 * (vs_depth_data, vs_depth_surface_data); ignored without cutout or wind
 * @param range Whether the instances have a DrawRange: both stages have the draw_range override
 * and a plain batch discards with fs_depth_range (fs_count_range)
 * @param impostor Whether the instances are impostor cards, drawn with vs_impostor and
 * fs_impostor_depth (fs_impostor_count), which write the depth of the depth atlas
 * @return The pipeline
 */
static WGPURenderPipeline box_gpu_prepass_create(
    box_gpu_t *gpu,
    WGPUShaderModule shader,
    uint32_t back_face,
    bool cutout,
    bool texture_blend,
    bool surface,
    WGPUTextureFormat count,
    bool wind,
    bool layers,
    bool data,
    bool range,
    bool impostor)
{
    WGPUConstantEntry vertex_constants[2];
    WGPUConstantEntry range_constant = {
        .key = { .data = "draw_range", .length = WGPU_STRLEN },
        .value = 1.0
    };
    size_t vertex_count = 0;
    if (wind) {
        vertex_constants[vertex_count ++] = (WGPUConstantEntry){
            .key = { .data = "material_wind", .length = WGPU_STRLEN },
            .value = 1.0
        };
    }

    if (range) {
        vertex_constants[vertex_count ++] = range_constant;
    }

    bool counts = count != WGPUTextureFormat_Undefined;
    const char *entry = !cutout && range ? (counts ? "fs_count_range" : "fs_depth_range")
        : !cutout ? (counts ? "fs_count" : "fs_depth")
        : texture_blend ? (counts ? "fs_count_cutout_blend" : "fs_depth_cutout_blend")
        : (counts ? "fs_count_cutout" : "fs_depth_cutout");
    const char *vertex = cutout ? (surface ? "vs_cutout_surface" : "vs_cutout")
        : (surface ? "vs_depth_surface" : "vs_depth");
    data = data && (cutout || wind);
    if (data) {
        vertex = cutout ? (surface ? "vs_cutout_surface_data" : "vs_cutout_data")
            : (surface ? "vs_depth_surface_data" : "vs_depth_data");
    }

    if (impostor) {
        vertex = "vs_impostor";
        entry = counts ? "fs_impostor_count" : "fs_impostor_depth";
        data = true;
        back_face = BackFacePlain;
    }

    WGPUVertexAttribute vertex_attrs[3] = {
        { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x3, .offset = 12, .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 24, .shaderLocation = 2 }
    };
    WGPUVertexAttribute slot_attr = {
        .format = WGPUVertexFormat_Uint32, .offset = 0, .shaderLocation = 3
    };
    WGPUVertexAttribute data_attrs[2];
    WGPUVertexBufferLayout buffers[3] = {
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
    buffers[2] = box_gpu_vertex_data_layout(data_attrs);
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
        .constantCount = range ? 1 : 0,
        .constants = range ? &range_constant : NULL,
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
        .layout = layers ? box_gpu_layer_pipeline_layout(gpu, texture_blend)
            : texture_blend ? gpu->blend_mesh_layout : gpu->mesh_layout,
        .vertex = {
            .module = shader,
            .entryPoint = { .data = vertex, .length = WGPU_STRLEN },
            .constantCount = vertex_count,
            .constants = vertex_count ? vertex_constants : NULL,
            .bufferCount = data ? 3 : 2,
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
 * @param shader Shader handle, below 2^11
 * @param format Format of the color target, below 2^20, Undefined for a depth prepass, the count
 * format for an overdraw count
 * @param blend BlendMode, below 16
 * @param back_face BackFaceMode, of which the pipeline only tells BackFaceCull from the rest
 * @param cutout For the 2D shape shader, whether the fragments are shadows (box_shadow); for the
 * mesh shaders, whether the fragment stage alpha tests
 * @param transmissive Whether the fragments sample the opaque snapshot (Transmission), for the
 * 2D shape shader, which is never transmissive, whether they are sliced (slice)
 * @param texture_blend Whether the fragments blend the layers of a TextureBlend (fs_blend), for
 * the 2D shape shader, which never blends layers, whether they are polygons (polygon)
 * @param surface Whether the instances are parametric surfaces (vs_surface)
 * @param depth Depth state of the pipeline
 * @param features BOX_RENDER_OVERRIDE_SCENE overrides of the mesh shaders
 * (box_render_mesh_overrides), 0 for the 2D shape shader and the depth prepass
 * @param mirrored Whether the front faces wind clockwise, for a planar reflection
 * @param water Whether the pipeline draws Water shapes (the water override)
 * @param distortion Whether the pipeline offsets the snapshot by a Distortion (the
 * material_distortion override)
 * @param wind Whether the vertex stage moves the vertices by their WindSway (the material_wind
 * override)
 * @param layers Whether the pipeline applies TextureLayers or, for a depth prepass, has their
 * layouts
 * @param data Whether the vertex stage reads the per vertex data stream (box_gpu_vertex_data)
 * @param range Whether the pipeline dithers the DrawRange of the instances (the draw_range
 * override)
 * @param impostor Whether the pipeline draws impostor cards (the _impostor entry points), which
 * are opaque and take the blend value 15 that no BlendMode has
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
    bool surface,
    box_gpu_depth_mode_t depth,
    uint32_t features,
    bool mirrored,
    bool water,
    bool distortion,
    bool wind,
    bool layers,
    bool data,
    bool range,
    bool impostor)
{
    ecs_assert((uint32_t)format < (1u << 20), ECS_INTERNAL_ERROR, NULL);
    ecs_assert(shader < (1u << 11), ECS_INTERNAL_ERROR, NULL);
    blend = impostor ? 15u : blend;
    ecs_assert(features <= BOX_RENDER_OVERRIDE_SCENE, ECS_INTERNAL_ERROR, NULL);

    return ((uint64_t)shader << 53) | ((uint64_t)data << 52) | ((uint64_t)distortion << 51) |
        ((uint64_t)water << 50) |
        ((uint64_t)surface << 49) | ((uint64_t)mirrored << 48) | ((uint64_t)features << 33) |
        ((uint64_t)texture_blend << 32) |
        ((uint64_t)transmissive << 31) | ((uint64_t)wind << 30) | ((uint64_t)layers << 29) |
        ((uint64_t)range << 28) | ((uint64_t)format << 8) | ((uint64_t)blend << 4) |
        ((uint64_t)depth << 2) |
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
    return box_gpu_forward_pipeline_mirrored(gpu, format, b, equal, false);
}

WGPURenderPipeline box_gpu_forward_pipeline_mirrored(
    box_gpu_t *gpu,
    WGPUTextureFormat format,
    const box_render_batch_t *b,
    bool equal,
    bool mirrored)
{
    WGPURenderPipeline result = NULL;
    uint32_t features = box_render_mesh_overrides(b->pipeline, b->features, false, false);
    bool shape2d = b->pipeline == BOX_RENDER_SHADER_SHAPE2D;
    bool mesh = box_render_is_mesh(b->pipeline);
    WGPUShaderModule shader;
    ecs_map_val_t *cached;
    uint64_t key;
    bool texture_blend, cutout, surface, water, distortion, wind, layers, data, range, impostor;
    if (b->blend > Max) {
        goto done;
    }

    water = b->water && b->transmissive && b->pipeline == BOX_RENDER_SHADER_MESH_EXTENDED;
    layers = box_gpu_layered(b);
    shader = box_gpu_shader_get(gpu, layers ? box_render_mesh_layers(b->pipeline) : b->pipeline);
    if (!shader) {
        goto done;
    }

    equal = equal && box_gpu_prepass_draws(b);
    texture_blend = box_gpu_texture_blends(b);
    cutout = mesh && b->cutout && !b->transmissive && !equal;
    surface = mesh && b->surface;
    texture_blend = texture_blend && !water;
    distortion = b->transmissive && b->pipeline == BOX_RENDER_SHADER_MESH_EXTENDED &&
        (b->features & BOX_RENDER_FEATURE_DISTORTION);
    wind = mesh && b->wind;
    data = mesh && box_gpu_vertex_data(gpu, b);
    range = mesh && box_render_batch_ranged(b);
    impostor = b->impostor && b->pipeline == BOX_RENDER_SHADER_MESH && !layers;
    equal = equal && !impostor;

    key = box_gpu_forward_key(b->pipeline, format, b->blend, b->back_face,
        shape2d ? b->box_shadow : cutout,
        shape2d ? b->slice : b->transmissive,
        shape2d ? b->polygon : texture_blend, surface,
        equal ? BOX_GPU_DEPTH_EQUAL : BOX_GPU_DEPTH_GREATER, features, mirrored, water,
        distortion, wind, layers, data, range, impostor);
    cached = ecs_map_get(&gpu->forward, key);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    result = box_gpu_forward_cache(gpu, key, box_gpu_forward_create(gpu, b->pipeline, shader,
        format, b->blend, b->back_face, cutout, b->transmissive, water, distortion, equal,
        b->box_shadow,
        b->slice, texture_blend, b->polygon, surface, features, mirrored, wind, layers, data,
        range, impostor));
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
    bool texture_blend, layers, data, range, impostor;
    if (!shader) {
        goto done;
    }

    texture_blend = box_gpu_texture_blends(b);
    layers = box_gpu_layered(b);
    data = (b->cutout || b->wind) && box_gpu_vertex_data(gpu, b);
    range = box_render_batch_ranged(b);
    impostor = b->impostor && b->pipeline == BOX_RENDER_SHADER_MESH && !layers;
    key = box_gpu_forward_key(b->pipeline, count, Opaque, b->back_face, b->cutout,
        false, texture_blend, b->surface,
        count != WGPUTextureFormat_Undefined ? BOX_GPU_DEPTH_COUNT : BOX_GPU_DEPTH_PREPASS, 0,
        false, false, false, b->wind, layers, data, range, impostor);
    cached = ecs_map_get(&gpu->forward, key);
    if (cached && *cached) {
        result = (WGPURenderPipeline)(uintptr_t)*cached;
        goto done;
    }

    result = box_gpu_forward_cache(gpu, key,
        box_gpu_prepass_create(gpu, shader, b->back_face, b->cutout, texture_blend, b->surface,
            count, b->wind, layers, data, range, impostor));
done:
    return result;
}

/** Request the pipelines of the mesh shaders a view draws its alpha tested, blended and material
 * feature shapes with, so they compile before the first shape needs them; other blend modes
 * than AlphaBlend and the parametric surfaces compile on first use.
 *
 * Those are the alpha tested, TextureBlend and alpha blended ones of the mesh shader, and the
 * opaque, alpha tested, alpha blended and transmissive ones of the extended mesh shader, of both
 * back face modes with the depth states of the view, each when its scene enables it.
 *
 * Scene.water adds the culled water pipeline of the extended mesh shader (and its layers variant
 * with Scene.texture_layers), Scene.distortion the transmissive ones without Scene.transmission,
 * Scene.wind and Scene.texture_layers the opaque and alpha tested wind and layers mesh pipelines.
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
    const uint32_t shading = BOX_RENDER_FEATURE_MATERIALS &
        ~(BOX_RENDER_FEATURE_TEXTURE_BLEND | BOX_RENDER_FEATURE_TRANSMISSION |
            BOX_RENDER_FEATURE_DISTORTION);
    uint32_t f = v->scene_features;
    int32_t face, kind;
    for (face = 0; face < 2; face ++) {
        for (kind = 0; kind < 8; kind ++) {
            bool extended = kind >= 2 && kind != 4 && kind != 5;
            box_render_batch_t b = {
                .view = -1,
                .scene = v->scene,
                .pipeline = extended
                    ? BOX_RENDER_SHADER_MESH_EXTENDED : BOX_RENDER_SHADER_MESH,
                .features = f,
                .blend = kind == 1 || kind == 6 || kind == 7 ? AlphaBlend : Opaque,
                .back_face = face ? BackFacePlain : BackFaceCull,
                .cutout = kind == 0 || kind == 3 || kind == 5,
                .texture_blend = kind == 4 || kind == 5,
                .transmissive = kind == 7
            };
            if ((b.cutout && !(f & BOX_RENDER_FEATURE_ALPHA_CUTOUT)) ||
                (b.texture_blend && !(f & BOX_RENDER_FEATURE_TEXTURE_BLEND)) ||
                ((kind == 1 || kind == 6) && !(f & BOX_RENDER_FEATURE_BLEND_MODES)) ||
                (b.transmissive &&
                    !(f & (BOX_RENDER_FEATURE_TRANSMISSION | BOX_RENDER_FEATURE_DISTORTION))) ||
                (extended && !b.transmissive && !(f & shading)))
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

    for (face = 0; (f & BOX_RENDER_FEATURE_WIND) && face < 2; face ++) {
        for (kind = 0; kind < 2; kind ++) {
            box_render_batch_t b = {
                .view = -1,
                .scene = v->scene,
                .pipeline = BOX_RENDER_SHADER_MESH,
                .features = f,
                .blend = Opaque,
                .back_face = face ? BackFacePlain : BackFaceCull,
                .cutout = kind == 1,
                .wind = true
            };
            if (b.cutout && !(f & BOX_RENDER_FEATURE_ALPHA_CUTOUT)) {
                continue;
            }

            box_gpu_forward_pipeline(gpu, format, &b, false);
            if (prepass) {
                box_gpu_forward_pipeline(gpu, format, &b, true);
                box_gpu_prepass_pipeline(gpu, &b, WGPUTextureFormat_Undefined);
            }
        }
    }

    for (face = 0; (f & BOX_RENDER_FEATURE_TEXTURE_LAYERS) && face < 2; face ++) {
        for (kind = 0; kind < 2; kind ++) {
            box_render_batch_t b = {
                .view = -1,
                .scene = v->scene,
                .pipeline = BOX_RENDER_SHADER_MESH,
                .features = f,
                .blend = Opaque,
                .back_face = face ? BackFacePlain : BackFaceCull,
                .cutout = kind == 1,
                .texture_layers = true
            };
            if (b.cutout && !(f & BOX_RENDER_FEATURE_ALPHA_CUTOUT)) {
                continue;
            }

            box_gpu_forward_pipeline(gpu, format, &b, false);
            if (prepass) {
                box_gpu_forward_pipeline(gpu, format, &b, true);
                box_gpu_prepass_pipeline(gpu, &b, WGPUTextureFormat_Undefined);
            }
        }
    }

    if (f & BOX_RENDER_FEATURE_WATER) {
        box_render_batch_t b = {
            .view = -1,
            .scene = v->scene,
            .pipeline = BOX_RENDER_SHADER_MESH_EXTENDED,
            .features = f,
            .blend = AlphaBlend,
            .back_face = BackFaceCull,
            .transmissive = true,
            .water = true
        };
        box_gpu_forward_pipeline(gpu, format, &b, false);
        if (f & BOX_RENDER_FEATURE_TEXTURE_LAYERS) {
            b.texture_layers = true;
            box_gpu_forward_pipeline(gpu, format, &b, false);
        }
    }
}

#endif
