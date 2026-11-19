/**
 * @file render/render_packet.h
 * @brief Constants and types of render packets, their resources and the backend interface.
 *
 * Included by render.h, which declares the functions that build and consume the packets.
 */

#ifndef BOX_RENDER_PACKET_H
#define BOX_RENDER_PACKET_H

#include <box.h>

/** Byte alignment of every block in the data of a packet */
#define BOX_RENDER_DATA_ALIGN (16)

/** Shader handle of the engine shader that scales a scene texture to the presented image */
#define BOX_RENDER_SHADER_BLIT (1)

/** Shader handle of the engine shader of the forward pass that draws the opaque meshes without
 * the material features of BOX_RENDER_SHADER_MESH_EXTENDED */
#define BOX_RENDER_SHADER_MESH (2)

/** Shader handle of the engine compute shader that scatters changed instances into slots */
#define BOX_RENDER_SHADER_SCATTER (3)

/** Shader handle of the engine compute shader that assigns the lights of a view to clusters */
#define BOX_RENDER_SHADER_CLUSTER (4)

/** Shader handle of the engine shader that draws the casters into the cascades of a view */
#define BOX_RENDER_SHADER_SHADOW (5)

/** Shader handle of the engine compute shader that culls the instances of a view */
#define BOX_RENDER_SHADER_CULL (6)

/** Shader handle of the engine compute shader that places the instances of the mover pages in
 * the world from their transforms relative to their movers */
#define BOX_RENDER_SHADER_COMPOSE (12)

/** Projected size in pixels below which a capture of a camera without a view culls */
#define BOX_RENDER_CULL_MIN_DIMENSION (1.0f)

/** Shader handle of the engine shader of the tone map and exposure passes and the effect output */
#define BOX_RENDER_SHADER_TONEMAP (40)

/** Shader handle of the engine compute shader that measures and adapts the exposure */
#define BOX_RENDER_SHADER_EXPOSURE (41)

/** Shader handle of the engine shader of the bloom passes */
#define BOX_RENDER_SHADER_BLOOM (42)

/** Shader handle of the engine shader of the ambient occlusion (Gtao) passes */
#define BOX_RENDER_SHADER_GTAO (43)

/** Shader handle of the engine shader of the height fog pass */
#define BOX_RENDER_SHADER_FOG (44)

/** Shader handle of the engine compute shader of the atmosphere lookup tables */
#define BOX_RENDER_SHADER_ATMOSPHERE (45)

/** Shader handle of the engine compute shader that turns environment images into cubemaps */
#define BOX_RENDER_SHADER_ENVIRONMENT (46)
/** Shader handle of the engine compute shader that ages, moves and spawns particles */
#define BOX_RENDER_SHADER_PARTICLE_SIM (47)

/** Shader handle of the engine shader that draws particles */
#define BOX_RENDER_SHADER_PARTICLE_DRAW (48)

/** Shader handle of the engine shader of the SMAA passes */
#define BOX_RENDER_SHADER_SMAA (49)

/** Shader handle of the engine shader that blurs the opaque snapshot of transmission */
#define BOX_RENDER_SHADER_TRANSMISSION (50)

/** Shader handle of the engine shader that builds the mip pyramid of the planar reflections */
#define BOX_RENDER_SHADER_PLANAR (60)

/** Shader handle of the engine shader that raymarches the fog volumes of a view */
#define BOX_RENDER_SHADER_FOG_VOLUME (51)

/** Shader handle of the engine shader that raymarches the atmosphere shells of the planets of a
 * view */
#define BOX_RENDER_SHADER_PLANET_ATMOSPHERE (61)

/** Shader handle of the engine shader that draws the stars of the StarFields of a view */
#define BOX_RENDER_SHADER_STAR_FIELD (11)

/** Shader handle of the engine shader that draws the infinite grid of the debug pass of a view */
#define BOX_RENDER_SHADER_DEBUG_GRID (52)

/** Shader handle of the engine shader that draws the wireframe of the debug pass of a view */
#define BOX_RENDER_SHADER_DEBUG_WIRE (54)
/** Shader handle of the engine shader that draws the light and reflection probes of the debug
 * pass of a view */
#define BOX_RENDER_SHADER_DEBUG_PROBES (53)

/** Shader handle of the engine shader that draws the overdraw heat map of the debug pass of a
 * view */
#define BOX_RENDER_SHADER_DEBUG_OVERDRAW (55)

/** Shader handle of the engine shader that draws the boxes of the spatial index trees of the
 * debug pass of a view */
#define BOX_RENDER_SHADER_DEBUG_BVH (63)

/** Shader handle of the engine shader of the color grading pass */
#define BOX_RENDER_SHADER_GRADING (56)

/** Shader handle of the engine shader of the vignette pass */
#define BOX_RENDER_SHADER_VIGNETTE (57)

/** Shader handle of the engine shader of the chromatic aberration pass */
#define BOX_RENDER_SHADER_CHROMATIC_ABERRATION (58)

/** Shader handle of the engine shader of the depth of field passes */
#define BOX_RENDER_SHADER_DEPTH_OF_FIELD (59)

/** Shader handle of the shared code of the texture graph stages, which the GPU backend puts in
 * front of the stages it generates instead of compiling it alone */
#define BOX_RENDER_SHADER_TEXTURE_GRAPH (62)

/** Probes of a kind (light or reflection) the debug pass of a view draws at most; the bake
 * warns once about a volume with more for the first view that shows it */
#define BOX_RENDER_DEBUG_PROBES_MAX (65536)

/** Fog volumes a view draws at most, the nearest ones */
#define BOX_RENDER_FOG_VOLUMES_MAX (32)

/** Cascades of the shadow map of a view at most */
#define BOX_RENDER_SHADOW_CASCADES (4)

/** Widest tent filter of a shadow lookup in texels, odd */
#define BOX_RENDER_SHADOW_FILTER_MAX (7)

/** Widest blend band at the edge of a cascade, a fraction of its half extent */
#define BOX_RENDER_SHADOW_BLEND_MAX (0.5f)

/** Tiles of the shadow atlas of a scene at most, the tile records of its uniform buffer */
#define BOX_RENDER_SHADOW_TILES (64)

/** Tile record index of a light without a tile */
#define BOX_RENDER_SHADOW_NONE (0xFFFFFFFFu)

/** Smallest edge of a shadow atlas in texels */
#define BOX_RENDER_SHADOW_ATLAS_MIN (256)

/** Largest edge of a shadow atlas in texels, the 2D texture limit webgpu guarantees */
#define BOX_RENDER_SHADOW_ATLAS_MAX (8192)

/** Widest half angle of the projection of a spot light tile in degrees; a wider cone is
 * unshadowed outside it */
#define BOX_RENDER_SHADOW_SPOT_ANGLE_MAX (80.0f)

/** Near plane of a spot light tile as a fraction of the range of the light */
#define BOX_RENDER_SHADOW_NEAR_FRACTION (0.005f)

/** Smallest near plane of a spot light tile */
#define BOX_RENDER_SHADOW_NEAR_MIN (0.001f)


/** Shader handle of the engine shader that copies an impostor frame into its atlas, dilating
 * the colors of the covered texels into the empty ones */
#define BOX_RENDER_SHADER_IMPOSTOR (16)

/** Shader handle of the engine compute shader that steps the fields of WaterRipples */
#define BOX_RENDER_SHADER_WATER_RIPPLES (15)

/** Shader handle of the engine shader of the forward pass that draws the 2D shapes */
#define BOX_RENDER_SHADER_SHAPE2D (7)

/** Shader handle of the engine compute shader that projects probe captures onto SH */
#define BOX_RENDER_SHADER_BAKE (8)

/** Shader handle of the variant of the shadow shader that clips the octants of the point light
 * tiles with clip distances, compiled only by a device with that feature */
#define BOX_RENDER_SHADER_SHADOW_CLIP (9)

/** Shader handle of the engine shader of the forward pass that draws the meshes with a feature
 * the mesh shader lacks.
 *
 * Those are Transmission, a triplanar TextureMapping, Sheen, ClearCoat, SpecularAnisotropy,
 * DiffuseTransmission or Fresnel. */
#define BOX_RENDER_SHADER_MESH_EXTENDED (10)

/** Shader handle of the mesh shader compiled with BOX_TEXTURE_LAYERS: the layers entry points
 * the batches with TextureLayers draw with (box_render_mesh_layers). A module apart, so the
 * module of the other batches is unchanged. */
#define BOX_RENDER_SHADER_MESH_LAYERS (13)

/** Shader handle of the extended mesh shader compiled with BOX_TEXTURE_LAYERS */
#define BOX_RENDER_SHADER_MESH_EXTENDED_LAYERS (14)

/** Check whether a shader handle is one of the two mesh shaders.
 *
 * @param pipeline The shader handle of a batch
 * @return Whether it is BOX_RENDER_SHADER_MESH or BOX_RENDER_SHADER_MESH_EXTENDED
 */
static inline bool box_render_is_mesh(
    uint32_t pipeline)
{
    return pipeline == BOX_RENDER_SHADER_MESH || pipeline == BOX_RENDER_SHADER_MESH_EXTENDED;
}

/** Get the layers module of a mesh shader, which its batches with TextureLayers draw with.
 *
 * @param pipeline BOX_RENDER_SHADER_MESH or BOX_RENDER_SHADER_MESH_EXTENDED
 * @return BOX_RENDER_SHADER_MESH_LAYERS or BOX_RENDER_SHADER_MESH_EXTENDED_LAYERS
 */
static inline uint32_t box_render_mesh_layers(
    uint32_t pipeline)
{
    return pipeline == BOX_RENDER_SHADER_MESH_EXTENDED
        ? BOX_RENDER_SHADER_MESH_EXTENDED_LAYERS : BOX_RENDER_SHADER_MESH_LAYERS;
}

/** Check whether a shader handle is the layers module of a mesh shader.
 *
 * @param handle The shader handle
 * @return Whether it is BOX_RENDER_SHADER_MESH_LAYERS or BOX_RENDER_SHADER_MESH_EXTENDED_LAYERS
 */
static inline bool box_render_is_mesh_layers(
    uint32_t handle)
{
    return handle == BOX_RENDER_SHADER_MESH_LAYERS ||
        handle == BOX_RENDER_SHADER_MESH_EXTENDED_LAYERS;
}

/** Buffer handle of the change buffer the scatter pass reads changed instances from */
#define BOX_RENDER_BUFFER_CHANGES (1)

/** Buffer handle of the clip buffer: the areas of the ClipChildren elements the 2D draws read,
 * box_render_clip_t by clip index */
#define BOX_RENDER_BUFFER_CLIPS (2)

/** Buffer handle of the material table: box_render_material_t by MaterialId slot, which the mesh
 * instances reference by the id of their shading (box_render_mesh_shading_t). Bound with the
 * instance streams of every pass that draws meshes. */
#define BOX_RENDER_BUFFER_MATERIALS (3)

/** Buffer handle of the mover table: the world transform of each mover,
 * box_render_transform_t by mover index, index 0 the identity. The compose pass places the
 * instances of the mover pages with it. */
#define BOX_RENDER_BUFFER_MOVERS (4)

/** Buffer handle of the wave table: BOX_RENDER_WATER_WAVE_SETS box_render_water_waves_t, the
 * travelling waves of the Water materials with WaterWaves by the set their record names
 * (BOX_RENDER_WATER_WAVES_SHIFT), set 0 unused. Bound as uniforms by the view group of the
 * forward pass, which the Water fragments read it from. */
#define BOX_RENDER_BUFFER_WATER_WAVES (5)

/** Records the material table holds at least, doubling from there */
#define BOX_RENDER_MATERIALS_MIN (64)

/** Records the material table holds at most, the 128 MiB storage binding every WebGPU device
 * supports; an id past it renders with slot 0 */
#define BOX_RENDER_MATERIALS_MAX ((uint32_t)((128u << 20) / sizeof(box_render_material_t)))

/** Most nested clips a 2D draw intersects; clips further up the hierarchy are not applied */
#define BOX_RENDER_CLIP_DEPTH (16)

/** Buffer handle of the first instance page, the first handle of the instance pages.
 *
 * Every batch draws a run of slots of one instance page: a storage buffer per stream of at most
 * the chunk of its store (box_draws_layout_chunk). Plain mesh and 2D pages have no surface
 * stream (BOX_RENDER_INSTANCE_STREAMS_PLAIN), for which the backend binds a zero record; only
 * frame pages have the frame stream, for which the other pages bind a zero record too.
 *
 * box_render_batch_t.instances is the transform stream of the page, first_instance its slot.
 * Page p has the handle BOX_RENDER_BUFFER_FIRST + p * BOX_RENDER_INSTANCE_STREAMS + s for stream
 * s; a released page index is reused by the next page the store needs. */
#define BOX_RENDER_BUFFER_FIRST (16)

/** Buffers of a mesh instance page of the MappingFrame materials under a mover, one per stream,
 * at consecutive handles; the handles of every page leave room for this many */
#define BOX_RENDER_INSTANCE_STREAMS (5)

/** Pages each kind of instance page has handles for */
#define BOX_RENDER_PAGE_RANGE (0x00400000u)

/** Buffers of a mesh instance page with parametric surfaces: transform, shading and surface */
#define BOX_RENDER_INSTANCE_STREAMS_SURFACE (3)

/** Buffers of a plain mesh or a 2D instance page: the transform and the shading stream */
#define BOX_RENDER_INSTANCE_STREAMS_PLAIN (2)

/** Buffers of a 2D instance page: the transform and the shading stream */
#define BOX_RENDER_INSTANCE_STREAMS_2D (BOX_RENDER_INSTANCE_STREAMS_PLAIN)

/** Buffer handle of the first instance page of the meshes with a parametric surface, which have
 * the surface stream. Page p of these pages has the handle
 * BOX_RENDER_BUFFER_FIRST_SURFACE + p * BOX_RENDER_INSTANCE_STREAMS + s for stream s. */
#define BOX_RENDER_BUFFER_FIRST_SURFACE \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 1u)

/** Buffer handle of the first instance page of the plain meshes whose material uses
 * MappingFrame, which have the frame stream but no surface stream. Page p of these pages has the
 * handle BOX_RENDER_BUFFER_FIRST_FRAME + p * BOX_RENDER_INSTANCE_STREAMS + s for stream s. */
#define BOX_RENDER_BUFFER_FIRST_FRAME \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 2u)

/** Buffer handle of the first instance page of the meshes with a parametric surface whose
 * material uses MappingFrame, which have every stream. Page p of these pages has the handle
 * BOX_RENDER_BUFFER_FIRST_FRAME_SURFACE + p * BOX_RENDER_INSTANCE_STREAMS + s for stream s. */
#define BOX_RENDER_BUFFER_FIRST_FRAME_SURFACE \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 3u)

/** Buffer handle of the first instance page of the 2D shapes and glyphs. Page p of the 2D pages
 * has the handle BOX_RENDER_BUFFER_FIRST_2D + p * BOX_RENDER_INSTANCE_STREAMS + s for stream s. */
#define BOX_RENDER_BUFFER_FIRST_2D \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 4u)

/** Buffer handle of the first mover page: the plain mesh pages of the shapes that move with a
 * mover, an ancestor that owns DynamicTransform. Their transform stream holds the transforms
 * relative to the mover and their mover stream the index of the mover; the compose pass writes
 * their world transforms into a buffer the passes bind in place of the transform stream. The
 * mover pages with a surface, a frame or both follow at BOX_RENDER_PAGE_RANGE pages each. */
#define BOX_RENDER_BUFFER_FIRST_MOVER \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 5u)

/** Buffer handle of the first mover page with a parametric surface */
#define BOX_RENDER_BUFFER_FIRST_MOVER_SURFACE \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 6u)

/** Buffer handle of the first mover page of the MappingFrame materials */
#define BOX_RENDER_BUFFER_FIRST_MOVER_FRAME \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 7u)

/** Buffer handle of the first mover page with a parametric surface of the MappingFrame
 * materials */
#define BOX_RENDER_BUFFER_FIRST_MOVER_FRAME_SURFACE \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 8u)

/** Handle past the handles of the instance pages */
#define BOX_RENDER_BUFFER_PAGES_END \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE * 9u)

/** Stream of the transforms of an instance page, box_render_transform_t per slot */
#define BOX_RENDER_STREAM_TRANSFORM (0)

/** Stream of the shading constants of an instance page: box_render_mesh_shading_t per slot of
 * a mesh page, box_render_shading_t per slot of a 2D page */
#define BOX_RENDER_STREAM_SHADING (1)

/** Stream of the surface parameters of a mesh instance page with parametric surfaces,
 * box_render_surface_t per slot; plain mesh pages and 2D pages have none */
#define BOX_RENDER_STREAM_SURFACE (2)

/** Stream of the transforms relative to the texture frame of a frame page, the pages of the
 * MappingFrame materials and of the MappingObject materials under a TextureFrame:
 * box_render_transform_t per slot. For MappingFrame the inverse of the world transform of the
 * nearest TextureFrame, its scale included, times the model matrix (the model matrix without a
 * frame); for MappingObject the model matrix with its axes divided by the uniform world scale of
 * the frame. Every other page has none */
#define BOX_RENDER_STREAM_FRAME (3)

/** Stream of the movers of a mover page, uint32_t per slot: the index of the mover in the mover
 * table in the bits of BOX_RENDER_MOVER_INDEX, and BOX_RENDER_MOVER_FRAME when the compose pass
 * places the frame record with the mover too. Every other page has none */
#define BOX_RENDER_STREAM_MOVER (4)

/** Bits of the mover index of a record of the mover stream */
#define BOX_RENDER_MOVER_INDEX (0x7FFFFFFFu)

/** Bit of a record of the mover stream whose frame record is relative to the mover */
#define BOX_RENDER_MOVER_FRAME (0x80000000u)

/** Kinds of instance pages, by the range of their handles */
typedef enum box_render_page_kind_t {
    BOX_RENDER_PAGE_SURFACE = 1 << 0,   /**< The page has the surface stream */
    BOX_RENDER_PAGE_FRAME = 1 << 1,     /**< The page has the frame stream */
    BOX_RENDER_PAGE_MOVER = 1 << 2,     /**< The page has the mover stream */
    BOX_RENDER_PAGE_2D = 1 << 3         /**< A page of 2D shapes and glyphs */
} box_render_page_kind_t;

