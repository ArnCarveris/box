/**
 * @file render/gpu_texture_blend.h
 * @brief Internals of the texture arrays and bind groups of the TextureBlend and TextureLayers
 * batches of the webgpu backend, shared by gpu_texture.c and gpu_texture_layers.c.
 */

#ifndef BOX_RENDER_GPU_TEXTURE_BLEND_H
#define BOX_RENDER_GPU_TEXTURE_BLEND_H

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bindings of the blend group (blend_layout), fs_blend of mesh.wgsl: the emissive layer and the
 * sampler at the bindings of the plain group, then the arrays, the mask, the uniforms and the
 * heights */
#define BOX_GPU_BLEND_EMISSIVE (1)
#define BOX_GPU_BLEND_SAMPLER (4)
#define BOX_GPU_BLEND_ARRAYS (5)
#define BOX_GPU_BLEND_MASK (8)
#define BOX_GPU_BLEND_UNIFORMS (9)
#define BOX_GPU_BLEND_HEIGHTS (10)

/** Binding of the bake atlas of the grid mesh of a terrain blend (terrain_bake of
 * mesh_blend.wgsl), a one texel placeholder for the other blends */
#define BOX_GPU_BLEND_BAKE (18)

/** Entries of the blend group that box_gpu_texture_init makes blend_layout of.
 *
 * The fragment stage of fs_blend samples 16 textures: 9 of the view group (gpu_draw.c), the
 * emissive layer, the 4 arrays, the mask and the bake atlas, which the device allows as it
 * requests the sampled textures the adapter offers. */
#define BOX_GPU_BLEND_ENTRIES (10)

/** BlendUniforms flag: the mask is loaded, else layer 0 shows alone */
#define BOX_GPU_BLEND_MASKED (1u)

/** BlendUniforms flag: the mask has a single channel, which weighs layers 0 and 1 by 1 - r, r */
#define BOX_GPU_BLEND_SINGLE (2u)

/** BlendUniforms flag of layer 0, shifted by the layer for the others: the layer has a height
 * image, else it is flat at 0.5 */
#define BOX_GPU_BLEND_HEIGHT (4u)

/** BlendUniforms flag: a TileTerrain, whose mask is its grid of kind indices and whose slices
 * are the atlases of its kinds */
#define BOX_GPU_BLEND_TILES (256u)

/** BlendUniforms flag: a Terrain with masks, whose mask is the mask atlas of its grid mesh,
 * read at the cell of the tile of a fragment (terrain_mask of mesh_blend.wgsl) */
#define BOX_GPU_BLEND_TERRAIN (512u)

/** BlendUniforms of mesh.wgsl */
typedef struct box_gpu_blend_uniforms_t {
    float transforms[BOX_RENDER_BLEND_LAYERS][4]; /**< TextureTransform of each layer */
    uint32_t count;     /**< Slices of the arrays */
    uint32_t flags;     /**< BOX_GPU_BLEND_MASKED, BOX_GPU_BLEND_SINGLE, BOX_GPU_BLEND_HEIGHT */
    float contrast;     /**< TextureBlend.contrast, 0 samples no heights */
    uint32_t terrain;   /**< BOX_GPU_BLEND_TERRAIN: texels along an edge of the masks of a tile
                         *   | cells across the mask atlas << 16, else 0 */
    uint32_t bake;      /**< BOX_GPU_BLEND_TERRAIN: texels along an edge of the bake of a tile
                         *   | cells across the bake atlas << 16, 0 without a bake atlas */
    float variation;    /**< TextureBlend.variation, 0 for none */
    uint32_t pad[2];    /**< Zero */
    box_render_grass_far_t grass; /**< BOX_GPU_BLEND_TERRAIN: the far field of the Grass on the
                         *   terrain (box_render_grass_far_of), written every packet, all zero
                         *   without one */
} box_gpu_blend_uniforms_t;

