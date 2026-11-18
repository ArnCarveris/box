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

/** Entries of the blend group that box_gpu_texture_init makes blend_layout of.
 *
 * The fragment stage of fs_blend samples 15 of the 16 textures WebGPU guarantees: 9 of the view
 * group (gpu_draw.c), the emissive layer, the 4 arrays and the mask. */
#define BOX_GPU_BLEND_ENTRIES (9)

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

/** BlendUniforms of mesh.wgsl */
typedef struct box_gpu_blend_uniforms_t {
    float transforms[BOX_RENDER_BLEND_LAYERS][4]; /**< TextureTransform of each layer */
    uint32_t count;     /**< Slices of the arrays */
    uint32_t flags;     /**< BOX_GPU_BLEND_MASKED, BOX_GPU_BLEND_SINGLE, BOX_GPU_BLEND_HEIGHT */
    float contrast;     /**< TextureBlend.contrast, 0 samples no heights */
    uint32_t pad;       /**< Unused, zero */
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

/** Make the bind groups, arrays and buffers of the TextureLayers batches of a packet, and
 * release the groups and buffers no batch of the packet draws with.
 *
 * @param gpu The backend
 * @param packet The packet
 */
void box_gpu_layers_prepare(
    box_gpu_t *gpu,
    const box_render_packet_t *packet);

/** Release the layers groups and the layer buffers.
 *
 * @param gpu The backend
 */
void box_gpu_layers_clear(
    box_gpu_t *gpu);

#endif

#endif