/** Get the kind of the instance page of a buffer handle.
 *
 * @param handle Handle of a stream of the page
 * @return box_render_page_kind_t flags, 0 for a plain mesh page or a handle of no page
 */
static inline uint32_t box_render_page_kind(
    uint32_t handle)
{
    static const uint32_t kinds[9] = {
        0, BOX_RENDER_PAGE_SURFACE, BOX_RENDER_PAGE_FRAME,
        BOX_RENDER_PAGE_FRAME | BOX_RENDER_PAGE_SURFACE, BOX_RENDER_PAGE_2D,
        BOX_RENDER_PAGE_MOVER, BOX_RENDER_PAGE_MOVER | BOX_RENDER_PAGE_SURFACE,
        BOX_RENDER_PAGE_MOVER | BOX_RENDER_PAGE_FRAME,
        BOX_RENDER_PAGE_MOVER | BOX_RENDER_PAGE_FRAME | BOX_RENDER_PAGE_SURFACE
    };
    uint32_t result = 0;
    if (handle >= BOX_RENDER_BUFFER_FIRST && handle < BOX_RENDER_BUFFER_PAGES_END) {
        result = kinds[(handle - BOX_RENDER_BUFFER_FIRST) /
            (BOX_RENDER_INSTANCE_STREAMS * BOX_RENDER_PAGE_RANGE)];
    }

    return result;
}

/** Byte alignment of a scatter segment in the change buffer, the storage offset alignment */
#define BOX_RENDER_SCATTER_ALIGN (256)

/** Words of the header of a scatter segment: count, words per instance and two unused */
#define BOX_RENDER_SCATTER_HEADER (4)

/** First buffer handle of the light buffers of scenes */
#define BOX_RENDER_BUFFER_LIGHT_FIRST (0x20000000u)

/** First buffer handle of the reflection probe buffers of scenes */
#define BOX_RENDER_BUFFER_REFLECT_FIRST (0x30000000u)

/** First handle of the shadow atlases of scenes: a BOX_RENDER_SHADOW_ATLAS and the uniform
 * BOX_RENDER_BUFFER of its tile records share the handle. The range must not overlap the
 * particle buffers at 0x50000000-0x5FFFFFFF, or the tile buffer replaces a particle page. */
#define BOX_RENDER_BUFFER_SHADOW_FIRST (0x60000000u)

/** First target handle of captures, the handles below belong to surfaces */
#define BOX_RENDER_CAPTURE_TARGET_FIRST (0x40000000u)

/** First target handle of the impostor frames, see render/impostor.c */
#define BOX_RENDER_IMPOSTOR_TARGET_FIRST (0x70000000u)

/** First texture handle of the impostor atlases, see render/impostor.c */
#define BOX_RENDER_IMPOSTOR_TEXTURE_FIRST (0x70000000u)

/** First shader handle of application shaders, the handles below are engine shaders */
#define BOX_RENDER_SHADER_FIRST (64)

/** Texture layers of a batch: albedo, emissive, ORM and normal, as in PbrTextures */
#define BOX_RENDER_TEXTURE_SLOTS (4)

/** Layers of a TextureBlend */
#define BOX_RENDER_BLEND_LAYERS (4)

/** Slices of the texture arrays of a blend: the layers of a TextureBlend or the kinds of a
 * TileTerrain */
#define BOX_RENDER_BLEND_SLICES (16)

/** Maps each layer of a TextureBlend samples: albedo, ORM and normal */
#define BOX_RENDER_BLEND_MAPS (3)

/** Map of a layer of a TextureBlend: albedo, display encoded */
#define BOX_RENDER_BLEND_ALBEDO (0)

/** Map of a layer of a TextureBlend: occlusion, roughness and metallic, stored */
#define BOX_RENDER_BLEND_ORM (1)

/** Map of a layer of a TextureBlend: tangent space normal, stored */
#define BOX_RENDER_BLEND_NORMAL (2)

/** Map of a layer of a TextureBlend: height in red, stored, sampled only with a contrast above 0,
 * after the maps every blend samples */
#define BOX_RENDER_BLEND_HEIGHT (3)

/** Textures of each layer of a TextureBlend in its record: the maps and the height */
#define BOX_RENDER_BLEND_SOURCES (BOX_RENDER_BLEND_MAPS + 1)

/** Layers of a TextureLayers */
#define BOX_RENDER_LAYERS (4)

/** Maps each layer of a TextureLayers samples, consecutive slices of its texture array: the
 * albedo (display encoded, stored as such), the ORM, the normal and the mask, stored */
#define BOX_RENDER_LAYER_MAPS (4)

/** Map of a layer of a TextureLayers: albedo, display encoded */
#define BOX_RENDER_LAYER_ALBEDO (0)

/** Map of a layer of a TextureLayers: occlusion, roughness and metallic, stored */
#define BOX_RENDER_LAYER_ORM (1)

/** Map of a layer of a TextureLayers: tangent space normal, stored */
#define BOX_RENDER_LAYER_NORMAL (2)

/** Map of a layer of a TextureLayers: the coverage mask in red, stored */
#define BOX_RENDER_LAYER_MASK (3)

/** Flags of box_render_texture_layer_t: the layer has an albedo, ORM, normal or mask texture
 * entity, and it maps with the UVs of the mesh instead of the world position */
#define BOX_RENDER_LAYER_HAS_ALBEDO (1u)
#define BOX_RENDER_LAYER_HAS_ORM (2u)
#define BOX_RENDER_LAYER_HAS_NORMAL (4u)
#define BOX_RENDER_LAYER_HAS_MASK (8u)
#define BOX_RENDER_LAYER_UV (16u)

/** Anisotropic samples of the texture samplers when no surface sets them */
#define BOX_RENDER_ANISOTROPY_DEFAULT (4)

/** Bytes of texture images a packet carries before the rest waits for the next frame */
#define BOX_RENDER_TEXTURE_BUDGET (32 * 1024 * 1024)

/** Flag of a texture handle that samples the image of the target in its low bits */
#define BOX_RENDER_TEXTURE_TARGET (0x80000000u)

/** Bytes of packet data a reused packet always keeps; a larger block shrinks to the largest frame
 * of BOX_RENDER_DATA_TRIM_RESETS resets in a row that fit in this size or used under a
 * BOX_RENDER_DATA_TRIM_FILL share of it */
#define BOX_RENDER_DATA_KEEP (16 * 1024 * 1024)

/** Divisor of the capacity of a large data block under which a frame counts as using little */
#define BOX_RENDER_DATA_TRIM_FILL (4)

/** Resets in a row that use little of a large data block before the block is freed */
#define BOX_RENDER_DATA_TRIM_RESETS (120)

/** Phase after EcsOnStore in which modules append what a frame renders to its packet */
extern ECS_DECLARE(RenderExtract);

/** Phase after RenderExtract that hands the packet of the frame to the renderer */
extern ECS_DECLARE(RenderSubmit);

/** Kind of backend object a resource change applies to */
typedef enum box_render_resource_kind_t {
    BOX_RENDER_BUFFER,   /**< GPU buffer, the target of uploads */
    BOX_RENDER_MESH,     /**< Vertex and index data */
    BOX_RENDER_TEXTURE,  /**< Sampled texture */
    BOX_RENDER_TARGET,   /**< Surface or offscreen target that views render into */
    BOX_RENDER_SHADER,   /**< Shader module or pipeline */
    BOX_RENDER_VOLUME,   /**< Irradiance probe volume of a scene with LightBake */
    BOX_RENDER_REFLECTION, /**< Cubemap array of the reflection probes of a scene */
    BOX_RENDER_ATMOSPHERE, /**< Transmittance and multi scattering LUTs of a scene */
    BOX_RENDER_ENVIRONMENT, /**< Cubemap of an environment source */
    BOX_RENDER_ENVIRONMENT_IMAGE, /**< Equirectangular image an EnvironmentImage writes into the
                                  *   cubemap of its handle */
    BOX_RENDER_ATMOSPHERE_ENVIRONMENT, /**< Sky an Atmosphere renders into the cubemap of its
                                       *   handle */
    BOX_RENDER_SHADOW_ATLAS, /**< Depth32Float shadow atlas of the spot and point light tiles
                             *   of a scene */
    BOX_RENDER_TEXTURE_GRAPH, /**< Compiled TextureGraph the backend evaluates into the image
                             *   of the texture of its handle, box_render_texture_graph_t */
    BOX_RENDER_TEXTURE_NODE_KIND /**< Node kind of the texture graphs whose id is its handle,
                             *   box_render_texture_node_kind_t */
} box_render_resource_kind_t;

/** What a resource change does to its object */
typedef enum box_render_resource_op_t {
    BOX_RENDER_CREATE,   /**< Create the object, or recreate it when the handle exists */
    BOX_RENDER_UPDATE,   /**< Change the object in place */
    BOX_RENDER_DESTROY   /**< Release the object and its handle */
} box_render_resource_op_t;

/** Creation, change or release of a backend object, applied before the views render */
typedef struct box_render_resource_t {
    box_render_resource_kind_t kind; /**< Kind of object */
    box_render_resource_op_t op;     /**< What happens to it */
    uint32_t handle;                 /**< Handle the main thread assigned to the object */
    uint32_t size;                   /**< Bytes of the description, 0 for none */
    uint32_t data;                   /**< Offset of the description in the packet data */
} box_render_resource_t;

/** Bytes written into a buffer before the views render */
typedef struct box_render_upload_t {
    uint32_t buffer;    /**< Handle of the destination buffer */
    uint32_t offset;    /**< Byte offset in the destination buffer */
    uint32_t size;      /**< Bytes to write */
    uint32_t data;      /**< Offset of the bytes in the packet data */
} box_render_upload_t;

/** A light of a scene as the forward pass reads it from the light buffer of the scene.
 *
 * The directional lights of a scene come first in its buffer, then its point and spot lights.
 * A light without a cone has a spot_scale of 0 and a spot_offset of 1.
 *
 * shadow is the only member that is not the light itself: the bake and the reflection
 * invalidation compare the members before it (BOX_RENDER_LIGHT_SHADING bytes).
 */
typedef struct box_render_light_t {
    float position[3];  /**< World position, the direction toward a directional light */
    float range;        /**< Distance at which the light reaches zero, 0 for directional */
    float color[3];     /**< Linear color times intensity, the irradiance of the light */
    float spot_scale;   /**< Cone falloff scale, 1 / (cos inner - cos outer) */
    float direction[3]; /**< Direction a spot light shines in */
    float spot_offset;  /**< Cone falloff offset, -cos outer times spot_scale */
    uint32_t shadow;    /**< Index of the tile record of the light in the shadow atlas of its
                         *   scene, BOX_RENDER_SHADOW_NONE for none */
    uint32_t pad[3];    /**< Unused, zero */
} box_render_light_t;

/** Bytes of a light record that describe the light, its tile index excluded */
#define BOX_RENDER_LIGHT_SHADING (offsetof(box_render_light_t, shadow))

/** Projection of a shadow atlas tile: the perspective of a spot light, its record matrix */
#define BOX_RENDER_SHADOW_PERSPECTIVE (0)

/** Projection of a shadow atlas tile: the 8 octants of a point light unfolded into the tile */
#define BOX_RENDER_SHADOW_OCTAHEDRAL (1)

/** Octants of the octahedral projection of a point light tile */
#define BOX_RENDER_SHADOW_OCTANTS (8)

/** A tile of a shadow atlas as the forward pass reads it from the tile records of the scene,
 * ShadowTile of etc/shaders/include/spot_shadow.wgsl.
 *
 * A spot light tile has a reversed Z perspective along the light in its matrix: clip depth
 * near * (far - z) / (z * (far - near)) at the distance z along its axis, 1 at the near plane.
 *
 * A point light tile has a zero matrix; each octant of its octahedron is a perspective of the
 * same depth with z the distance along the axis (+-1, +-1, +-1) / sqrt(3) of the octant.
 */
typedef struct box_render_shadow_record_t {
    mat4 matrix;        /**< World to shadow clip space of a spot light, zero for a point light */
    float rect[4];      /**< Left and top of the tile in atlas uv, its edge in uv, a texel of
                         *   the atlas in uv */
    float params[4];    /**< Near and far plane, world size of a texel per unit of distance
                         *   along the light axis (the octant axis of a point light), edge of
                         *   the tile in texels */
    float light[4];     /**< World position of the light, the projection of the tile in w:
                         *   BOX_RENDER_SHADOW_PERSPECTIVE or BOX_RENDER_SHADOW_OCTAHEDRAL */
} box_render_shadow_record_t;

/** A tile the renderer draws the shadow casters of its light into */
typedef struct box_render_shadow_tile_t {
    ecs_entity_t light;     /**< Entity of the light */
    int32_t x;              /**< Left column of the tile in the atlas */
    int32_t y;              /**< Top row of the tile in the atlas */
    int32_t size;           /**< Edge of the tile in texels */
    int32_t projection;     /**< BOX_RENDER_SHADOW_PERSPECTIVE or BOX_RENDER_SHADOW_OCTAHEDRAL */
    mat4 matrix;            /**< World to shadow clip space of the light, as its record */
    float position[3];      /**< World position of the light */
    float near_;            /**< Near plane */
    float far_;             /**< Far plane, the radius of the sphere an octahedral tile culls its
                             *   casters by */
    bool render;            /**< The tile renders in this packet: it is new, moved or resized, or
                             *   its light or a caster within its range changed since it last
                             *   rendered; else the atlas keeps what it holds */
} box_render_shadow_tile_t;

/** The shadow atlas of a scene in a packet: its tiles that render, once per packet before the
 * targets, in one render pass that clears only those tiles or the atlas when every tile renders.
 * An atlas that cannot keep its tiles (new, shadow pipelines changed) renders every tile */
typedef struct box_render_shadow_atlas_t {
    ecs_entity_t scene;     /**< Scene root of the lights and their casters, 0 for the default */
    uint32_t handle;        /**< Handle of the BOX_RENDER_SHADOW_ATLAS and of its tile buffer */
    int32_t resolution;     /**< Edge of the atlas in texels */
    int32_t first;          /**< First of its tiles in the shadow tiles of the packet */
    int32_t count;          /**< Its tiles, in the order of their records */
} box_render_shadow_atlas_t;

/** Description of a BOX_RENDER_SHADOW_ATLAS create; a create of an existing handle replaces it */
typedef struct box_render_shadow_atlas_desc_t {
    int32_t resolution;     /**< Edge of the atlas in texels, a power of two */
} box_render_shadow_atlas_desc_t;

/** The LightCookie of the first directional light of a view, resolved for the backend */
typedef struct box_render_view_cookie_t {
    uint32_t texture;        /**< Backend handle of its texture, 0 for no cookie: no texture, a
                              *   texture the backend has no image of yet, no effect */
    uint32_t projection;     /**< LightCookieProjection */
    float inv_size;          /**< One over the world units the texture repeats over */
    float height;            /**< World Y of the plane of the Plane projection */
    float offset[2];         /**< Offset plus velocity times the total time in world units,
                              *   wrapped to [0, size) */
    float threshold;         /**< Value at which the remapped cookie is half lit */
    float softness;          /**< Half the range of values the remap ramps over */
    float strength;          /**< Mix from no cookie to the remapped value */
    float axis_x[3];         /**< Local X axis of the light in world space, normalized */
    float axis_y[3];         /**< Local Y axis of the light in world space, normalized */
} box_render_view_cookie_t;

/** The lights a view is shaded with: the light buffer of its scene and its ambient light */
typedef struct box_render_view_lights_t {
    bool enabled;            /**< Shade with the lights, else draw base color and emission */
    uint32_t buffer;         /**< Handle of the light buffer, BOX_RENDER_BUFFER_LIGHT_FIRST for
                              *   the shared buffer of the default sun */
    uint32_t directional;    /**< Directional lights at the start of the buffer */
    uint32_t count;          /**< Lights in the buffer, the directional ones included */
    float ambient_sky[4];    /**< Linear ambient light from above */
    float ambient_ground[4]; /**< Linear ambient light from below */
    float sun[3];            /**< Direction toward the first directional light, if any */
    float sun_color[3];      /**< Linear color times intensity of that light */
    box_render_view_cookie_t cookie; /**< LightCookie of that light, texture 0 for none */
} box_render_view_lights_t;

/** The shadow settings of a view, resolved, with the view depths its cascades end at.
 *
 * The first directional light of the scene casts the cascades; they are fitted to the target
 * the view renders into, because the frustum follows its aspect. The spot lights with a tile in
 * the shadow atlas of the scene are shadowed through it.
 */
typedef struct box_render_view_shadows_t {
    bool enabled;           /**< The view renders shadows and its scene has a directional light */
    int32_t cascades;       /**< Cascades, 1 to BOX_RENDER_SHADOW_CASCADES */
    int32_t resolution;     /**< Edge of a cascade in texels */
    int32_t filter_size;    /**< Width of the tent filter in texels, odd, 1 to
                             *   BOX_RENDER_SHADOW_FILTER_MAX */
    float depth_bias;       /**< Depth offset of the comparison in texels */
    float normal_offset;    /**< Offset of the lookup along the geometric normal in texels at
                             *   grazing light, scaled by 1 - N.L */
    float fade;             /**< Fraction of the last cascade that fades out */
    float blend;            /**< Fraction of the half extent of a cascade that blends into
                             *   the next one at its edge */
    float min_texels;       /**< Projected size in texels of a cascade below which a caster
                             *   is not drawn into it, 0 for none */
    float near_;            /**< View depth the first cascade starts at */
    float splits[BOX_RENDER_SHADOW_CASCADES]; /**< View depth each cascade ends at */
    float direction[3];     /**< Direction toward the light that casts the shadows */
    uint32_t render;        /**< Mask of the cascades that render in this frame; the others keep
                             *   their depth and the matrix of the frame they rendered in, which
                             *   the renderer also renders when it does not hold them */
    bool reuse;             /**< A cascade has a ShadowSettings.interval above 1: the cascades
                             *   live in a shadow map of the view, which no other view renders
                             *   into, else in the one the views share */
    bool spots;             /**< The view samples the spot and point light tiles of the atlas
                             *   of its scene: it renders shadows and is lit; filter_size,
                             *   depth_bias and normal_offset apply, in texels of a tile */
    uint32_t atlas;         /**< Handle of that atlas and its tile buffer, 0 for none */
    uint32_t tiles;         /**< Tile records of that atlas */
} box_render_view_shadows_t;