/** Queue the textures a new bind group binds in a color layer whose levels average stored values.
 *
 * @param gpu The backend
 * @param textures Texture handles of the layers, 0 for the default
 */
void box_gpu_material_mips(
    box_gpu_t *gpu,
    const uint32_t *textures);

/** Get the view a layer of a material group binds: the texture of its handle or the default.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 for the default
 * @param slot The layer, BOX_RENDER_TEXTURE_SLOTS for the foam texture
 * @return The view
 */
WGPUTextureView box_gpu_material_view(
    box_gpu_t *gpu,
    uint32_t handle,
    int32_t slot);

/** Get a texture a blend samples: an image of the backend, not the image of a target.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 for none
 * @return The texture, NULL for none
 */
box_gpu_texture_t* box_gpu_blend_source(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get the view of the mask a blend group binds: the mask atlas of the grid mesh of a terrain
 * blend, the image of the mask, or white.
 *
 * @param gpu The backend
 * @param blend The blend record
 * @return The view
 */
WGPUTextureView box_gpu_blend_mask_view(
    box_gpu_t *gpu,
    const box_render_texture_blend_t *blend);

/** Get the serial of the image a blend samples through a handle.
 *
 * @param gpu The backend
 * @param handle Texture handle, 0 for none
 * @return The serial, 0 for none
 */
uint32_t box_gpu_blend_serial(
    box_gpu_t *gpu,
    uint32_t handle);

/** Get a texture array of the blends and layers, created on first use and built again when an
 * image of one of its sources changed.
 *
 * @param gpu The backend
 * @param map BOX_RENDER_BLEND_ALBEDO, _ORM, _NORMAL, _HEIGHT or BOX_GPU_LAYER_MAP
 * @param in Texture handle of each slice, 0 for the default; a target is the default
 * @param count Slices, 1 to BOX_GPU_ARRAY_SLICES
 * @param quality Largest dimension of a slice
 * @param flat Slices whose default is a flat normal, bit per slice
 * @return The array
 */
box_gpu_blend_array_t* box_gpu_array_get(
    box_gpu_t *gpu,
    uint32_t map,
    const uint32_t *in,
    uint32_t count,
    uint32_t quality,
    uint32_t flat);

/** Get the blend group of a TextureBlend batch, created on first use and checked once per packet.
 *
 * @param gpu The backend
 * @param packet The packet of the batch
 * @param b The batch, box_gpu_texture_blends
 * @return The blend group
 */
box_gpu_blend_group_t* box_gpu_blend_group_get(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b);

/** Get where the group the batch at an index of the last packets drew with is remembered, so a
 * batch that did not change finds its group without a search.
 *
 * @param hints The groups by batch index, grown to the batches of the packet
 * @param packet The packet
 * @param b The batch
 * @return The slot of the batch, NULL when the batch is not one of the packet
 */
void** box_gpu_batch_hint(
    ecs_vec_t *hints,
    const box_render_packet_t *packet,
    const box_render_batch_t *b);

/** Get the bind group of a TextureLayers batch, created on first use, and write the buffers of
 * its layers whose values changed. Once per packet, when a texture changed since it last did
 * or its blend group or buffers differ, it checks its array, the blend group of a blend batch
 * and the images of a plain one, and binds them again when one changed.
 *
 * The array holds the albedo, ORM, normal and mask of each layer, after the heights of the
 * layers of a TextureBlend, whose blend group lends the rest of its entries.
 *
 * @param gpu The backend
 * @param packet The packet of the batch
 * @param b The batch, box_gpu_layered
 * @return The bind group
 */
WGPUBindGroup box_gpu_layer_group(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_render_batch_t *b);

/** Release the layers groups and layer buffers no batch of the packet drew with.
 *
 * @param gpu The backend
 */
void box_gpu_layers_sweep(
    box_gpu_t *gpu);

/** Release the layers groups and the layer buffers.
 *
 * @param gpu The backend
 */
void box_gpu_layers_clear(
    box_gpu_t *gpu);

#endif

#endif