/** A CloudLayer of the scene of a view, resolved for the sky pass of the view */
typedef struct box_render_cloud_layer_t {
    uint32_t texture;       /**< Backend handle of its texture */
    float inv_size;         /**< One over the world units the texture repeats over */
    float height;           /**< World Y of the layer minus that of the camera */
    float altitude;         /**< World Y of the layer */
    float offset[2];        /**< Offset plus velocity times the total time in world units,
                             *   wrapped to [0, size) */
    float origin[2];        /**< Texture coordinate of the camera: its world X and Z plus the
                             *   offset over the size, wrapped to [0, 1) in double precision */
    float threshold;        /**< Value at which the coverage is one half */
    float softness;         /**< Half the range of values the coverage ramps over */
    float density;          /**< Opacity of fully covered clouds */
    float tint[3];          /**< Linear tint */
    ecs_entity_t entity;    /**< Entity with the CloudLayer */
} box_render_cloud_layer_t;

/** What the sky view and aerial perspective passes of a view read besides the medium of the
 * atmosphere of its scene */
typedef struct box_render_view_atmos_t {
    float sun[4];           /**< Direction toward the sun, w unused; (0, -1, 0) without one */
    float sun_color[4];     /**< Top of atmosphere irradiance of the sun: its light color times
                             *   intensity before the transmittance, 0 without one */
    float night[4];         /**< Linear night_tint times night_intensity */
    float sun_disk[4];      /**< Cosine and sine of the angular radius of the sun disk and its
                             *   sun_disk_intensity, w unused */
    float moon[4];          /**< Direction toward the moon and in w its moon_illumination;
                             *   (0, -1, 0, 0) without one */
    float moon_color[4];    /**< Top of atmosphere irradiance of the moon: its light color times
                             *   intensity before the transmittance, 0 without one */
    float moon_disk[4];     /**< Cosine and sine of the angular radius of the moon disk, its
                             *   moon_disk_intensity times moon_albedo and its earthshine */
    float altitude;         /**< Altitude of the camera above the ground in km, at least 1 m */
    float mie_anisotropy;   /**< Mie phase asymmetry g */
    float aerial_distance;  /**< Far end of the aerial perspective volume in km */
    float aerial_intensity; /**< Scale of the scattering and extinction of the volume */
    float km_per_unit;      /**< Kilometers in a world unit, the inverse of units_per_km */
    float fog_light[4];     /**< Direction toward the main light of the atmosphere, the sun or
                             *   the moon, and in w the fade of its disk at the horizon; w 0
                             *   without one */
    float fog_moon[4];      /**< Radiance the fog scatters toward the moon as the main light,
                             *   its attenuated color over pi, a glow the sky view hardly
                             *   holds; w 1 when the moon is the main light */
    bool sky;               /**< LightSettings.atmosphere: the view draws the sky and applies
                             *   the aerial perspective to its 3D meshes */
    ecs_entity_t entity;    /**< Entity with the Atmosphere of the scene of the view */
    box_render_cloud_layer_t clouds[BOX_CLOUD_LAYER_MAX]; /**< The cloud layers with a loaded
                             *   texture by ascending altitude, for views with sky */
    int32_t cloud_count;    /**< Layers in clouds */
} box_render_view_atmos_t;

/** The environment a view is lit with, resolved from LightSettings.environment */
typedef struct box_render_view_env_t {
    uint32_t handle;        /**< Handle of the BOX_RENDER_ENVIRONMENT, 0 for none */
    float intensity;        /**< Scale of the environment light */
    float yaw;              /**< Rotation of the environment about +Y in radians */
    bool background;        /**< Draw the environment in place of the clear color */
} box_render_view_env_t;

/** A box of a tree of the spatial index the debug pass of a view draws (BvhDebug), matching
 * BvhBox in debug_bvh.wgsl */
typedef struct box_render_debug_box_t {
    vec3 min;           /**< Minimum corner in the space of its pose */
    uint32_t kind;      /**< BOX_SPATIAL_STATIC, BOX_SPATIAL_DYNAMIC, BOX_SPATIAL_UI or
                         *   BOX_RENDER_DEBUG_BOX_MOVER, with BOX_RENDER_DEBUG_BOX_LEAF for the
                         *   leaf of an entity */
    vec3 max;           /**< Maximum corner in the space of its pose */
    uint32_t depth;     /**< Depth of the box in its tree, 0 for the root */
    uint32_t pose;      /**< Index of the matrix that places the box in the debug poses of the
                         *   view, 0 for the identity of the boxes in world space */
    uint32_t padding[3];
} box_render_debug_box_t;

/** Tree of box_render_debug_box_t.kind of the tree of a mover in its frame */
#define BOX_RENDER_DEBUG_BOX_MOVER (3u)

/** Bit of box_render_debug_box_t.kind of the leaf of an entity */
#define BOX_RENDER_DEBUG_BOX_LEAF (4u)

/** A FogVolume a view raymarches, resolved; the layout of FogVolume in fog_volume.wgsl.
 *
 * The box is centered on center and spans extent along each of its axes, the world rotation of
 * the entity; the values are clamped to their ranges.
 */
typedef struct box_render_fog_volume_t {
    float center[3];        /**< World position of the entity */
    float density;          /**< FogVolume.density, at least 0 */
    float axes[3][4];       /**< World direction of the local X, Y and Z of the box, w unused */
    float extent[3];        /**< FogVolume.extent, each at least 0 */
    float absorption;       /**< FogVolume.absorption, 0 to 1 */
    float color[3];         /**< Linear FogVolume.color */
    float asymmetry;        /**< FogVolume.asymmetry, -0.9 to 0.9 */
    float edge_fade;        /**< FogVolume.edge_fade, at least 0 */
    float sun_inscatter;    /**< FogVolume.sun_inscatter, at least 0 */
    float ambient_inscatter; /**< FogVolume.ambient_inscatter, at least 0 */
    int32_t steps;          /**< FogVolume.steps, 4 to 128 */
    float offset[3];        /**< World offset the noise moves by: FogVolume.wind times the world
                             *   time, in double precision and wrapped to [0, 10 noise_scale),
                             *   the period of the noise, see box_render_fog_offset */
    float noise_strength;   /**< FogVolume.noise_strength, 0 to 1 */
    float noise_scale;      /**< FogVolume.noise_scale, at least BOX_RENDER_FOG_NOISE_SCALE_MIN */
    float light_inscatter;  /**< FogVolume.light_inscatter, at least 0 */
    float pad[2];           /**< Pads the record to the 16 byte stride of its WGSL array */
} box_render_fog_volume_t;

/** A PlanarReflection a view renders a mirrored image of, resolved by the extract
 * (planar_reflections.c). The planes and matrices are in world space. */
typedef struct box_render_planar_t {
    ecs_entity_t entity;    /**< Entity with the PlanarReflection */
    int32_t slot;           /**< Slot of the reflection in its view, 0 for the plane nearest to
                             *   the camera, below BOX_PLANAR_REFLECTION_MAX */
    float plane[4];         /**< Unit normal, the local +Y of the entity, and minus its dot with
                             *   the position: positive on the front side */
    float clip[4];          /**< The plane moved by PlanarReflection.clip_offset against its
                             *   normal, which keeps what is on its positive side */
    mat4 reflection;        /**< Mirrors a point about the plane */
    mat4 view_matrix;       /**< View matrix of the mirrored camera: the view matrix of the view
                             *   times the reflection */
    float camera[3];        /**< World position of the mirrored camera */
    float resolution;       /**< PlanarReflection.resolution, 0.125 to 1 */
    uint32_t exclude_page;  /**< Handle of the transform stream of the instance page of the shape
                             *   of the entity, which the reflection does not draw, 0 for none */
    uint32_t exclude_slot;  /**< Slot of that shape in the page */
    vec4 owner[3];          /**< Rows of the transform of that instance as its page holds it,
                             *   which its vertices compare to find their reflection slot */
    float distortion;       /**< PlanarReflection.distortion */
} box_render_planar_t;

/** Noise scales over which the noise of a fog volume repeats: the coarse sample repeats every
 * scale and the detail sample, at 3.1 times it, every 10 */
#define BOX_RENDER_FOG_NOISE_PERIOD (10.0)

/** Smallest FogVolume.noise_scale a record keeps */
#define BOX_RENDER_FOG_NOISE_SCALE_MIN (0.01f)

/** The culling of a view, resolved from its CullSettings */
typedef struct box_render_view_cull_t {
    bool enabled;           /**< Instances outside the frustum or too small are not drawn */
    bool occlusion;         /**< Instances hidden behind what the view drew are not drawn,
                             *   false for a view with WireframeOnly, which writes no depth */
    float min_dimension;    /**< Projected size in pixels below which an instance is culled */
    bool bounds;            /**< Batches outside a frustum of the view, by their world bounds,
                             *   are not drawn in it; only with enabled */
} box_render_view_cull_t;

/** The ambient occlusion of a view, resolved from the first enabled Gtao of its effects */
typedef struct box_render_view_gtao_t {
    bool enabled;           /**< The view is lit, has a perspective camera and lists a Gtao */
    ecs_entity_t effect;    /**< The Gtao entity, keys the images the passes keep */
    Gtao settings;          /**< Resolved settings */
} box_render_view_gtao_t;

/** Scene features (the members of Scene) of the mesh shaders, each the override of the same
 * name; the lighting ones also gate the uniforms of the views of the scene */
#define BOX_RENDER_FEATURE_BAKED (1u << 0)
#define BOX_RENDER_FEATURE_REFLECTIONS (1u << 1)
#define BOX_RENDER_FEATURE_PUNCTUAL_SHADOWS (1u << 2)
#define BOX_RENDER_FEATURE_COOKIES (1u << 3)
#define BOX_RENDER_FEATURE_ENVIRONMENT (1u << 4)
#define BOX_RENDER_FEATURE_AERIAL (1u << 5)
#define BOX_RENDER_FEATURE_SHEEN (1u << 6)
#define BOX_RENDER_FEATURE_CLEAR_COAT (1u << 7)
#define BOX_RENDER_FEATURE_ANISOTROPY (1u << 8)
#define BOX_RENDER_FEATURE_DIFFUSE_TRANSMISSION (1u << 9)
#define BOX_RENDER_FEATURE_TRIPLANAR (1u << 10)
#define BOX_RENDER_FEATURE_FRESNEL (1u << 11)
#define BOX_RENDER_FEATURE_TRANSMISSION (1u << 12)

/** Scene features of the mesh shaders without an override: the TextureBlend entry points of the
 * mesh shaders, and the alpha test and blend modes of the DrawKind */
#define BOX_RENDER_FEATURE_TEXTURE_BLEND (1u << 13)
#define BOX_RENDER_FEATURE_ALPHA_CUTOUT (1u << 14)
#define BOX_RENDER_FEATURE_BLEND_MODES (1u << 15)

/** A view draws a depth prepass before its forward pass (LightSettings.depth_prepass), in the
 * features of a view only */
#define BOX_RENDER_FEATURE_DEPTH_PREPASS (1u << 16)

/** Water (Scene.water): its shapes draw transmissive with the water override of the extended
 * mesh shader, which follows the water pipelines like the transmission override */
#define BOX_RENDER_FEATURE_WATER (1u << 17)

/** Planar reflections (Scene.planar_reflections): the views render the images of the
 * PlanarReflections of the scene, and their shapes draw with the extended mesh shader, whose
 * scene_planar override samples them */
#define BOX_RENDER_FEATURE_PLANAR (1u << 18)

/** TextureScroll (Scene.texture_scroll): the materials pack the scroll offsets, and both mesh
 * shaders sample the normal map at the offset of its normal_velocity (material_texture_scroll
 * override); without it a shape with a TextureScroll draws as without one */
#define BOX_RENDER_FEATURE_TEXTURE_SCROLL (1u << 19)

/** Planet surface light (Scene.planet_light): both mesh shaders attenuate the sun of the
 * PlanetAtmosphere of an instance by its transmittance (scene_planet_light override) */
#define BOX_RENDER_FEATURE_PLANET_LIGHT (1u << 20)

/** Distortion (Scene.distortion): its shapes draw transmissive, and the transmissive pipelines
 * of the extended mesh shader offset the snapshot by its noise (material_distortion override) */
#define BOX_RENDER_FEATURE_DISTORTION (1u << 21)

/** Wind (Scene.wind): the materials pack their WindSway with the Wind of the frame, and the
 * batches of the shapes with one (box_render_batch_t.wind) draw with the material_wind
 * override of every vertex stage, which moves their vertices; the other batches keep the
 * pipelines of a scene without it */
#define BOX_RENDER_FEATURE_WIND (1u << 22)

/** Texture layers (Scene.texture_layers): the batches of the shapes with TextureLayers
 * (box_render_batch_t.texture_layers) draw with the layers entry points of the layers modules
 * of the mesh shaders (box_render_mesh_layers) and their own bind group layouts; the other
 * batches keep the modules and pipelines of a scene without it */
#define BOX_RENDER_FEATURE_TEXTURE_LAYERS (1u << 23)

/** The scene features of the mesh shader */
#define BOX_RENDER_FEATURE_LIGHTING (0x3Fu)

/** The scene features that draw with the extended mesh shader */
#define BOX_RENDER_FEATURE_MATERIALS (0x3FC0u | BOX_RENDER_FEATURE_WATER | \
    BOX_RENDER_FEATURE_PLANAR | BOX_RENDER_FEATURE_DISTORTION)

/** The scene features that are overrides in the order of their bits, which key the pipelines
 * of the mesh shaders with the TEXTURE_SCROLL, PLANET_LIGHT and PLANAR features; the
 * transmission override follows the transmissive pipelines instead */
#define BOX_RENDER_FEATURE_OVERRIDES (0xFFFu)

/** Overrides of the extended mesh shader, bit i the override box_render_mesh_override_key(i):
 * the lighting ones, material_texture_scroll, scene_planet_light, the material ones,
 * scene_planar, transmissive, water and material_distortion; the mesh shader has the first
 * PLAIN ones. */
#define BOX_RENDER_MESH_OVERRIDES (18)

/** Overrides of the mesh shader: the lighting ones, material_texture_scroll and
 * scene_planet_light */
#define BOX_RENDER_MESH_OVERRIDES_PLAIN (8)

/** Bit of the material_texture_scroll override */
#define BOX_RENDER_OVERRIDE_TEXTURE_SCROLL (1u << 6)

/** Bit of the scene_planet_light override */
#define BOX_RENDER_OVERRIDE_PLANET_LIGHT (1u << 7)

/** Bit of the material_fresnel override */
#define BOX_RENDER_OVERRIDE_FRESNEL (1u << 13)

/** Bit of the scene_planar override */
#define BOX_RENDER_OVERRIDE_PLANAR (1u << 14)

/** Bit of the transmissive override */
#define BOX_RENDER_OVERRIDE_TRANSMISSIVE (1u << 15)

/** Bit of the water override */
#define BOX_RENDER_OVERRIDE_WATER (1u << 16)

/** Bit of the material_distortion override */
#define BOX_RENDER_OVERRIDE_DISTORTION (1u << 17)

/** The overrides that come from the scene features, without transmissive, water and
 * material_distortion */
#define BOX_RENDER_OVERRIDE_SCENE (0x7FFFu)

/** Get the name of an override of the mesh shaders.
 *
 * @param index Index of the override, below BOX_RENDER_MESH_OVERRIDES
 * @return Its name in the mesh shaders
 */
static inline const char* box_render_mesh_override_key(
    int32_t index)
{
    static const char *keys[BOX_RENDER_MESH_OVERRIDES] = {
        "scene_baked", "scene_reflections", "scene_punctual_shadows", "scene_cookies",
        "scene_environment", "scene_aerial", "material_texture_scroll", "scene_planet_light",
        "material_sheen", "material_clear_coat", "material_anisotropy",
        "material_diffuse_transmission", "material_triplanar", "material_fresnel",
        "scene_planar", "transmissive", "water", "material_distortion"
    };

    return keys[index];
}

/** Compute the override values of a pipeline of a mesh shader, a scene feature that is off
 * compiles out its code.
 *
 * The mesh shader takes the lighting features, the TextureScroll and the planet light ones,
 * the extended mesh shader all of them, transmissive for the pipelines of transmissive shapes,
 * water for those of transmissive Water shapes and material_distortion for the transmissive
 * ones of a scene with Scene.distortion.
 *
 * @param shader The shader handle, 0 overrides for one that is not a mesh shader
 * @param features BOX_RENDER_FEATURE_* features of the scene the pipeline draws
 * @param transmissive Whether the pipeline draws transmissive shapes
 * @param water Whether the pipeline draws Water shapes, with transmissive
 * @return The overrides that are true, bit i the override box_render_mesh_override_key(i)
 */
static inline uint32_t box_render_mesh_overrides(
    uint32_t shader,
    uint32_t features,
    bool transmissive,
    bool water)
{
    uint32_t result = 0;
    if (!box_render_is_mesh(shader)) {
        goto done;
    }

    result = (features & BOX_RENDER_FEATURE_LIGHTING) |
        (features & BOX_RENDER_FEATURE_TEXTURE_SCROLL ? BOX_RENDER_OVERRIDE_TEXTURE_SCROLL : 0u) |
        (features & BOX_RENDER_FEATURE_PLANET_LIGHT ? BOX_RENDER_OVERRIDE_PLANET_LIGHT : 0u);
    if (shader != BOX_RENDER_SHADER_MESH_EXTENDED) {
        goto done;
    }

    result |= (features & BOX_RENDER_FEATURE_OVERRIDES & ~BOX_RENDER_FEATURE_LIGHTING) << 2;
    result |= features & BOX_RENDER_FEATURE_PLANAR ? BOX_RENDER_OVERRIDE_PLANAR : 0u;
    result |= transmissive ? BOX_RENDER_OVERRIDE_TRANSMISSIVE : 0u;
    result |= transmissive && water ? BOX_RENDER_OVERRIDE_WATER : 0u;
    result |= transmissive && (features & BOX_RENDER_FEATURE_DISTORTION)
        ? BOX_RENDER_OVERRIDE_DISTORTION : 0u;
done:
    return result;
}

/** A view of the frame: the camera it renders, the scene it draws and how it clears.
 *
 * The projection is built by the renderer per target, because its aspect follows the
 * surface; views are in render order: by View.order, then in the order they were created.
 */
typedef struct box_render_view_t {
    ecs_entity_t view;          /**< Entity with the View component */
    int32_t order;              /**< View.order, the views render in ascending order */
    ecs_entity_t camera;        /**< Entity with the Camera component */
    ecs_entity_t parent;        /**< Parent of the view, matched against Surface.view_root */
    ecs_entity_t scene;         /**< Scene root of the camera, 0 for the default scene */
    mat4 camera_transform;      /**< World transform of the camera */
    mat4 view_matrix;           /**< Inverse of the camera transform */
    Camera camera_data;         /**< Settings of the camera */
    ClearSettings clear;        /**< Clear settings of the view */
    uint32_t target;            /**< Handle of the only target that renders the view, 0 for any */
    bool claimed;               /**< A surface has the parent of the view as its view_root, so
                                 *   surfaces without a view_root skip the view */
    box_render_view_lights_t lights; /**< Lights of the scene, filled by the light extract */
    box_render_view_shadows_t shadows; /**< Shadows of the view, filled by the shadow extract */
    box_render_view_cull_t cull; /**< Culling of the view and of its shadow casters */
    int32_t effect_first;       /**< Index of the first effect pass of the view in the packet */
    int32_t effect_count;       /**< Effect passes of the view, 0 renders without an HDR target */
    box_render_view_gtao_t gtao; /**< Ambient occlusion of the indirect light, which runs between
                                 *   the depth prepass and the forward pass, not in the chain */
    uint32_t volume;            /**< Probe volume the view samples, 0 for the hemisphere ambient */
    uint32_t volume_buffer;     /**< Probe buffer of the volume the view samples, 0 or 1 */
    uint32_t reflections;       /**< Reflection probe buffer of the scene, 0 for none or when
                                 *   the view does not sample reflection probes */
    uint32_t reflection_count;  /**< Resident reflection probes in that buffer */
    uint32_t reflection_listed; /**< Probes of that buffer the clusters list: the resident
                                 *   ReflectionProbes, which come before the scene probe */
    uint32_t atmosphere;        /**< Handle of the BOX_RENDER_ATMOSPHERE of its scene, 0 when the
                                 *   scene has no valid Atmosphere and the view runs no
                                 *   atmosphere pass */
    box_render_view_atmos_t atmos; /**< Inputs of its sky view and aerial perspective */
    box_render_view_env_t environment; /**< Environment the view is lit with */
    int32_t planet_first;       /**< Index of the first planet atmosphere of the view in the
                                 *   packet */
    int32_t planet_count;       /**< Planet atmospheres the view draws, back to front, filled
                                 *   by the atmosphere extract for a lit view with a depth */
    int32_t star_first;         /**< Index of the first star field of the view in the packet */
    int32_t star_count;         /**< Star fields the view draws, filled by the light extract
                                 *   for a lit view with a perspective camera with a depth */
    int32_t fog_first;          /**< Index of the first fog volume of the view in the packet */
    int32_t fog_count;          /**< Fog volumes the view draws, back to front, filled by the
                                 *   light extract for a lit view with LightSettings.fog_volumes */
    ViewDebug debug;            /**< View.debug, copied by a capture of the camera of a view;
                                 *   all zero for the other captures (probes, reflections,
                                 *   bakes, no view), which draw no debug pass */
    int32_t debug_box_first;    /**< Index of the first box of the view in the debug boxes of
                                 *   the packet */
    int32_t debug_box_count;    /**< Boxes of the spatial index trees the view draws (BvhDebug),
                                 *   copied by a capture of the camera of the view */
    int32_t debug_pose_first;   /**< Index of the first pose of the view in the debug poses of
                                 *   the packet */
    int32_t debug_pose_count;   /**< Poses the boxes of the view index, the identity first and
                                 *   then one per mover tree (BvhDebug.mover_trees), 0 without
                                 *   boxes */
    LodSettings lod;            /**< View.lod resolved by box_render_lod_settings, all zero
                                 *   (every shape at full detail) without a view */
    uint32_t features;          /**< BOX_RENDER_FEATURE_* features: those of its scene, without
                                 *   the lighting ones its LightSettings turn off, and the depth
                                 *   prepass */
    uint32_t scene_features;    /**< BOX_RENDER_FEATURE_* features of its scene, which key the
                                 *   pipelines of its batches */
    int32_t planar_first;       /**< Index of the first planar reflection of the view in the
                                 *   packet */
    int32_t planar_count;       /**< Planar reflections the view renders, nearest first, at most
                                 *   BOX_PLANAR_REFLECTION_MAX */
    uint32_t impostor;          /**< BOX_RENDER_IMPOSTOR_* channel of an impostor frame the view
                                 *   renders unlit, 0 for any other view */
} box_render_view_t;

/** Channels of an impostor frame (box_render_view_t.impostor), each its own view and atlas.
 * The shaders see the channel plus one in view.lights.z, above the 0 and 1 of an unlit and a
 * lit view (impostor_channel in mesh_direct.wgsl). */
#define BOX_RENDER_IMPOSTOR_ALBEDO (1u)
#define BOX_RENDER_IMPOSTOR_NORMAL (2u)
#define BOX_RENDER_IMPOSTOR_DEPTH (3u)

/** Channels of an impostor */
#define BOX_RENDER_IMPOSTOR_CHANNELS (3)

/** An impostor frame the targets of a packet rendered, copied into a cell of its atlas.
 *
 * The backend creates the atlas texture of the handle on its first blit, RGBA8 with mips, and
 * generates its mips after the last blit of a bake (last). */
typedef struct box_render_impostor_blit_t {
    uint32_t target;        /**< Target the frame rendered into, the size of a cell */
    uint32_t atlas;         /**< Texture handle of the atlas */
    uint32_t channel;       /**< BOX_RENDER_IMPOSTOR_* channel, the albedo is sRGB encoded */
    uint32_t atlas_size;    /**< Width and height of the atlas in pixels */
    uint32_t x;             /**< Left column of the cell in the atlas */
    uint32_t y;             /**< Top row of the cell in the atlas */
    uint32_t dilate;        /**< Texels an empty texel looks for a covered one, 0 for none */
    bool last;              /**< The last blit of the bake into this atlas */
} box_render_impostor_blit_t;

/** An impostor atlas written to a PNG file, for tests and tools */
typedef struct box_render_impostor_read_t {
    uint32_t atlas;         /**< Texture handle of the atlas */
    uint32_t path;          /**< Offset of the path in the packet data, with its terminator */
} box_render_impostor_read_t;

/** Kind of an effect pass of a view */
typedef enum box_render_effect_kind_t {
    BOX_RENDER_EFFECT_TONEMAP,  /**< Tonemap, optionally scaled by an adapted exposure first */
    BOX_RENDER_EFFECT_EXPOSURE, /**< AutoExposure on its own: scales by the adapted exposure */
    BOX_RENDER_EFFECT_BLOOM,    /**< Bloom */
    BOX_RENDER_EFFECT_FOG,      /**< HeightFog */
    BOX_RENDER_EFFECT_SMAA,     /**< Smaa */
    BOX_RENDER_EFFECT_GRADING,  /**< ColorGrading */
    BOX_RENDER_EFFECT_VIGNETTE, /**< Vignette */
    BOX_RENDER_EFFECT_CHROMATIC_ABERRATION, /**< ChromaticAberration */
    BOX_RENDER_EFFECT_DEPTH_OF_FIELD /**< DepthOfField */
} box_render_effect_kind_t;

/** Resolved settings of a color grading pass */
typedef struct box_render_grading_t {
    ColorGrading settings;      /**< Clamped settings, lut is the entity */
    uint32_t lut;               /**< Texture handle of a strip LUT of a valid shape, 0 for none */
    int32_t lut_size;           /**< Slices of the LUT, its height, 0 for none */
    bool display;               /**< A Tonemap of the view runs before it, so the input is in
                                 *   display range */
} box_render_grading_t;

/** An effect pass of a view with its settings resolved, in the order of View.effects */
typedef struct box_render_effect_t {
    box_render_effect_kind_t kind; /**< Kind of pass */
    ecs_entity_t entity;        /**< Effect entity, keys the state the pass keeps across frames */
    ecs_entity_t exposure;      /**< AutoExposure entity whose adapted exposure scales, 0 none */
    AutoExposure auto_exposure; /**< Resolved exposure settings when exposure is set */
    TonemapOperator tonemap;    /**< Operator of a tone map pass */
    Bloom bloom;                /**< Resolved settings of a bloom pass */
    HeightFog fog;              /**< Resolved settings of a fog pass */
    DepthOfField depth_of_field; /**< Resolved settings of a depth of field pass */
    Smaa smaa;                  /**< Resolved settings of an anti aliasing pass */
    box_render_grading_t grading; /**< Resolved settings of a color grading pass */
    Vignette vignette;          /**< Resolved settings of a vignette pass */
    ChromaticAberration chromatic_aberration; /**< Resolved settings of a chromatic aberration
                                               *   pass */
} box_render_effect_t;

/** Description of a BOX_RENDER_TARGET create or update: the size and kind of a surface */
typedef struct box_render_target_desc_t {
    void *layer;            /**< CAMetalLayer of a window, NULL for an offscreen texture */
    int32_t width;          /**< Width of the presented image in pixels */
    int32_t height;         /**< Height of the presented image in pixels */
    int32_t render_width;   /**< Width the views render at, after the resolution scale */
    int32_t render_height;  /**< Height the views render at, after the resolution scale */
    int32_t point_width;    /**< Width in points cameras are sized by, the width when 0 */
    int32_t point_height;   /**< Height in points cameras are sized by, the height when 0 */
    bool vsync;             /**< A window presents with vertical sync */
    float refresh;          /**< Refresh rate in Hz of the display of a window, 0 unknown */
    bool texture;           /**< Materials sample the image, so it renders before other targets */
} box_render_target_desc_t;

/** Description of a BOX_RENDER_MESH create, followed by its vertices and its indices.
 *
 * The vertices are box_vertex_t, the indices uint32_t with three per counter-clockwise
 * triangle. A mesh does not change under its handle; it is destroyed and created again.
 */
typedef struct box_render_mesh_desc_t {
    uint32_t surface_asset_count; /**< Surface asset vec4s after the indices */
    uint32_t vertex_count;  /**< Vertices after the description */
    uint32_t index_count;   /**< Indices after the vertices */
    uint32_t data;          /**< 1 when two vec4 of per vertex data per vertex (box_mesh_t.data)
                             *   follow the surface assets, uploaded as a vertex stream of its
                             *   own; 0 for a mesh without data, which uploads and binds none */
} box_render_mesh_desc_t;

/** Description of a BOX_RENDER_TEXTURE create, followed by its RGBA8 pixels row by row, or its
 * R8 pixels for a single channel texture.
 *
 * The first row is the top of the image, at v 0. Creating an existing handle replaces the
 * image; the backend generates the mip levels. A single channel texture is sampled as
 * (r, 0, 0, 1) by every layer, without sRGB decoding.
 */
typedef struct box_render_texture_desc_t {
    uint32_t width;         /**< Width in pixels */
    uint32_t height;        /**< Height in pixels */
    bool single_channel;    /**< The pixels are R8, one byte each; false for an update */
} box_render_texture_desc_t;

/** Description of a BOX_RENDER_TEXTURE update, followed by the RGBA8 pixels of its region row
 * by row.
 *
 * An update writes a region of a texture that has one mip level, such as a page of a glyph
 * atlas. When the handle has no texture of the given size, the backend first creates one that is
 * cleared to zero.
 */
typedef struct box_render_texture_region_t {
    box_render_texture_desc_t texture; /**< Width and height of the whole texture */
    uint32_t x;             /**< Left column of the region */
    uint32_t y;             /**< Top row of the region, 0 at the top of the image */
    uint32_t width;         /**< Columns of the region */
    uint32_t height;        /**< Rows of the region */
} box_render_texture_region_t;

/** Coefficients of an L2 spherical harmonic: one for band 0, three for 1 and five for 2 */
#define BOX_RENDER_SH_COEFFICIENTS (9)

/** What an update of a BOX_RENDER_VOLUME does */
typedef enum box_render_volume_op_t {
    BOX_RENDER_VOLUME_FILL = 0, /**< Fill every probe of both buffers with sh, write the mask */
    BOX_RENDER_VOLUME_MASK = 1, /**< Write the mask only, the probes keep their values */
    BOX_RENDER_VOLUME_COPY = 2, /**< Copy probe buffer from into the other one, then the mask */
    BOX_RENDER_VOLUME_RESIZE = 3 /**< Take the new grid, fill it with sh, keep the probes of both
                                  *   buffers it shares with the old grid, then the mask */
} box_render_volume_op_t;

/** Description of a BOX_RENDER_VOLUME create or update, followed by the validity mask.
 *
 * Probe (x, y, z) is at origin + (x, y, z) * spacing, stored x fastest, then y, then z. A
 * probe is L2 SH irradiance E_lm = A_l * L_lm (Ramamoorthi and Hanrahan) in Sloan's real basis,
 * evaluated as E(n) / pi in place of the hemisphere ambient.
 *
 * It has two probe buffers, so a pass reads the one the previous pass wrote. The mask has one
 * bit per probe, (probes + 31) / 32 words, set where views sample the probe. A create fills
 * every probe of both buffers with sh and writes the mask; an update does what its op says.
 */
typedef struct box_render_volume_desc_t {
    float origin[3];        /**< World position of the first probe */
    float spacing;          /**< World units between neighbouring probes */
    uint32_t size[3];       /**< Probes per axis, at least 1 */
    uint32_t probes;        /**< Probes of the grid, the product of size */
    float sh[BOX_RENDER_SH_COEFFICIENTS][4]; /**< Linear rgb irradiance coefficients, w unused */
    uint32_t op;            /**< box_render_volume_op_t of an update */
    uint32_t from;          /**< Probe buffer a BOX_RENDER_VOLUME_COPY reads, 0 or 1 */
    int32_t shift[3];       /**< Old grid index of probe 0 of a resize per axis, new + shift */
    uint32_t old_size[3];   /**< Probes per axis of the grid before a resize */
    uint32_t environment;   /**< BOX_RENDER_ENVIRONMENT whose SH a create, fill or resize fills
                             *   the probes with in place of sh once it was processed, 0 for
                             *   sh */
} box_render_volume_desc_t;

/** A probe the backend captures: six faces around its position, projected onto its SH */
typedef struct box_render_probe_capture_t {
    float position[3];      /**< World position of the probe */
    uint32_t index;         /**< Index of the probe in its volume */
} box_render_probe_capture_t;

/** No probe buffer: the captures of the first pass shade with a black ambient term */
#define BOX_RENDER_BAKE_BLACK (-1)

/** The probe captures of one pass of the volume of one scene in a packet.
 *
 * Each probe renders six faces of the batches of the scene with bake set, lit by the lights
 * of the batch, the first directional one shadowed by one cascade over the bounds, the ambient
 * term from the source buffer (black for the first pass), and is written into the target.
 *
 * The direct light of the Baked lights at the probe is added, occluded by its faces' depth;
 * texels no shape covers see the hemisphere ambient. Captures run before the targets, in packet
 * order, so a pass may read the buffer an earlier batch of the packet wrote.
 */
typedef struct box_render_bake_batch_t {
    uint32_t volume;        /**< Handle of the volume the probes are written into */
    uint32_t generation;    /**< Bake of the volume the probes belong to, reported back */
    int32_t pass;           /**< Pass of the bake, 0 bakes direct light and emission */
    int32_t source;         /**< Probe buffer the faces shade with, or BOX_RENDER_BAKE_BLACK */
    int32_t target;         /**< Probe buffer the probes are written into, 0 or 1 */
    ecs_entity_t scene;     /**< Scene root of the batches the faces draw */
    int32_t size;           /**< Edge of a face in texels */
    box_render_view_lights_t lights; /**< Lights the faces are shaded with, enabled: the Mixed
                                      *   and Baked lights of the scene over a black source, the
                                      *   Mixed ones over a source that holds the Baked ones */
    uint32_t baked;         /**< Light buffer of the Baked lights of the scene, 0 for none */
    uint32_t baked_count;   /**< Baked lights in that buffer */
    uint32_t baked_directional; /**< Directional lights at the start of that buffer */
    uint32_t environment;   /**< BOX_RENDER_ENVIRONMENT whose SH texels nothing covers see, 0
                             *   or one not processed yet for the hemisphere ambient */
    float bounds_min[3];    /**< Minimum corner of the static bounds of the scene */
    float bounds_max[3];    /**< Maximum corner of the static bounds of the scene */
    int32_t first;          /**< First probe of the batch in the probes of the packet */
    int32_t count;          /**< Probes of the batch */
} box_render_bake_batch_t;

/** Description of a BOX_RENDER_REFLECTION create: a cubemap array of a scene, 6 layers per slot,
 * with mips down to 4 texels and the SH irradiance of each slot. Recreating a handle at the same
 * resolution keeps the slots both textures have; at another resolution nothing is kept.
 */
typedef struct box_render_reflection_desc_t {
    uint32_t resolution;    /**< Face edge of a cubemap in texels */
    uint32_t cubes;         /**< Cubemap slots of the array */
} box_render_reflection_desc_t;

/** The reflection probe captures of one scene in a packet, which run after its bake batches.
 *
 * Each probe renders six 90 degree faces from its capture point as the bake captures do: the
 * batches of the scene with bake set, lit by the lights of the views, the sun shadowed by one
 * cascade over the bounds, the ambient term of the volume buffer the views show or the hemisphere.
 *
 * Faces go to mip 0 of the slot, texels nothing covered the sky radiance of the bake; mip k of
 * N gets the GGX prefiltered radiance for the perceptual roughness k / (N - 1) and the SH of the
 * slot the projection, in the same command buffer, before the probe is reported captured.
 */
typedef struct box_render_reflect_batch_t {
    uint32_t texture;       /**< Handle of the BOX_RENDER_REFLECTION the probes are written into */
    uint32_t ticket;        /**< Ticket of the first probe, the next probes count up from it */
    ecs_entity_t scene;     /**< Scene root of the batches the faces draw */
    int32_t size;           /**< Edge of a face in texels, the resolution of the texture */
    box_render_view_lights_t lights; /**< Lights and ambient light of the views of the scene */
    uint32_t volume;        /**< Probe volume the faces shade their ambient term with, 0 for the
                             *   hemisphere ambient */
    uint32_t volume_buffer; /**< Probe buffer of that volume, 0 or 1 */
    uint32_t environment;   /**< BOX_RENDER_ENVIRONMENT texels nothing covers see, its cube in
                             *   the stored faces and its SH in the projection, 0 or one not
                             *   processed yet for the hemisphere ambient */
    float bounds_min[3];    /**< Minimum corner of the static bounds of the scene */
    float bounds_max[3];    /**< Maximum corner of the static bounds of the scene */
    int32_t first;          /**< First probe of the batch in the probes of the packet, whose
                             *   index is the slot */
    int32_t count;          /**< Probes of the batch */
} box_render_reflect_batch_t;

/** Width of the transmittance LUT of an atmosphere in texels */
#define BOX_RENDER_ATMOS_TRANSMITTANCE_WIDTH (256)

/** Height of the transmittance LUT of an atmosphere in texels */
#define BOX_RENDER_ATMOS_TRANSMITTANCE_HEIGHT (64)

/** Width of the multi scattering LUT of an atmosphere in texels, over the cosine of the sun.
 * Finer than its height so the bilinear kinks do not band the twilight of planet shells */
#define BOX_RENDER_ATMOS_MULTI_WIDTH (128)

/** Height of the multi scattering LUT of an atmosphere in texels, over the altitude */
#define BOX_RENDER_ATMOS_MULTI_HEIGHT (32)

/** Width of the sky view LUT of a view in texels */
#define BOX_RENDER_ATMOS_SKY_WIDTH (192)

/** Height of the sky view LUT of a view in texels */
#define BOX_RENDER_ATMOS_SKY_HEIGHT (108)

/** Edge of the aerial perspective volume of a view in texels, its slices included */
#define BOX_RENDER_ATMOS_AERIAL_SIZE (32)

/** Width of the fog in scatter color of a view in texels: the ambient and the glow */
#define BOX_RENDER_ATMOS_FOG_WIDTH (2)

/** Width of the sky irradiance LUT of an atmosphere in texels, over the cosine of the sun */
#define BOX_RENDER_ATMOS_IRRADIANCE_WIDTH (64)

/** Height of the sky irradiance LUT of an atmosphere in texels, over the altitude */
#define BOX_RENDER_ATMOS_IRRADIANCE_HEIGHT (16)

/** Passes of the atmosphere, the order of their GPU times in box_render_gpu_stats_t */
typedef enum box_render_atmos_pass_t {
    BOX_RENDER_ATMOS_TRANSMITTANCE, /**< Transmittance LUT of a scene */
    BOX_RENDER_ATMOS_MULTI,         /**< Multi scattering LUT of a scene */
    BOX_RENDER_ATMOS_SKY,           /**< Sky view LUT of a view */
    BOX_RENDER_ATMOS_AERIAL,        /**< Aerial perspective volume of a view */
    BOX_RENDER_ATMOS_FOG,           /**< In scatter color of the HeightFog of a view */
    BOX_RENDER_ATMOS_SCENE_SKY,     /**< Sky view LUT of a scene at its ground altitude, which
                                     *   its environment is rendered from */
    BOX_RENDER_ATMOS_ENVIRONMENT,   /**< Mip 0 of the environment cubemap of a scene */
    BOX_RENDER_ATMOS_IRRADIANCE,    /**< Sky irradiance LUT of a scene, which lights the meshes
                                     *   of a PlanetAtmosphere */
    BOX_RENDER_ATMOS_PASSES         /**< Number of passes */
} box_render_atmos_pass_t;

/** Description of a BOX_RENDER_ATMOSPHERE create or update: the medium of the atmosphere of a
 * scene, which its transmittance and multi scattering LUTs are built from, as the first members
 * of Atmosphere in etc/shaders/atmosphere.wgsl.
 *
 * A create makes the LUTs, an update builds them again; either builds before the targets of the
 * first packet whose atmosphere shader exists. The bytes are the key the LUTs were built with.
 */
typedef struct box_render_atmosphere_desc_t {
    float radii[4];         /**< Bottom and top radius, Rayleigh and Mie scale height in km */
    float rayleigh[4];      /**< Rayleigh scattering in km^-1 at density 1, w unused */
    float mie_scattering[4]; /**< Tinted Mie scattering in km^-1 at density 1, w unused */
    float mie_extinction[4]; /**< Tinted Mie extinction in km^-1 at density 1, w unused */
    float ozone[4];         /**< Ozone absorption in km^-1 at the peak of its tent, w unused */
    float ground_albedo[4]; /**< Linear albedo of the ground, w unused */
} box_render_atmosphere_desc_t;

/** The atmosphere shell of a PlanetAtmosphere a view draws, resolved by the atmosphere extract
 * (atmosphere.c); its first members are the layout of Planet in planet_atmosphere.wgsl. */
typedef struct box_render_planet_atmos_t {
    box_render_atmosphere_desc_t medium; /**< The medium its LUTs were built from */
    float center[4];        /**< World position of the entity, w km per world unit */
    float sun[4];           /**< Direction toward its sun, w the Mie anisotropy; (0, -1, 0)
                             *   without a sun */
    float sun_color[4];     /**< Top of atmosphere irradiance of its sun, 0 without one, w
                             *   PlanetAtmosphere.intensity */
    uint32_t atmosphere;    /**< Handle of its BOX_RENDER_ATMOSPHERE, the LUTs */
    ecs_entity_t entity;    /**< Entity with the PlanetAtmosphere */
} box_render_planet_atmos_t;

/** The stars of a StarField a view draws, resolved by the light extract (stars.c): its values
 * clamped to their ranges and the rotation of its entity. */
typedef struct box_render_star_field_t {
    float rotation[3][4];   /**< Unit X, Y and Z axes of the world rotation of the entity, w
                             *   unused; the band is the plane of X and Z */
    int32_t count;          /**< Stars, 0 to BOX_STAR_FIELD_MAX */
    uint32_t seed;          /**< Seed of the hash */
    float brightness;       /**< Emission of the brightest stars, at least 0 */
    float falloff;          /**< Exponent of the relative brightness, at least 0 */
    float size;             /**< Radius of the brightest star in pixels, at least 0 */
    float temperature_min;  /**< Coolest star in kelvin, 1000 to 40000 */
    float temperature_max;  /**< Hottest star in kelvin, temperature_min to 40000 */
    float saturation;       /**< Saturation of the colors, 0 to 1 */
    float band;             /**< Share of the stars in the band, 0 to 1 */
    float band_width;       /**< Standard deviation of the latitude of the band in radians, at
                             *   least 0 */
    ecs_entity_t entity;    /**< Entity with the StarField */
} box_render_star_field_t;

/** Planet light slots, slot 0 is none: a mesh shape whose nearest PlanetAtmosphere at or above
 * it has a slot from 1 to BOX_RENDER_PLANET_LIGHT_MAX - 1 attenuates the light of its sun */
#define BOX_RENDER_PLANET_LIGHT_MAX (64)

/** The sun of a PlanetAtmosphere as the meshes at or below its entity are lit by it, resolved
 * by the atmosphere extract (atmosphere.c) for every valid PlanetAtmosphere with a planet light
 * slot; its first members are the layout of PlanetLight in planet_light.wgsl. */
typedef struct box_render_planet_light_t {
    float center[4];        /**< World position of the entity, w km per world unit */
    float radii[4];         /**< Bottom and top radius in km, z unused, w the scale of its
                             *   sky light, PlanetAtmosphere.intensity */
    float sun[4];           /**< Direction toward its sun, (0, -1, 0) without a sun; w the
                             *   angular radius of the sun disk in radians */
    uint32_t slot;          /**< Planet light slot the mesh instances reference, 1 to
                             *   BOX_RENDER_PLANET_LIGHT_MAX - 1 */
    uint32_t atmosphere;    /**< Handle of its BOX_RENDER_ATMOSPHERE, the LUTs */
    ecs_entity_t entity;    /**< Entity with the PlanetAtmosphere */
} box_render_planet_light_t;

/** Description of a BOX_RENDER_ATMOSPHERE_ENVIRONMENT update: the sky of the atmosphere of a
 * scene, rendered into mip 0 of the cubemap of the BOX_RENDER_ENVIRONMENT of the handle.
 *
 * The backend renders a sky view LUT of the scene at its ground altitude, then the cubemap
 * from it with the cloud layers before the environments are processed, and reports the cubemap
 * written with version.
 *
 * The request waits while the LUTs, the atmosphere shader or the processing are not ready; a
 * newer one for the handle replaces it, one whose cubemap is gone is dropped.
 */
typedef struct box_render_atmosphere_env_t {
    uint32_t atmosphere;    /**< Handle of the BOX_RENDER_ATMOSPHERE of the scene */
    int32_t version;        /**< EnvironmentStatus.version of the content */
    float altitude;         /**< Altitude of the ground in km, at least 1 m */
    float mie_anisotropy;   /**< Mie phase asymmetry g */
    float sun[4];           /**< Direction toward the sun, w unused; (0, -1, 0) without one */
    float sun_color[4];     /**< Top of atmosphere irradiance of the sun, 0 without one */
    float moon[4];          /**< Direction toward the moon, in w its illumination;
                             *   (0, -1, 0) without one */
    float moon_color[4];    /**< Top of atmosphere irradiance of the moon, 0 without one */
    float night[4];         /**< Linear night_tint times night_intensity */
    float aerial_intensity; /**< Scale of the aerial perspective of the cloud layers */
    float km_per_unit;      /**< Kilometers in a world unit, the inverse of units_per_km */
    int32_t cloud_count;    /**< Layers in clouds */
    box_render_cloud_layer_t clouds[BOX_CLOUD_LAYER_MAX]; /**< The cloud layers with a loaded
                             *   texture by ascending altitude, seen from the capture point of
                             *   the cubemap: world X and Z 0 at the sea_level of the
                             *   Atmosphere */
} box_render_atmosphere_env_t;

/** How a buffer is used, flags of box_render_buffer_desc_t */
typedef enum box_render_buffer_usage_t {
    BOX_RENDER_BUFFER_INSTANCE = 1 << 0, /**< Per instance vertex data of batches */
    BOX_RENDER_BUFFER_STORAGE = 1 << 1,  /**< Read and written by compute shaders */
    BOX_RENDER_BUFFER_UNIFORM = 1 << 2,  /**< Bound as uniforms */
    BOX_RENDER_BUFFER_COPY = 1 << 3,     /**< Source of GPU copies (box_render_copy_t), such as
                                          *   an instance page whose runs move */
    BOX_RENDER_BUFFER_KEEP = 1 << 4      /**< A create that replaces a buffer of the handle
                                          *   copies the bytes both have from the old buffer,
                                          *   which needs BOX_RENDER_BUFFER_COPY */
} box_render_buffer_usage_t;

/** The area of a ClipChildren element as shape2d.wgsl reads it from the clip buffer.
 *
 * The rows map a world position into the local XY plane of the element, where the area is a
 * rounded rectangle on the origin. A 2D instance holds its clip index in ids[0] of its shading
 * (0 for none, index 0 of the buffer is unused) and a clip the index of the clip it is nested in.
 */
typedef struct box_render_clip_t {
    float x[4];             /**< First row of the inverse world transform of the element */
    float y[4];             /**< Second row of the inverse world transform of the element */
    float area[4];          /**< Half width, half height, corner radius, index of the parent clip */
} box_render_clip_t;

/** The header of the polygon record of a Polygon2D, which fs_polygon of shape2d.wgsl reads from
 * the clip buffer, followed by its edges in chunks of BOX_RENDER_POLYGON_CHUNK_RECORDS records.
 *
 * Every vec4 of a chunk after its first is an edge, its start x, y and end x, y in the local
 * units of the polygon. The first vec4 is the bounds of the edges of the chunk, which lets a
 * fragment skip chunks too far to be nearest that its ray does not cross.
 *
 * The chunks hold BOX_RENDER_POLYGON_CHUNK_EDGES edges, the last fewer.
 */
typedef struct box_render_polygon_t {
    float bounds[4];        /**< Center x, y and half width, half height of the bounds */
    float shape[4];         /**< Edges, half the width of a stroke (0 for a filled polygon),
                             *   0, 0 */
    float unused[4];        /**< 0 */
} box_render_polygon_t;

/** Clip records of a chunk of the edges of a polygon record */
#define BOX_RENDER_POLYGON_CHUNK_RECORDS (5)

/** Edges of a chunk of a polygon record: three per clip record, but for its bounds */
#define BOX_RENDER_POLYGON_CHUNK_EDGES (3 * BOX_RENDER_POLYGON_CHUNK_RECORDS - 1)

/** Clip records a polygon record of a number of edges takes, its header included */
#define BOX_RENDER_POLYGON_RECORDS(edges) (1 + BOX_RENDER_POLYGON_CHUNK_RECORDS * \
    (((edges) + BOX_RENDER_POLYGON_CHUNK_EDGES - 1) / BOX_RENDER_POLYGON_CHUNK_EDGES))

/** The nine parts of a sliced Image (ImageSlice) as fs_slice of shape2d.wgsl reads them from
 * two consecutive records of the clip buffer, the index of the first in uv.x of the shading.
 *
 * Positions are fractions of the element from its top left, as the uv of the quad.
 */
typedef struct box_render_slice_t {
    float x[4];             /**< Column edges: 0, the end of the left corners, the start of the
                             *   right corners, 1 */
    float y[4];             /**< Row edges, down */
    float u[4];             /**< Texture u at the column edges */
    float v[4];             /**< Texture v at the row edges */
    float period[4];        /**< Lengths the tiled parts repeat at, 0 for a stretched part: top
                             *   and bottom edges along x, left and right edges along y, the
                             *   center along x and y */
    float flags[4];         /**< x 1 for a hollow center, the others 0 */
} box_render_slice_t;

/** Description of a BOX_RENDER_BUFFER create; creating an existing handle replaces it */
typedef struct box_render_buffer_desc_t {
    uint64_t size;          /**< Bytes of the buffer */
    uint32_t usage;         /**< box_render_buffer_usage_t flags */
} box_render_buffer_desc_t;

/** Transform of an instance, the hot stream of an instance page that moved instances upload.
 *
 * The three rows of the affine model matrix, the world transform with the instance scale:
 * row r is (m[0][r], m[1][r], m[2][r], m[3][r]). The shaders derive the normal matrix from it.
 */
typedef struct box_render_transform_t {
    vec4 rows[3];           /**< Rows of the model matrix, the fourth row is (0, 0, 0, 1) */
} box_render_transform_t;

/** Shading constants of a 2D shape or glyph, the shading stream of a 2D instance page, which
 * changes with its material, Shape2DShading of shape2d.wgsl.
 *
 * emissive.w, material and uv depend on what the instance draws (box_draws_shape2d,
 * box_draws_glyph), see the members. A shape without a 2D shape holds metallic and roughness in
 * material.xy and 0 in emissive.w and material.zw.
 *
 * A shadow (BoxShadow, box_shadow.c) is a rectangle whose quad is the box of its shape, moved and
 * spread for a polygon, plus 3 standard deviations per side, cut out by the shape.
 */
typedef struct box_render_shading_t {
    vec4 emissive;          /**< Linear emitted color times its strength, xyz unused by a
                             *   rectangle shadow.
                             *
                             *   w: width over height of a rectangle, negated width of a line,
                             *   index of the box_render_polygon_t in the clip buffer of a
                             *   polygon as a float, 0 for a glyph */
    vec4 material;          /**< Rectangle: x corner radius, y border width, in half its
                             *   smaller side, zw linear border color as half floats (r | g <<
                             *   16, b | a << 16). Line: xy start, zw end.
                             *
                             *   Polygon: x 0, y border width (f32 bits, local units), zw border
                             *   color. Glyph: xy distance range of its atlas page, z (f32 bits)
                             *   BOX_TEXT_BOLD_FIELD when bold, else 0, w 0.
                             *
                             *   Shadow: y negated standard deviation, w alpha its shape covers
                             *   it with; xz shadow and cutout corner radius, or polygon record
                             *   index (f32) and spread (f32 bits, local units). */
    vec4 uv;                /**< TextureTransform: u scale, v scale, u offset, v offset.
                             *
                             *   Sliced Rect: x index of its slice record (float), yzw 0.
                             *   Polygon: area of its image over its bounds. Glyph: its area on
                             *   its atlas page.
                             *
                             *   Shadow: xy center of the cutout box (rectangle) or bounds
                             *   (polygon) relative to the shadow, zw half extents of that box,
                             *   or z half the smaller side of the quad in local units. */
    uint32_t color[2];      /**< Linear base color and alpha (the opacity with its ancestors) as
                             *   unorm16x4: r | g << 16, b | a << 16 (box_draws_color_pack) */
    uint32_t ids[2];        /**< Clip index of a 2D instance (0 for none), its rank in the draw
                             *   order of its tree */
} box_render_shading_t;

/** A record of the material table (BOX_RENDER_BUFFER_MATERIALS), Material of material.wgsl: the
 * material of a prefab or of a shape that owns material components, slot 0 the default one */
typedef struct box_render_material_t {
    uint32_t color;         /**< Rgb, display encoded bytes: r | g << 8 | b << 16 */
    float metallic;         /**< PbrMaterial.metallic, clamped to 0..1 */
    float roughness;        /**< PbrMaterial.roughness, clamped to 0..1 */
    float cutoff;           /**< AlphaCutoff clamped to 0..1, 0 without one */
    float emissive[3];      /**< Linear emitted color times its strength */
    uint32_t attenuation_color; /**< Transmission.attenuation_color, display encoded bytes:
                             *   r | g << 8 | b << 16, white without Transmission */
    float uv[4];            /**< TextureTransform: u scale, v scale, u offset, v offset; the
                             *   offset plus the wrapped TextureScroll.velocity offset */
    float transmission_factor; /**< Transmission.transmission_factor, 0 without one */
    float ior;              /**< Transmission.ior, BOX_MATERIAL_IOR without one */
    float thickness_factor; /**< Transmission.thickness_factor, 0 without one or when negative */
    float attenuation_distance; /**< Transmission.attenuation_distance,
                             *   BOX_MATERIAL_ATTENUATION_DISTANCE without one */
    uint32_t clear_coat;    /**< ClearCoat strength | roughness << 16, two unorm16 clamped to
                             *   0..1, 0 without one or with a strength of 0 */
    uint32_t anisotropy;    /**< SpecularAnisotropy strength | rotation << 16, two unorm16, the
                             *   strength clamped to 0..1 and the rotation wrapped to 0..2 pi
                             *   over 2 pi, 0 without one or with a strength of 0 */
    union {                 /**< The Sheen and DiffuseTransmission of a record without Water,
                             *   the scatter of one with Water */
        struct {            /**< The Sheen and DiffuseTransmission of a record without Water */
            uint32_t sheen; /**< Sheen r | g << 8 | b << 16 | roughness << 24, the color
                             *   display encoded and the roughness a unorm8 clamped to 0..1, 0
                             *   without one or with a black color */
            uint32_t diffuse_transmission; /**< DiffuseTransmission r | g << 8 | b << 16 |
                             *   factor << 24, the color display encoded and the factor a
                             *   unorm8 clamped to 0..1, 0 without one */
        };

        struct {            /**< The scatter of a record with Water, which ignores Sheen and
                             *   DiffuseTransmission */
            uint32_t water_scatter_color; /**< Water.scatter_color, display encoded bytes:
                             *   r | g << 8 | b << 16, 0 without a scatter */
            float water_scatter; /**< Water.scatter, 0 without one or when negative or not
                             *   finite, which tells a record without scatter */
        };
    };
    union {                 /**< The DiffuseTransmission of a record without Water, the shore
                             *   waves of one with Water */
        struct {            /**< The DiffuseTransmission of a record without Water */
            float thickness; /**< DiffuseTransmission.thickness, 0 without one */
            float diffuse_blur; /**< DiffuseTransmission.blur, 0 without one or with a negative
                             *   or non finite blur */
        };

        struct {            /**< The shore waves of a record with Water, which ignores
                             *   DiffuseTransmission */
            float water_shore_phase; /**< Total render time over Water.shore_period wrapped to
                             *   0..1 (box_draws_water_shore_phase), 0 without shore waves */
            float water_shore_reach; /**< Water.shore_reach of a Water with a shore_period
                             *   above 0, 0 without shore waves or when negative or not finite,
                             *   which tells a record without them */
        };
    };
    uint32_t mapping;       /**< TextureMapping.mode | blend << 16, the blend a unorm16
                             *   clamped to 0..1 (0 when not finite); MappingUv (0) without one
                             *   or for MappingUv, whose blend is dropped */
    float mapping_size;     /**< TextureMapping.size of a projected mapping, 1 when
                             *   it is not positive and finite; 0 for MappingUv, so it matches
                             *   the record without one */
    float normal_offset[2]; /**< Wrapped TextureScroll.normal_velocity offset the normal map
                             *   adds to uv, 0 without one */
    union {                 /**< The Fresnel of a record without Water, the WaterCaustics of
                             *   one with Water */
        struct {            /**< The Fresnel of a record without Water */
            uint32_t fresnel_color[2]; /**< Fresnel.color in linear light as unorm16:
                                     *   r | g << 16, b | 0 << 16; 0 without one */
            float fresnel_strength; /**< Fresnel.strength, 0 without one or when negative or
                                     *   not finite */
            float fresnel_power;    /**< Fresnel.power, 0 without one or when negative or not
                                     *   finite */
            float fresnel_bias;     /**< Fresnel.bias, 0 without one or when not finite */
            float fresnel_opacity;  /**< Fresnel.opacity clamped to 0..1, 0 without one or when
                                     *   not finite */
        };

        struct {            /**< The WaterCaustics of a record with Water, which ignores
                             *   Fresnel */
            float caustics_offset[2]; /**< Offset of the water frame of the WaterCaustics in
                                     *   cells, the integral of the WaterFlow velocity over
                                     *   1 / scale wrapped to 0..BOX_RENDER_CAUSTICS_PERIOD, 0
                                     *   without one */
            float caustics_strength; /**< WaterCaustics.strength, 0 without one or when
                                     *   negative or not finite, which tells a record without
                                     *   caustics */
            float caustics_frequency; /**< 1 over WaterCaustics.scale, which is at least
                                     *   BOX_RENDER_CAUSTICS_MIN; 0 without one */
            float caustics_depth;   /**< WaterCaustics.depth, at least BOX_RENDER_CAUSTICS_MIN;
                                     *   0 without one */
            float caustics_phase;   /**< WaterCaustics.speed times the total render time over
                                     *   BOX_RENDER_CAUSTICS_LOOP, wrapped to 0..1
                                     *   (box_draws_water_caustics_phase); 0 without one */
        };
    };
    float water_ripple_offset[2]; /**< Wrapped Water.ripple_velocity offset of the second normal
                             *   sample, 0 without one */
    uint32_t water_shallow; /**< Water.shallow_color, display encoded bytes: r | g << 8 | b << 16,
                             *   0 without one */
    uint32_t water_deep;    /**< Water.deep_color, display encoded bytes, 0 without one */
    uint32_t water_foam;    /**< Water.foam_color, display encoded bytes, 0 without one */
    float water_depth;      /**< Water.depth, at least BOX_RENDER_WATER_MIN (the depth of a
                             *   Water that is not positive and finite); 0 without one, which
                             *   tells a record without Water */
    float water_visibility; /**< Water.visibility, at least BOX_RENDER_WATER_MIN, 0 without one */
    float water_refraction; /**< Water.refraction, 0 without one or when not finite */
    float water_foam_width; /**< Water.foam_width, 0 without one or when negative or not
                             *   finite */
    float water_foam_scale; /**< Water.foam_scale, 0 without one or when not finite */
    float water_ripple_scale; /**< Water.ripple_scale, 0 without one, when negative or not
                             *   finite */
    uint32_t water_flags;   /**< BOX_RENDER_WATER_* flags in bit 0, the set of its waves in
                             *   bits 1..7 (BOX_RENDER_WATER_WAVES_SHIFT, 0 without WaterWaves),
                             *   Water.absorption display encoded above them
                             *   (BOX_RENDER_WATER_ABSORPTION_SHIFT): r << 8 | g << 16 | b << 24;
                             *   0 without one */
    union {                 /**< The Distortion of a record without Water, the flow mapping of
                             *   the textures of one with Water */
        struct {            /**< The Distortion of a record without Water */
            float distortion_strength; /**< Distortion.strength, 0 without one or when
                                     *   negative or not finite */
            float distortion_frequency; /**< 1 over Distortion.scale, which is at least
                                     *   BOX_RENDER_DISTORTION_MIN; 0 without one */
            float distortion_phase; /**< Distortion.speed times the total render time wrapped
                                     *   to 0..1 (box_draws_distortion_phase), 0 without one */
            uint32_t distortion_flags; /**< BOX_RENDER_DISTORTION_* flags, 0 without one */
        };

        struct {            /**< The WaterFlow.textures of a record with Water, which ignores
                             *   Distortion */
            float water_flow[2]; /**< WaterFlow.velocity, world X and Z, of a Water whose
                                     *   textures flow (0 when not finite); 0 without */
            float water_flow_phase; /**< Total render time over BOX_RENDER_WATER_FLOW_PERIOD
                                     *   wrapped to 0..1 (box_draws_water_flow_phase), 0
                                     *   without */
            uint32_t water_flow_flags; /**< BOX_RENDER_WATER_FLOW_TEXTURES when the textures
                                     *   flow, 0 without */
        };
    };
    float wind_direction[2]; /**< Normalized world X and Z the Wind blows toward, of a record
                             *   with a WindSway (Scene.wind), 0 without one */
    float wind_offset[2];   /**< Wrapped world X and Z offset of the gust field, 0 without one */
    float wind_speed;       /**< Wind.speed, 0 without a WindSway or when calm */
    float wind_gust;        /**< Wind.gust clamped to 0..1, 0 without a WindSway */
    float wind_frequency;   /**< 1 over Wind.gust_scale, 0 without a WindSway */
    float wind_sway;        /**< Phase of the sway of the Wind, 0..1, 0 without a WindSway */
    float wind_bend;        /**< WindSway.bend, 0 without one or when not finite */
    float wind_height;      /**< WindSway.height, at least BOX_RENDER_WIND_MIN, 0 without one */
    float wind_base;        /**< WindSway.base, 0 without one or when not finite */
    float wind_flutter;     /**< WindSway.flutter, 0 without one, when negative or not finite */
    float wind_reach;       /**< WindSway.flutter_reach, at least BOX_RENDER_WIND_MIN, 0
                             *   without one */
    float wind_flutter_phase; /**< WindSway.flutter_frequency times the time of the wind wrapped
                             *   to 0..1, 0 without one */
    uint32_t flags;         /**< BOX_RENDER_WIND_PRESENT with a WindSway; with a VertexReveal
                             *   whose channel is 0..7 BOX_RENDER_REVEAL_PRESENT, maybe
                             *   BOX_RENDER_REVEAL_INVERT, the channel at
                             *   BOX_RENDER_REVEAL_CHANNEL_SHIFT and its softness (0 when
                             *   negative or not finite) as a half float in the high 16 bits;
                             *   0 without either */
    float reveal_threshold; /**< VertexReveal.threshold, 0 without one or when not finite */
} box_render_material_t;

/** Flag of a record with a VertexReveal (box_render_material_t.flags) */
#define BOX_RENDER_REVEAL_PRESENT (1u << 1)

/** Flag of a VertexReveal record that hides above the threshold (VertexReveal.invert) */
#define BOX_RENDER_REVEAL_INVERT (1u << 2)

/** Bit of the flags at which the data channel of a VertexReveal record starts, 3 bits */
#define BOX_RENDER_REVEAL_CHANNEL_SHIFT (3)

/** Bit of the flags at which the softness of a VertexReveal record starts, a half float */
#define BOX_RENDER_REVEAL_SOFTNESS_SHIFT (16)

/** Smallest WaterCaustics.scale and depth of a record */
#define BOX_RENDER_CAUSTICS_MIN (1e-3f)

/** Cells after which the pattern of WaterCaustics repeats, which the offset of its water frame
 * wraps at: its first layer repeats every BOX_RENDER_CAUSTICS_PERIOD cells, its second, 11 / 8
 * as fine, every 88 */
#define BOX_RENDER_CAUSTICS_PERIOD (64.0)

/** Cells (speed times seconds) after which the animation of WaterCaustics repeats: the phase is
 * that over this loop, 0..1 */
#define BOX_RENDER_CAUSTICS_LOOP (8.0)

/** Flag of a record with a WindSway (Scene.wind) */
#define BOX_RENDER_WIND_PRESENT (1u)

/** Smallest Distortion.scale of a record */
#define BOX_RENDER_DISTORTION_MIN (1e-3f)

/** Flag of a Distortion record: the material has a Distortion */
#define BOX_RENDER_DISTORTION_PRESENT (1u << 0)

/** Flag of a Distortion record without Transmission: the transmission words are those of a
 * factor of 1 without refraction or absorption, and the snapshot is sampled at its sharp
 * level */
#define BOX_RENDER_DISTORTION_SHARP (1u << 1)

/** Smallest depth and visibility of a Water record */
#define BOX_RENDER_WATER_MIN (1e-3f)

/** Flag of a Water record: it has a foam_texture, which the foam samples */
#define BOX_RENDER_WATER_FOAM_TEXTURE (1u << 0)

/** Flag of the water_flow_flags of a Water record: WaterFlow.textures, its normal map and foam
 * texture move with the flow (two phase flow mapping) */
#define BOX_RENDER_WATER_FLOW_TEXTURES (1u << 0)

/** Seconds of a cycle of the two phase flow mapping of WaterFlow.textures: each of the two
 * samples moves with the flow for this long, then resets while its weight is 0 */
#define BOX_RENDER_WATER_FLOW_PERIOD (1.0)

/** Bit of the Water record flags word at which the display encoded Water.absorption starts */
#define BOX_RENDER_WATER_ABSORPTION_SHIFT (8)

/** Bit of the Water record flags word at which the set of its waves in the wave table starts
 * (BOX_RENDER_BUFFER_WATER_WAVES), 0 for a Water without WaterWaves */
#define BOX_RENDER_WATER_WAVES_SHIFT (1)

/** Mask of the set of the waves of a Water record, after the shift */
#define BOX_RENDER_WATER_WAVES_MASK (0x7Fu)

/** Sets of the wave table, set 0 included, which means no waves: BOX_RENDER_WATER_WAVES_MASK + 1 */
#define BOX_RENDER_WATER_WAVE_SETS (128)

/** One wave of a set of the wave table, WaterWave of mesh_extended.wgsl */
typedef struct box_render_water_wave_t {
    float direction[2];     /**< Normalized direction of travel in world XZ */
    float k;                /**< Wave number 2 pi / wavelength, 0 for a disabled wave */
    float phase;            /**< Phase wrapped to 0..2 pi: the height is amplitude *
                             *   sin(k dot(direction, xz) - phase) */
    float slope;            /**< Amplitude times k, the largest slope of the wave; 0 for a
                             *   disabled wave */
    float steepness;        /**< Gerstner Q, 0..1 */
    float pad[2];           /**< Unused */
} box_render_water_wave_t;

/** A set of the wave table, the WaterWaves of one Water material, WaterWaveSet of
 * mesh_extended.wgsl */
typedef struct box_render_water_waves_t {
    box_render_water_wave_t waves[4]; /**< The waves, BOX_MATERIAL_WATER_WAVES */
    float detail;           /**< WaterWaves.detail, at least 0 */
    uint32_t count;         /**< Waves that are enabled */
    float pad[2];           /**< Unused */
} box_render_water_waves_t;

/** Seconds of a step of the field of WaterRipples */
#define BOX_RENDER_RIPPLE_STEP (1.0 / 120.0)

/** Steps of a field of WaterRipples a frame at most; a longer frame drops the rest of its time */
#define BOX_RENDER_RIPPLE_SUBSTEPS (8)

/** RippleSource entities a field of WaterRipples takes a frame at most */
#define BOX_RENDER_RIPPLE_SOURCES (64)

/** box_material_ripple_at disturbances a field of WaterRipples applies a frame at most, the
 * others wait for the next frame */
#define BOX_RENDER_RIPPLE_IMPULSES (64)

/** Largest Courant number c dt / dx of a field of WaterRipples, below the 1 / sqrt(2) bound of
 * the explicit 2D wave equation; the wave speed is clamped to it */
#define BOX_RENDER_RIPPLE_COURANT (0.5f)

/** World units the water under a RippleSource sinks per world unit it travels through the water,
 * times RippleSource.strength */
#define BOX_RENDER_RIPPLE_PUSH (0.05f)

/** Foam a RippleSource lays down at its center per world unit it travels through the water, times
 * RippleSource.foam */
#define BOX_RENDER_RIPPLE_FOAM_PUSH (2.0f)

/** Slope of the heights of a field of WaterRipples (world units per world unit) above which
 * WaterRipples.crest_foam forms foam, CREST_SLOPE of water_ripples.wgsl */
#define BOX_RENDER_RIPPLE_CREST_SLOPE (0.1f)

/** Largest share of the difference to its neighbours the foam of a cell takes per step, below the
 * 1 / 4 bound of the explicit 2D diffusion; WaterRipples.foam_spread is clamped to it */
#define BOX_RENDER_RIPPLE_FOAM_SPREAD_MAX (0.2f)

/** Relative speeds of a RippleSource below this push nothing, world units per second */
#define BOX_RENDER_RIPPLE_SPEED_MIN (1e-3f)

/** World units above or below the height of the Water entity within which a Water fragment takes
 * the normals of the field */
#define BOX_RENDER_RIPPLE_PLANE_TOLERANCE (0.5f)

/** Water shapes with a WaterFlow a field of WaterRipples takes a frame at most, the nearest to
 * the center of its window first; the others are ignored */
#define BOX_RENDER_RIPPLE_FLOWS (64)

/** World units inside the edges of the rectangle of a flowing Water over which its weight in the
 * velocity of a field of WaterRipples fades from 1 to 0 */
#define BOX_RENDER_RIPPLE_FLOW_FADE (1.0f)

/** Cells a step of a field of WaterRipples looks back along its velocity relative to the water
 * frame at most, REACH of water_ripples.wgsl */
#define BOX_RENDER_RIPPLE_FLOW_REACH (2.0f)

/** The oriented XZ rectangle of a Water shape with a WaterFlow over a field of WaterRipples,
 * FlowRect of water_ripples.wgsl. Positions and sizes are in cells from the corner of the window of
 * the frame (or in world units, for box_render_ripples_flow_at, as long as all are in the same
 * units). */
typedef struct box_render_ripple_flow_t {
    float center[2];        /**< Center of the rectangle */
    float axis[2];          /**< Its first axis in world XZ, normalized; the second is
                             *   (-axis[1], axis[0]) */
    float extents[2];       /**< Half size along its two axes */
    float velocity[2];      /**< WaterFlow velocity, world units per second (X, Z) */
} box_render_ripple_flow_t;

/** A source of a field of WaterRipples for the steps of a frame, RippleSource of
 * water_ripples.wgsl: a RippleSource (pushed every step) or a box_material_ripple_at disturbance
 * (once, on the first step). Positions are in cells from the corner of the window of the frame. */
typedef struct box_render_ripple_source_t {
    float from[2];          /**< Center at the start of the frame */
    float to[2];            /**< Center at the end of the frame; a step at time fraction t of
                             *   the frame is at mix(from, to, t) */
    float extents[2];       /**< Half size along its own X and Z, cells */
    float axis[2];          /**< Its own X axis in world XZ, normalized */
    float amount;           /**< Height added at the center per step, world units, negative
                             *   sinks the water */
    uint32_t once;          /**< 1 for a disturbance applied on the first step only */
    float foam;             /**< Foam added at the center per step, negative clears it */
    float pad;              /**< Unused */
} box_render_ripple_source_t;

/** A field of WaterRipples of a frame, simulated by the backend before the views draw.
 *
 * The window covers the cells origin .. origin + resolution - 1 of the water frame; the texel of
 * cell c is c mod resolution, so a window that moves keeps the heights of the cells it still
 * covers. */
typedef struct box_render_water_ripples_t {
    ecs_entity_t water;     /**< Entity with the Water and WaterRipples, the key of the field */
    ecs_entity_t scene;     /**< Scene root of the Water, 0 for the default scene */
    uint32_t resolution;    /**< Cells per side, a power of two */
    uint32_t steps;         /**< Steps of the frame, 0..BOX_RENDER_RIPPLE_SUBSTEPS */
    int32_t origin[2];      /**< Window origin modulo the resolution: the texel of its corner */
    int32_t shift[2];       /**< Cells the window moved since the previous frame, clamped to
                             *   -resolution..resolution; the cells that entered start flat */
    bool reset;             /**< The field starts flat: new, or a resolution that changed */
    float cell;             /**< World units of a cell */
    float courant2;         /**< (c dt / dx)^2 of the clamped wave speed */
    float keep;             /**< Factor of the vertical velocity a step keeps, from damping */
    float corner[2];        /**< World XZ of the corner of the window at the end of the frame */
    float height;           /**< World Y of the Water entity */
    float strength;         /**< WaterRipples.strength, 0 when not finite */
    float foam_spread;      /**< Share of the difference to its neighbours the foam of a cell
                             *   takes per step, from WaterRipples.foam_spread */
    float foam_keep;        /**< Factor of the foam a step keeps, from WaterRipples.foam_decay */
    float crest;            /**< Foam formed per step per unit of slope above
                             *   BOX_RENDER_RIPPLE_CREST_SLOPE, from WaterRipples.crest_foam */
    int32_t source_first;   /**< First of its sources in the ripple_sources of the packet */
    int32_t source_count;   /**< Its sources */
    float velocity[2];      /**< WaterFlow velocity of its Water that the window frame moves
                             *   with, world units per second, 0 for still water */
    int32_t flow_first;     /**< First of its flowing Water rectangles in the ripple_flows of
                             *   the packet */
    int32_t flow_count;     /**< Its flowing Water rectangles, the nearest to the center of the
                             *   window first */
    bool flowing;           /**< One of its rectangles flows relative to the frame, so the
                             *   steps advect the heights and the foam */
} box_render_water_ripples_t;

/** Bits of box_render_mesh_shading_t.material that hold the slot of the material table, the
 * planet light slot is above them */
#define BOX_RENDER_MESH_MATERIAL_BITS (24)

/** Mask of the slot of the material table in box_render_mesh_shading_t.material */
#define BOX_RENDER_MESH_MATERIAL_MASK ((1u << BOX_RENDER_MESH_MATERIAL_BITS) - 1u)

/** Shading of a mesh instance, the shading stream of a mesh instance page, MeshShading of
 * instance.wgsl.
 *
 * The tint is the composed mix of the tints of the ancestors and of the instance's own Tint
 * (box_draw_tint_t): a display encoded color becomes color * keep + add, as four unorm16.
 */
typedef struct box_render_mesh_shading_t {
    uint32_t material;      /**< Slot of the material table the instance renders with, or
                             *   with the planet light slot of the instance shifted by
                             *   BOX_RENDER_MESH_MATERIAL_BITS (box_draws_planet_material) */
    uint32_t tint[2];       /**< Tint add red | add green << 16, add blue | keep << 16 */
    float alpha;            /**< The opacity of the instance times that of its ancestors */
} box_render_mesh_shading_t;

/** Surface parameters of an instance, the stream of an instance page that changes with its
 * ShapeMesh: the parametric surface the vertex shaders evaluate (surface.wgsl).
 *
 * Only the batches of shapes with a parametric surface (box_draw_key_t.surface) have the stream,
 * in the pages from BOX_RENDER_BUFFER_FIRST_SURFACE; a plain mesh has no record, and the vertex
 * shaders leave its surface zero.
 *
 * p[0].x is the surface kind and p[22], p[23] the center and half extent of its local bounds. */
typedef struct box_render_surface_t {
    vec4 p[BOX_SURFACE_VECTORS]; /**< Surface words, as box_surface_data_t.p */
} box_render_surface_t;

/** Compute pass that copies changed instances from the change buffer into their slots of one
 * stream of an instance page.
 *
 * The segment at offset holds BOX_RENDER_SCATTER_HEADER words (count, words per instance of
 * the stream) followed by count entries of a slot index and the words of the instance.
 */
typedef struct box_render_scatter_t {
    uint32_t buffer;        /**< Handle of the stream of the instance page the slots are in, one
                             *   scatter per page, stream and frame */
    uint32_t changes;       /**< Handle of the buffer holding the segment */
    uint32_t offset;        /**< Byte offset of the segment, a BOX_RENDER_SCATTER_ALIGN multiple */
    uint32_t size;          /**< Bytes of the segment */
    uint32_t count;         /**< Instances the segment holds */
} box_render_scatter_t;

/** Placement of the instances of a mover page in the world, after the scatters: the compose
 * pass writes the transform of every slot times the transform of its mover (and the frame
 * record of the slots with BOX_RENDER_MOVER_FRAME) into the buffers the passes bind for the
 * page. */
typedef struct box_render_compose_t {
    uint32_t page;          /**< Handle of the transform stream of the mover page */
    uint32_t count;         /**< Slots placed, from slot 0 */
} box_render_compose_t;

/** Byte alignment of the offsets and sizes of a copy between buffers, the copy alignment of
 * WebGPU */
#define BOX_RENDER_COPY_ALIGN (4)

/** Bytes the copies of one packet move at most together: the backend copies those within one
 * buffer, or that read what another copy writes, through one staging buffer, below the 256 MiB
 * buffer size every WebGPU device supports */
#define BOX_RENDER_COPY_MAX ((uint64_t)128 << 20)

/** GPU copy of bytes from one buffer into another or into the same one, such as the instances
 * of a run of an instance page that moved to another run.
 *
 * The copies of a packet run after its resources and before its uploads and scatters. Every
 * copy reads its source as it was before the copies of the packet, so one copy may write where
 * another reads, and source and destination may overlap.
 *
 * Offsets and sizes are BOX_RENDER_COPY_ALIGN multiples, the sizes of a packet add up to at most
 * BOX_RENDER_COPY_MAX, and the source has BOX_RENDER_BUFFER_COPY.
 */
typedef struct box_render_copy_t {
    uint32_t src;           /**< Handle of the buffer read */
    uint32_t src_offset;    /**< Byte offset in it */
    uint32_t dst;           /**< Handle of the buffer written */
    uint32_t dst_offset;    /**< Byte offset in it */
    uint32_t size;          /**< Bytes copied */
} box_render_copy_t;

/** Copy of the heights of a tile of a Terrain into its slot of the surface assets of its grid
 * mesh, which the vertex stages of the tile read (terrain.wgsl).
 *
 * The backend copies the R32Float image of the texture into the slot when the image changed since
 * it last copied it there, after the texture graphs evaluated and before the views render. A
 * slot holds the rows of the image, each aligned to 256 bytes (box_terrain_row_bytes); the
 * assets grow to hold the highest slot, which copies every slot again.
 */
typedef struct box_render_terrain_height_t {
    uint32_t mesh;          /**< Handle of the grid mesh of the tile */
    uint32_t slot;          /**< Slot of the tile */
    uint32_t texture;       /**< Handle of the texture with the heights of the tile */
} box_render_terrain_height_t;

/** Description of a BOX_RENDER_SHADER create or update, followed by its code and its label.
 *
 * The preprocessed WGSL code and the label each end with a terminator that their size counts.
 */
typedef struct box_render_shader_desc_t {
    uint32_t code_size;     /**< Bytes of the code after the description */
    uint32_t label_size;    /**< Bytes of the label after the code */
    int32_t revision;       /**< Revision of the code, reported back with the compile result */
} box_render_shader_desc_t;

/** Outcome of the compilation of a shader module, produced on the render thread */
typedef struct box_render_shader_result_t {
    uint32_t handle;        /**< Handle of the shader */
    int32_t revision;       /**< Revision of the code that was compiled */
    char *message;          /**< Error text, NULL when the module compiled, freed by the caller */
} box_render_shader_result_t;

/** A target the frame renders into and presents, optionally captured to a PNG file */
typedef struct box_render_target_t {
    uint32_t handle;        /**< Handle of the BOX_RENDER_TARGET resource */
    ecs_entity_t view_root; /**< Parent of the views the target renders, 0 for every view */
    float clear[4];         /**< Linear color the target is cleared to before the views */
    uint32_t capture;       /**< Offset of the PNG path in the packet data */
    uint32_t capture_size;  /**< Bytes of the path with its terminator, 0 when not captured */
    bool exclusive;         /**< Renders only the views whose target is this handle */
} box_render_target_t;

/** The textures and transforms of a TextureBlend or a TileTerrain as a batch draws them, in the
 * packet data of a batch with texture_blend.
 *
 * The backend blits the textures of each map into a texture array with a slice per layer, shared
 * by every batch with the same sources, and binds it with the mask, textures[1] and transforms.
 *
 * The heights make a fourth array only with a contrast above 0 and a height on a layer. A
 * TileTerrain has tiles set, its grid as the mask and an atlas per kind in each layer. */
typedef struct box_render_texture_blend_t {
    uint32_t mask;      /**< Texture handle of the mask, 0 while it is not loaded */
    uint32_t layers[BOX_RENDER_BLEND_SLICES][BOX_RENDER_BLEND_SOURCES]; /**< Texture handles
                         *   of the albedo, ORM, normal and height of each layer, 0 for the
                         *   default; the heights are 0 with a contrast of 0 */
    float transforms[BOX_RENDER_BLEND_LAYERS][4]; /**< TextureTransform of each layer: u scale,
                         *   v scale, u offset, v offset */
    uint32_t count;     /**< Layers up to the last one with an entity, at least 1: the slices of
                         *   the arrays */
    uint32_t quality;   /**< Largest dimension of a slice, the texture quality of the surfaces */
    float contrast;     /**< TextureBlend.contrast, 0 for the linear blend without heights */
    uint32_t tiles;     /**< 1 for a TileTerrain, whose mask is its grid of kind indices */
} box_render_texture_blend_t;

/** The values of one TextureLayer as the mesh shaders read them (TextureLayer of
 * mesh_layers.wgsl), the uniform buffer of its layer entity. 128 bytes. */
typedef struct box_render_texture_layer_t {
    float color[4];         /**< Linear albedo constant (Rgb), then the amount clamped to 0..1;
                             *   0 does nothing */
    float direction[4];     /**< Unit world direction, then the facing, 0..1 */
    float coverage[4];      /**< Cavity -1..1, edge width (one over the contrast), normal 0..1
                             *   and porosity 0..1 */
    float material[4];      /**< Roughness and metallic constants (PbrMaterial), then the world
                             *   units of one repeat (TextureMapping.size) and the triplanar
                             *   blend, 0..1 */
    float transform[4];     /**< TextureTransform scale u, v and offset u, v, the offset with
                             *   the wrapped offset of its TextureScroll */
    uint32_t modes[4];      /**< TextureLayerBlend of the albedo, roughness, metallic and
                             *   occlusion */
    uint32_t flags[4];      /**< BOX_RENDER_LAYER_HAS_* and BOX_RENDER_LAYER_UV, then 0 */
    float origin[4];        /**< Wrapped offset of the world projection for the absolute
                             *   position of the local origin (box_render_origin_mapping); 0 with
                             *   BOX_RENDER_LAYER_UV */
} box_render_texture_layer_t;

/** The layers of a TextureLayers as a batch draws them, in the packet data of a batch with
 * texture_layers.
 *
 * The backend keeps a uniform buffer per layer entity, written when its values change, which
 * every batch with the layer binds, and blits the maps of the layers into one texture array:
 * slice BOX_RENDER_LAYER_MAPS * layer + map, after the heights of a TextureBlend. */
typedef struct box_render_texture_layers_t {
    ecs_entity_t entities[BOX_RENDER_LAYERS]; /**< Layer entity of each slot, which keys its
                             *   buffer, 0 for a slot that does nothing */
    box_render_texture_layer_t values[BOX_RENDER_LAYERS]; /**< Values of each slot, zero for a
                             *   slot without an entity */
    uint32_t maps[BOX_RENDER_LAYERS][BOX_RENDER_LAYER_MAPS]; /**< Texture handles of the albedo,
                             *   ORM, normal and mask of each slot, 0 for the default (white, a
                             *   flat normal) */
    uint32_t count;         /**< Slots up to the last one with an entity, at least 1 */
    uint32_t quality;       /**< Largest dimension of a slice, the texture quality */
} box_render_texture_layers_t;

/** Instanced draw of one mesh with one pipeline, drawn by the views of its scene.
 *
 * Opaque batches come first in a packet; the blended instances follow as draws of one view,
 * sorted back to front, so they draw after the opaque ones in the order of the packet. Alpha
 * tested opaque batches (cutout) are opaque batches.
 */
typedef struct box_render_batch_t {
    int32_t view;               /**< Index of the view that draws it, -1 for every view */
    ecs_entity_t scene;         /**< Scene root of the instances, 0 for the default scene */
    uint32_t pipeline;          /**< Shader handle of the pipeline */
    uint32_t blend;             /**< BlendMode of the pipeline, Opaque writes depth */
    uint32_t back_face;         /**< BackFaceMode of the pipeline, BackFaceCull culls */
    bool cutout;                /**< The pipeline alpha tests against the cutoff of the
                                 *   instances (AlphaCutoff), in the opaque pass after the
                                 *   plain opaque batches and in the shadow pass */
    bool transmissive;          /**< The instances have Transmission: a blended draw of a view
                                 *   whose pipeline does not blend nor write depth and samples
                                 *   the snapshot of the opaque image of the view */
    bool water;                 /**< The instances have Water: transmissive too, drawn with the
                                 *   water override of the extended mesh shader, which binds
                                 *   foam as its foam texture */
    bool atmosphere;            /**< Blended instances that are layers around the ground of a
                                 *   planet, such as clouds, sorted before the other blended
                                 *   instances of the view: they draw before the atmosphere
                                 *   shells, which haze them with the ground, the others after */
    bool shadow;                /**< The instances cast shadows, false for NoShadow */
    bool fixture;               /**< The instances are LightFixture shapes: the cull keeps them
                                 *   out of the tile of a light inside their world bounds. Only
                                 *   set with shadow */
    bool bake;                  /**< Static 3D instances without NoBake of a scene with LightBake or
                                 *   ReflectionBake, drawn by the captures */
    bool box_shadow;            /**< The instances are the shadows of 2D shapes (BoxShadow),
                                 *   drawn with the fs_shadow entry point of the 2D shape shader */
    bool slice;                 /**< The instances are Rects with an ImageSlice, drawn with the
                                 *   fs_slice entry point of the 2D shape shader */
    bool polygon;               /**< The instances are Polygon2D shapes or their shadows,
                                 *   drawn with the polygon vertex stage of the 2D shape shader
                                 *   and fs_polygon, or fs_shadow_polygon for the shadows */
    uint32_t features;          /**< BOX_RENDER_FEATURE_* features of the scene of the instances,
                                 *   0 for other pipelines than the mesh shaders */
    bool texture_blend;         /**< The instances have a TextureBlend: the texture_blend variant
                                 *   of the mesh pipeline samples the layers at blend_layers in
                                 *   place of the albedo, ORM and normal textures */
    bool surface;               /**< The instances are parametric surfaces, drawn with the
                                 *   surface variants of the vertex stages of the mesh and shadow
                                 *   shaders */
    bool wind;                  /**< The instances have a WindSway in a scene with Scene.wind:
                                 *   every pass draws them with the material_wind override of its
                                 *   vertex stage, which moves the vertices by the WindSway of
                                 *   their material */
    float wind_reach;           /**< Largest world distance the WindSway of an instance moves a
                                 *   vertex in the wind of the frame, which the cull pass grows
                                 *   the world box of each instance by; 0 without wind */
    float range[3];             /**< DrawRange near, far and fade of the instances, all 0 for
                                 *   none: the cull pass drops the instances out of range and
                                 *   every pass draws them with the draw_range override, which
                                 *   dithers the fades */
    uint32_t mesh;              /**< Mesh handle */
    uint32_t lods[BOX_LOD_LEVELS_MAX]; /**< Mesh handle of each level of detail of an opaque 3D
                                 *   batch, lods[0] being mesh and a level that does not reduce
                                 *   the level before having its handle; 0 past the levels the
                                 *   views of the packet use (LodSettings) and for every other
                                 *   batch, which draws at level 0 */
    uint32_t textures[BOX_RENDER_TEXTURE_SLOTS]; /**< Texture handles of the layers, 0 for none */
    uint32_t foam;              /**< Texture handle of the Water.foam_texture of a water batch,
                                 *   0 for none */
    uint32_t blend_layers;      /**< Data offset of the box_render_texture_blend_t of a batch
                                 *   with texture_blend */
    bool texture_layers;        /**< The instances have TextureLayers in a scene with
                                 *   Scene.texture_layers: the layers variants of the mesh
                                 *   pipelines apply the layers at layers over the surface */
    uint32_t layers;            /**< Data offset of the box_render_texture_layers_t of a batch
                                 *   with texture_layers */
    uint32_t wrap;              /**< TextureWrap of the samplers */
    uint32_t instances;         /**< Handle of the transform stream of the instance page
                                 *   holding the instance data; its other streams follow */
    uint32_t first_instance;    /**< First slot in the instance page, unused when listed */
    bool bounded;               /**< The instances are parametric surfaces whose local bounds
                                 *   are center and extent instead of the bounds of the mesh */
    float center[3];            /**< Center of the local bounds of every instance when bounded */
    float extent[3];            /**< Half size of the local bounds of every instance when bounded */
    uint32_t instance_count;    /**< Instances to draw */
    bool listed;                /**< The instances are the slots at slots, not a range */
    uint32_t slots;             /**< Data offset of instance_count uint32_t page slots in draw
                                 *   order */
    bool bounds;                /**< min, max and radius hold the world bounds of the instances,
                                 *   only for opaque mesh batches when a view of the packet culls
                                 *   by them (box_render_view_cull_t.bounds) */
    float min[3];               /**< Minimum corner of the world box around the world box the
                                 *   cull pass computes for each instance, padded for rounding */
    float max[3];               /**< Maximum corner of that box */
    float radius;               /**< Largest radius of the world box of an instance, the size
                                 *   the cull pass tests, padded for rounding */
    bool screened;              /**< screen holds the bounds of the instances of a blended draw
                                 *   of a view in its image plane (box_draws_screen_bounds), false
                                 *   for a draw that may cover the whole view */
    float screen[4];            /**< Minimum x and y, then maximum x and y of those bounds: of
                                 *   view space x and y over -z for a perspective camera, of view
                                 *   space x and y for an orthographic one */
} box_render_batch_t;

/** Check whether the instances of a batch have a DrawRange: a near or a far distance.
 *
 * @param b The batch
 * @return Whether the passes draw it with the draw_range override and the cull pass culls its
 * instances by their range
 */
static inline bool box_render_batch_ranged(
    const box_render_batch_t *b)
{
    return box_render_is_mesh(b->pipeline) && (b->range[0] > 0.0f || b->range[1] > 0.0f);
}

/** Components whose rendered value can transition, the index of their entry in the layout table
 * of the resolver (box_render_transition_layout) */
typedef enum box_render_transition_component_t {
    BOX_RENDER_TRANSITION_POSITION = 0,     /**< Position */
    BOX_RENDER_TRANSITION_ROTATION,         /**< Rotation */
    BOX_RENDER_TRANSITION_SCALE,            /**< Scale */
    BOX_RENDER_TRANSITION_RGB,              /**< Rgb */
    BOX_RENDER_TRANSITION_OPACITY,          /**< Opacity */
    BOX_RENDER_TRANSITION_ALPHA_CUTOFF,     /**< AlphaCutoff */
    BOX_RENDER_TRANSITION_TINT,             /**< Tint */
    BOX_RENDER_TRANSITION_PBR_MATERIAL,     /**< PbrMaterial */
    BOX_RENDER_TRANSITION_EMISSIVE,         /**< Emissive */
    BOX_RENDER_TRANSITION_TEXTURE_TRANSFORM, /**< TextureTransform */
    BOX_RENDER_TRANSITION_TRANSMISSION,     /**< Transmission */
    BOX_RENDER_TRANSITION_CLEAR_COAT,       /**< ClearCoat */
    BOX_RENDER_TRANSITION_ANISOTROPY,       /**< SpecularAnisotropy */
    BOX_RENDER_TRANSITION_SHEEN,            /**< Sheen */
    BOX_RENDER_TRANSITION_DIFFUSE_TRANSMISSION, /**< DiffuseTransmission */
    BOX_RENDER_TRANSITION_FRESNEL,          /**< Fresnel */
    BOX_RENDER_TRANSITION_DISTORTION,       /**< Distortion */
    BOX_RENDER_TRANSITION_BOX_SHADOW,       /**< BoxShadow */
    BOX_RENDER_TRANSITION_BORDER,           /**< Border */
    BOX_RENDER_TRANSITION_POINT_LIGHT,      /**< PointLight */
    BOX_RENDER_TRANSITION_SPOT_LIGHT,       /**< SpotLight */
    BOX_RENDER_TRANSITION_COMPONENTS        /**< Number of transitionable components */
} box_render_transition_component_t;

/** Bytes of the largest transitionable component (SpotLight), the size of a value in a record,
 * a multiple of 4 so a value holds floats */
#define BOX_RENDER_TRANSITION_VALUE (28)

/** Fields of the transitionable component with the most fields: BoxShadow, four floats and
 * the four bytes of its color */
#define BOX_RENDER_TRANSITION_FIELDS (8)

/** What a transition record does */
typedef enum box_render_transition_op_t {
    BOX_RENDER_TRANSITION_CREATE = 0,   /**< A node exists, with no values */
    BOX_RENDER_TRANSITION_DESTROY,      /**< A node no longer exists, its values are dropped */
    BOX_RENDER_TRANSITION_START,        /**< A component of a node has a new target */
    BOX_RENDER_TRANSITION_POSE,         /**< The rendered world matrix of a node with an active
                                         *   transform transition, for this packet */
    BOX_RENDER_TRANSITION_SLOT,         /**< A transform in the packet data that a node moves,
                                         *   patched with W_r(node) * rel (the target chain
                                         *   from the node with the instance matrix) */
    BOX_RENDER_TRANSITION_MATERIAL,     /**< A box_render_material_t of the material table in
                                         *   the packet data, packed again from the rendered
                                         *   values of the nodes that own its components */
    BOX_RENDER_TRANSITION_SHADING,      /**< A box_render_shading_t of a 2D shape or a glyph, or
                                         *   a box_render_mesh_shading_t of a mesh shape, in the
                                         *   packet data, written again the same way */
    BOX_RENDER_TRANSITION_FADE,         /**< The rendered tints and opacity of a node with an
                                         *   active Tint or Opacity transition, for this packet,
                                         *   composed from its parent node */
    BOX_RENDER_TRANSITION_SHADOW_BOX,   /**< The transform and the box_render_shading_t of the
                                         *   shadow of a 2D shape, resolved again from the
                                         *   box_draw_transition_box_shadow_t and its node */
    BOX_RENDER_TRANSITION_LIGHT         /**< A box_render_light_t of a light buffer upload in
                                         *   the packet data, written again from the
                                         *   box_render_transition_light_t and the rendered
                                         *   light and Rgb of its nodes */
} box_render_transition_op_t;

/** What a light record points at in the packet data: the target of a light whose light
 * component or Rgb transitions. The node of the record renders the PointLight or SpotLight,
 * 0 when it is at its target. */
typedef struct box_render_transition_light_t {
    uint32_t kind;          /**< 0 for a directional, 1 for a point and 2 for a spot light */
    uint32_t rgb_node;      /**< Node whose Rgb the light renders, 0 for the target Rgb */
    float values[4];        /**< Target intensity, range, inner and outer angle */
    float scale[3];         /**< Per channel factor the extract applied to the color of the
                             *   light after its Rgb and intensity: the attenuation of a sun or
                             *   moon, the square of the world scale for the others */
    float range_scale;      /**< Factor the extract applied to the range: the uniform world
                             *   scale of a point or spot light, 1 for a directional light */
    uint8_t rgb[3];         /**< Target Rgb, white without one */
    uint8_t has_rgb;        /**< The light has an Rgb */
} box_render_transition_light_t;

/** What a fade record points at in the packet data: the composed Tint and Opacity of the
 * entities between a node and its parent node, and the target Tint and Opacity of the node.
 *
 * A tint maps a display encoded color c to c * keep + add. The node composes its parent node,
 * these and its own values in that order; nothing is divided, so an opacity of 0 works. */
typedef struct box_render_transition_fade_t {
    float add[3];           /**< Composed tints from the parent node down to the parent of the
                             *   node, both excluded, or of every ancestor without a parent
                             *   node, weighted by what the later tints keep */
    float keep;             /**< Product of one minus the strength of those tints */
    float alpha;            /**< Product of the opacities of those entities */
    float opacity;          /**< Target Opacity of the node, when present */
    uint8_t tint[4];        /**< Target Tint of the node, r, g, b, a, when present */
    uint32_t present;       /**< 1 << BOX_RENDER_TRANSITION_TINT and
                             *   1 << BOX_RENDER_TRANSITION_OPACITY when the node has them */
} box_render_transition_fade_t;

/** What a pose record points at in the packet data: the target chain of a node and the target
 * pose its own components fall back to when they do not transition.
 *
 * The rendered world matrix of the node is W_r(parent) * rel * L_r, or rel * L_r without a
 * parent node, where L_r is the local matrix (box_transform_local) of the rendered position,
 * rotation and scale. */
typedef struct box_render_transition_pose_t {
    float rel[4][4];        /**< Column major: the target locals from the parent node down to
                             *   the parent of the node, or without a parent node the target
                             *   world matrix of its parent (identity for a root) */
    float position[3];      /**< Target Position */
    float rotation[3];      /**< Target Rotation, 0 without one */
    float scale[3];         /**< Target Scale, 1 without one */
} box_render_transition_pose_t;

/** A change the game thread found for the transition resolver of the render thread.
 *
 * A start without a previous value of the component, or with a duration and delay of 0, sets
 * the value without a transition; any other start transitions from the value the node renders
 * at the start time to the target. The records of a packet apply in the order they were made.
 *
 * Pose and slot records come every frame a node moves, parent nodes first; fade records every
 * frame a Tint or Opacity runs, parent nodes first; material, shading and box shadow records
 * every frame they run, one per record, box shadows after the poses they read; light records
 * every frame a light transitions, one per light of an uploaded light buffer.
 */
typedef struct box_render_transition_record_t {
    box_render_transition_op_t op;  /**< What the record does */
    uint32_t node;                  /**< Node id, from 1 */
    uint32_t parent;                /**< Parent node of a pose or a fade, or the node that
                                     *   moves the shape of a box shadow, 0 for none */
    uint32_t data;                  /**< Packet data offset of the pose of a pose, the rel
                                     *   matrix of a slot, the box_render_transition_fade_t of a
                                     *   fade, the box_render_transition_light_t of a light,
                                     *   else the box_draw_transition_*_t of its op */
    uint32_t offset;                /**< Packet data offset of the box_render_transform_t a
                                     *   slot patches, or of the record a material or shading
                                     *   record patches, the shading of a box shadow, the
                                     *   box_render_light_t of a light */
    box_render_transition_component_t component; /**< Component of a start */
    TimingFunction timing_function; /**< Curve of a start */
    TransitionBezier bezier;        /**< Handles of a CubicBezier start, zero for the others */
    float delay;                    /**< Seconds the value holds still after the start */
    float duration;                 /**< Seconds from the end of the delay to the target */
    double start;                   /**< World time of the start, the time of its packet */
    uint8_t value[BOX_RENDER_TRANSITION_VALUE]; /**< Target in the layout of the component,
                                     *   padding zeroed */
} box_render_transition_record_t;

/** Everything the renderer needs of one frame, filled on the main thread.
 *
 * The vectors keep their storage when the packet is reused, so a frame that needs no more
 * than an earlier one does not allocate. Resources apply first, then the copies, then uploads,
 * then the scatters, then every target is cleared, drawn by its views and presented.
 */
typedef struct box_render_packet_t {
    int64_t frame;          /**< Frame the packet was extracted in */
    double time;            /**< World time at the end of the frame, in seconds */
    double origin[3];       /**< Absolute position of the local origin (WorldOrigin) of the
                             *   frame, which the world space patterns of the shaders add */
    float delta;            /**< Seconds the frame advanced the world */
    float fps;              /**< Frame rate cap of the frame, box_app_fps, 0 uncapped */
    bool headless;          /**< App.headless: the frame waits for the pipelines it draws
                             *   with, so its output does not depend on how long they take */
    bool vsync;             /**< A window target of the frame presents with vsync */
    float refresh;          /**< Refresh rate in Hz of the display of that window, 0 unknown */
    uint32_t anisotropy;    /**< Anisotropic samples of the texture samplers, 1 to 16 */
    float transmission_roughness; /**< Largest roughness of the transmissive materials, which
                             *   bounds the mips of the opaque snapshot that render */
    int64_t texture_graph_budget; /**< Bytes the cache of the texture graph values holds at
                             *   most (TextureGraphCache.budget), 0 for the default */
    ecs_vec_t resources;    /**< box_render_resource_t in the order they were made */
    ecs_vec_t uploads;      /**< box_render_upload_t in the order they were made */
    ecs_vec_t scatters;     /**< box_render_scatter_t, run after the uploads */
    ecs_vec_t composes;     /**< box_render_compose_t, run after the scatters */
    ecs_vec_t copies;       /**< box_render_copy_t, run after the resources and before the
                             *   uploads */
    ecs_vec_t views;        /**< box_render_view_t in render order */
    ecs_vec_t batches;      /**< box_render_batch_t */
    ecs_vec_t targets;      /**< box_render_target_t in the order they render */
    ecs_vec_t effects;      /**< box_render_effect_t, the effect passes of the views */
    ecs_vec_t bakes;        /**< box_render_bake_batch_t, the probe captures of the frame */
    ecs_vec_t probes;       /**< box_render_probe_capture_t of the bake and reflection batches */
    ecs_vec_t reflections;  /**< box_render_reflect_batch_t, the reflection captures of the frame */
    ecs_vec_t particles;    /**< box_render_particle_pool_t, simulated after the scatters */
    ecs_vec_t shadow_atlases; /**< box_render_shadow_atlas_t, the atlases with tiles */
    ecs_vec_t shadow_tiles; /**< box_render_shadow_tile_t of those atlases */
    ecs_vec_t fog_volumes;  /**< box_render_fog_volume_t, the fog volumes of the views */
    ecs_vec_t planet_atmospheres; /**< box_render_planet_atmos_t, the planet atmosphere shells
                             *   of the views */
    ecs_vec_t star_fields;  /**< box_render_star_field_t, the star fields of the views */
    ecs_vec_t planet_lights; /**< box_render_planet_light_t, the planets whose sun the mesh
                             *   instances with a planet light slot are lit by */
    ecs_vec_t planar_reflections; /**< box_render_planar_t, the planar reflections of the
                             *   views */
    ecs_vec_t water_ripples; /**< box_render_water_ripples_t, the fields of WaterRipples,
                             *   stepped before the targets */
    ecs_vec_t ripple_sources; /**< box_render_ripple_source_t of those fields */
    ecs_vec_t ripple_flows; /**< box_render_ripple_flow_t of those fields */
    ecs_vec_t impostor_blits; /**< box_render_impostor_blit_t, the impostor frames copied into
                             *   their atlases after the targets rendered */
    ecs_vec_t impostor_reads; /**< box_render_impostor_read_t, impostor atlases written to PNG
                             *   files after the blits */
    ecs_vec_t terrain_heights; /**< box_render_terrain_height_t, the heights of the terrain
                             *   tiles that draw, copied after the texture graphs evaluated */
    ecs_vec_t debug_boxes;  /**< box_render_debug_box_t, the boxes of the spatial index trees
                             *   of the views */
    ecs_vec_t debug_poses;  /**< mat4, the matrices that place the boxes of the views */
    ecs_vec_t transitions;  /**< box_render_transition_record_t in the order they were made,
                             *   empty in a world without Transition */
    ecs_vec_t data;         /**< Bytes the resources and uploads point into */
    int32_t generation;     /**< Backend generation the extract built it for, the ring drops a
                             *   packet of an older generation instead of rendering it */
    int32_t data_idle;      /**< Resets in a row whose frame used little of a data block above
                             *   BOX_RENDER_DATA_KEEP, see BOX_RENDER_DATA_TRIM_RESETS */
    int32_t data_peak;      /**< Largest data of those frames, the size the block shrinks to */
} box_render_packet_t;

/** A renderer: the only code that receives packets, always called from one thread.
 *
 * init runs before the first packet, render once per packet in frame order and fini after
 * the last; all three run on the render thread, or on the main thread when it renders.
 */
typedef struct box_render_backend_t {
    void *ctx;                  /**< State of the backend, passed to every callback */
    int (*init)(void*);         /**< Create the backend state, nonzero drops every packet */
    void (*render)(void*, const box_render_packet_t*); /**< Render a packet, valid until return */
    void (*fini)(void*);        /**< Release what init created */
    void (*free)(void*);        /**< Free ctx on the main thread, NULL when the caller owns it */
    void (*flush)(void*);       /**< Block until asynchronous work such as captures finished,
                                 *   on the render thread, NULL when there is none */
    bool (*lost)(void*);        /**< Check before a packet whether the device was lost, so the
                                 *   ring restarts the backend, NULL when it cannot tell */
    int32_t generation;         /**< Set by the ring, bumped at fini and on a new backend, as
                                 *   init runs after or during the next extract; a change makes
                                 *   the extract send everything again */
} box_render_backend_t;

/** Backend that renders nothing and records what it received, the default backend */
typedef struct box_render_null_t {
    int32_t inits;              /**< Calls to init */
    int32_t finis;              /**< Calls to fini */
    int32_t packets;            /**< Packets received */
    int32_t gaps;               /**< Packets whose frame did not follow the previous one */
    int32_t wrong_thread;       /**< Calls on another thread than the one that ran init */
    ecs_os_thread_id_t thread;  /**< Thread that ran init */
    int64_t frame;              /**< Frame of the last packet */
    double time;                /**< Time of the last packet */
    float delta;                /**< Delta of the last packet */
    int64_t views;              /**< Views received in total */
    int64_t batches;            /**< Batches received in total */
    int64_t uploads;            /**< Uploads received in total */
    int64_t scatters;           /**< Scatters received in total */
    int64_t copies;             /**< Copies between buffers received in total */
    int64_t copy_bytes;         /**< Bytes those copies move in total */
    int64_t kept;               /**< Buffer creates received in total that keep the contents of
                                 *   the buffer they replace (BOX_RENDER_BUFFER_KEEP) */
    int64_t resources;          /**< Resource changes received in total */
    int64_t bytes;              /**< Upload bytes received in total */
    int64_t targets;            /**< Targets received in total */
    int64_t transitions;        /**< Transition records received in total */
    int64_t texture_graphs;     /**< Texture graph creates received in total */
    int64_t texture_graph_destroys; /**< Texture graph destroys received in total */
    int64_t texture_node_kinds; /**< Texture node kind creates received in total */
    ecs_map_t graphs;           /**< Texture handle to a copy of the description of the last
                                 *   graph created for it and not destroyed, see
                                 *   box_render_null_texture_graph */
    ecs_map_t node_kinds;       /**< Node kind id to a copy of its last description */
    box_render_packet_t last;   /**< Copy of the last packet */
} box_render_null_t;

/* Test only access to the transition resolver.
 *
 * The in-progress value of a transition exists only on the render thread and only rendered
 * pixels show it: no public header, component, stat or shell command returns it. These exist so
 * that tests can check the values the resolver computes without a GPU, with the null backend.
 * Call them on the main thread after box_render_sync, while no packet is being rendered. */

/** Get the value the transition resolver computed for a component of an entity in the last
 * packet it resolved, for tests.
 *
 * @param world The world
 * @param entity The entity, which has Transition
 * @param component The component id, one that box_transition_supported accepts
 * @param out Receives the value in the layout of the component, the size of the component
 * @return Whether the entity has a node and the node has a value of the component
 */
bool box_render_transition_test_value(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    void *out);

/** Get the node id the extract gave an entity, for tests.
 *
 * @param world The world
 * @param entity The entity
 * @return The node id, 0 when the entity has no node
 */
uint32_t box_render_transition_test_node(
    ecs_world_t *world,
    ecs_entity_t entity);

/** Get the number of nodes with a running transition in the transition resolver, for tests.
 *
 * @param world The world
 * @return Nodes the resolver evaluates per packet
 */
int32_t box_render_transition_test_active(
    ecs_world_t *world);

#endif
