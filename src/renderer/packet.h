/**
 * @file renderer/packet.h
 * @brief Frame packet exchanged between the main thread and the renderer.
 *
 * The packet is the only data the renderer sees; it never touches ECS state.
 */

#ifndef BOX_RENDERER_PACKET_H
#define BOX_RENDERER_PACKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vec.h"

/** View index that names no view, the capture then reads the surface */
#define BOX_VIEW_NONE (0xFFFFFFFFu)

/** Longest capture path stored in a packet */
#define BOX_CAPTURE_PATH_MAX (512)

/** Floats per vertex: position, normal, uv and the packed bounds local position */
#define BOX_VERTEX_FLOATS (9)

/** Bytes from a vertex to its packed local position, a Unorm10_10_10_2 value */
#define BOX_VERTEX_LOCAL_OFFSET (32)

/** Material layers a scene can register, one bit each in the instance layer mask */
#define BOX_LAYER_MAX (16)

/** Render layers a scene can create, one bit each in the view and instance masks */
#define BOX_VIEW_LAYER_BITS (32)

/** Layer mask of a view that lists no layers, which shows every layer */
#define BOX_VIEW_LAYER_ALL (0xFFFFFFFFu)

/** Point and spot lights a packet can carry */
#define BOX_LIGHT_MAX (4096)

/** Light type of an omnidirectional light */
#define BOX_LIGHT_POINT (0)

/** Light type of a cone light */
#define BOX_LIGHT_SPOT (1)

/** Texture size classes, each a 2D array texture of one edge length */
#define BOX_TEXTURE_CLASSES (4)

/** Edge length of the smallest texture size class */
#define BOX_TEXTURE_MIN_SIZE (256)

/** Edge length of the largest texture size class */
#define BOX_TEXTURE_MAX_SIZE (BOX_TEXTURE_MIN_SIZE << (BOX_TEXTURE_CLASSES - 1))

/** Texture slots a material binds: albedo, emissive, roughness and normal */
#define BOX_TEXTURE_SLOTS (4)

/** Blend equations a material can pick, the values of the BlendMode component */
#define BOX_BLEND_COUNT (9)

/** No blending, a draw that only left the opaque batches for its back faces */
#define BOX_BLEND_OPAQUE (0)

/** Source over with a straight alpha, the mode a translucent Opaque material gets */
#define BOX_BLEND_ALPHA (1)

/** Source over with a color that already holds its alpha */
#define BOX_BLEND_PREMULTIPLIED (2)

/** Back face modes, the values of the BackFaceMode component */
#define BOX_BACK_TEXTURED (0)

/** Back faces show the material constants without textures */
#define BOX_BACK_PLAIN (1)

/** Back faces are culled, the mode of an instance without the component */
#define BOX_BACK_CULL (2)

/** Bit the blend mode of an instance starts at in its mode word */
#define BOX_MODE_BLEND_SHIFT (8)

/** Bit the back face mode of an instance starts at in its mode word */
#define BOX_MODE_BACK_SHIFT (12)

/** Bit the material layer mask of an instance starts at in its mode word */
#define BOX_MODE_LAYER_SHIFT (16)

/** Layers a size class can hold, the packed instance slot is sixteen bits wide */
#define BOX_TEXTURE_LAYER_MAX (0x3FFF - 1)

/** Per instance data as laid out in GPU instance buffers.
 *
 * The culling passes read the trailing bounding sphere out of the same buffer,
 * so the fields before it keep their offsets and BOX_INSTANCE_WORDS spells the
 * stride out for the shaders.
 */
typedef struct box_instance_t {
    float transform[12];   /**< Rows of the 3x4 world matrix, each x y z w */
    float color[4];        /**< Linear albedo rgb and opacity */
    float emissive[4];     /**< Linear emissive rgb, w is the emission strength */
    float params[4];       /**< Metallic, roughness, baked, unused */
    uint32_t textures[BOX_TEXTURE_SLOTS / 2]; /**< Two packed texture slots per word */
    float uv_transform[4]; /**< Uv scale x, scale y, offset x and offset y */
    uint32_t uv_mode;      /**< Tiling, wrap, blend, back face mode and layer mask */
    float bounds[4];       /**< World space bounding sphere: center xyz and radius */
} box_instance_t;

/** 32 bit words one instance takes in a GPU instance buffer */
#define BOX_INSTANCE_WORDS (35)

/** Word the bounding sphere of an instance starts at */
#define BOX_INSTANCE_BOUNDS (31)

/** One point or spot light, laid out as the GPU light storage buffer expects it */
typedef struct box_light_t {
    float position[3];   /**< World space position */
    float range;         /**< Distance at which the light reaches zero */
    float color[3];      /**< Linear color */
    float intensity;     /**< Radiance scale */
    float direction[3];  /**< Direction a spot light points in */
    float cos_inner;     /**< Cosine of the full intensity cone half angle */
    float cos_outer;     /**< Cosine of the zero intensity cone half angle */
    uint32_t type;       /**< BOX_LIGHT_POINT or BOX_LIGHT_SPOT */
    uint32_t is_static;  /**< Light and its ancestors have no dynamic tag */
    float pad;           /**< Padding to the 16 byte GPU stride */
} box_light_t;

/** One emissive static instance, treated as a sphere light by the bake */
typedef struct box_emitter_t {
    float position[3];  /**< World space center of the instance bounds */
    float radius;       /**< Radius of the sphere the instance is treated as */
    float color[3];     /**< Linear emissive color times its strength */
    float range;        /**< Distance at which the emitted light reaches zero */
} box_emitter_t;

/** Settings of the baked lighting pass, copied from the BakeSettings singleton */
typedef struct box_bake_t {
    uint32_t enabled;   /**< Static instances read baked probes instead of the static lights */
    uint32_t budget;    /**< Probes rebaked per frame */
    float spacing;      /**< World units between two probes of an instance */
    uint32_t probe_max; /**< Probes an instance gets on one axis */
    uint32_t x;         /**< Occupancy voxels across the scene */
    uint32_t y;         /**< Occupancy voxels up the scene */
    uint32_t z;         /**< Occupancy voxels through the scene */
    uint32_t steps;     /**< Ray march samples per visibility test */
} box_bake_t;

/** Froxel grid of a view, copied from the ClusterSettings singleton */
typedef struct box_cluster_t {
    uint32_t x;           /**< Clusters across the view */
    uint32_t y;           /**< Clusters down the view */
    uint32_t z;           /**< Depth slices */
    uint32_t max_lights;  /**< Lights stored per cluster */
    float near_;          /**< View depth of the first slice boundary */
    float far_;           /**< View depth of the last slice boundary */
} box_cluster_t;

/** Camera, target and lighting of one view.
 *
 * The fields up to and including the padding are the view uniform the shaders
 * read and must keep their layout; the fields after it stay on the CPU and tell
 * the renderer where the view draws and which slice of the packet arrays is
 * its own.
 */
typedef struct box_view_t {
    float view[16];         /**< World to view matrix */
    float proj[16];         /**< View to clip matrix */
    float eye[3];           /**< Camera position */
    float env_intensity;    /**< Radiance scale of the environment colors */
    float sun_dir[3];       /**< Direction the sun light travels */
    float sun_intensity;    /**< Sun intensity */
    float sun_color[3];     /**< Sun color */
    float gamma_encode;     /**< 1 when the target is not an sRGB format */
    float env_sky[4];       /**< Linear color of the upper hemisphere, w unused */
    float env_ground[4];    /**< Linear color of the lower hemisphere, w unused */
    box_cluster_t cluster;  /**< Froxel grid the lights are assigned to */
    uint32_t light_count;   /**< Lights of the view in the packet light array */
    uint32_t env_generation; /**< Changes when the extracted environment changes */
    uint32_t layers_enabled; /**< The view applies the registered material layers */
    uint32_t pad[3];        /**< Padding to the 16 byte GPU stride */
    uint32_t target;        /**< Render target handle, 0 draws into the surface */
    uint32_t width;         /**< Target width in pixels */
    uint32_t height;        /**< Target height in pixels */
    float clear[4];         /**< Clear color of the target, linear RGBA */
    uint32_t light_first;   /**< First light of the view in the packet light array */
    uint32_t transparent_first; /**< First sorted draw of the view in the packet */
    uint32_t transparent_count; /**< Sorted draws of the view */
    uint32_t shadows;       /**< The view casts and receives shadows */
    uint32_t effects;       /**< The view runs its post process effects */
    int32_t priority;       /**< Order in the frame, lower renders first */
    uint32_t layer_mask;    /**< Layers the view shows, BOX_VIEW_LAYER_ALL for every layer */
    uint32_t caster_mask;   /**< Layers that cast its shadows, BOX_VIEW_LAYER_ALL for all */
    uint32_t dynamic_first; /**< First dynamic batch of the view in the packet */
    uint32_t dynamic_count; /**< Dynamic batches of the view */
    uint32_t cull_enabled;  /**< Instances outside the frustum of the view are not drawn */
    uint32_t cull_threshold; /**< Static instances a batch needs before a compute pass culls it */
    uint32_t cpu_pad[1];    /**< Padding to the 16 byte stride */
} box_view_t;

/** Bytes of a view that the view uniform buffer holds */
#define BOX_VIEW_UNIFORM_SIZE (offsetof(box_view_t, target))

/** Render target to create or resize, the renderer owns the texture */
typedef struct box_target_cmd_t {
    uint32_t handle;  /**< Target handle */
    uint32_t width;   /**< Width in pixels */
    uint32_t height;  /**< Height in pixels */
} box_target_cmd_t;

/** Mesh to upload, the renderer takes ownership of the arrays */
typedef struct box_mesh_cmd_t {
    uint32_t handle;        /**< Mesh handle */
    uint32_t vertex_count;  /**< Vertices in the array */
    uint32_t index_count;   /**< Indices in the array */
    float *vertices;        /**< Interleaved BOX_VERTEX_FLOATS per vertex */
    uint32_t *indices;      /**< Triangle indices */
    float bounds_min[3];    /**< Local bounds minimum of the unscaled mesh */
    float bounds_max[3];    /**< Local bounds maximum of the unscaled mesh */
} box_mesh_cmd_t;

/** Registration of one material layer, the renderer takes ownership of the source */
typedef struct box_layer_cmd_t {
    uint32_t index; /**< Bit the layer takes in the instance layer mask */
    int32_t order;  /**< Application order the registrations were sorted by */
    char *wgsl;     /**< Body of the layer function, freed by the renderer */
} box_layer_cmd_t;

/** Scene global parameters of one material layer, as the layer uniform expects them */
typedef struct box_layer_params_t {
    float a[4]; /**< First parameter vector */
    float b[4]; /**< Second parameter vector */
} box_layer_params_t;

/** Texture to upload, the renderer takes ownership of the pixel array */
typedef struct box_texture_cmd_t {
    uint32_t handle;   /**< Texture handle, size class in the high byte */
    uint32_t width;    /**< Width in pixels, the edge length of its size class */
    uint32_t height;   /**< Height in pixels, the edge length of its size class */
    uint32_t mips;     /**< Mip levels in the pixel array, level 0 first */
    uint32_t srgb;     /**< Pixels are display encoded */
    uint8_t *pixels;   /**< RGBA8 mip chain of the texture */
} box_texture_cmd_t;

/** Edge length of a texture size class.
 *
 * @param cls The size class
 * @return Edge length in pixels
 */
static inline uint32_t box_texture_class_size(
    uint32_t cls)
{
    return (uint32_t)BOX_TEXTURE_MIN_SIZE << cls;
}

/** Build a texture handle out of a size class and a layer.
 *
 * @param cls The size class
 * @param layer The array layer inside the class
 * @return The handle, never 0
 */
static inline uint32_t box_texture_handle(
    uint32_t cls,
    uint32_t layer)
{
    return (cls << 24) | (layer + 1);
}

/** Size class of a texture handle.
 *
 * @param handle The handle
 * @return The size class
 */
static inline uint32_t box_texture_class(
    uint32_t handle)
{
    return handle >> 24;
}

/** Array layer of a texture handle.
 *
 * @param handle The handle
 * @return The layer inside its size class
 */
static inline uint32_t box_texture_layer(
    uint32_t handle)
{
    return (handle & 0xFFFFFF) - 1;
}

/** Mip levels of a square texture, down to a single texel.
 *
 * @param size Edge length in pixels
 * @return The number of mip levels
 */
static inline uint32_t box_texture_mips(
    uint32_t size)
{
    uint32_t result = 1;
    while (size > 1) {
        size >>= 1;
        result ++;
    }

    return result;
}

/** Bytes an RGBA8 mip chain of a square texture occupies.
 *
 * @param size Edge length in pixels
 * @return The byte count
 */
static inline size_t box_texture_bytes(
    uint32_t size)
{
    size_t result = 0;
    uint32_t level = size;
    while (level >= 1) {
        result += (size_t)level * level * 4;
        if (level == 1) {
            break;
        }

        level >>= 1;
    }

    return result;
}

/** Write a texture handle into one of the packed texture slots of an instance.
 *
 * A slot holds the size class in its top two bits and the layer plus one below
 * them, so a slot of zero binds no texture and two slots fit in one word.
 *
 * @param inst The instance
 * @param slot Slot index, 0 albedo, 1 emissive, 2 roughness and 3 normal
 * @param handle The texture handle, 0 for none
 */
static inline void box_instance_set_texture(
    box_instance_t *inst,
    uint32_t slot,
    uint32_t handle)
{
    uint32_t packed = 0;
    if (handle) {
        packed = (box_texture_class(handle) << 14) | (box_texture_layer(handle) + 1);
    }

    uint32_t shift = (slot & 1) * 16;
    uint32_t word = inst->textures[slot >> 1] & ~(0xFFFFu << shift);
    inst->textures[slot >> 1] = word | (packed << shift);
}

/** Texture handle one of the packed texture slots of an instance names.
 *
 * @param inst The instance
 * @param slot Slot index, 0 albedo, 1 emissive, 2 roughness and 3 normal
 * @return The texture handle, 0 when the slot binds none
 */
static inline uint32_t box_instance_texture(
    const box_instance_t *inst,
    uint32_t slot)
{
    uint32_t packed = (inst->textures[slot >> 1] >> ((slot & 1) * 16)) & 0xFFFF;
    uint32_t result = 0;
    if (packed) {
        result = box_texture_handle(packed >> 14, (packed & 0x3FFF) - 1);
    }

    return result;
}

/** Write the blend and back face mode of an instance into its mode word.
 *
 * @param inst The instance
 * @param blend One of the BOX_BLEND constants
 * @param back One of the BOX_BACK constants
 */
static inline void box_instance_set_shading(
    box_instance_t *inst,
    uint32_t blend,
    uint32_t back)
{
    inst->uv_mode &= ~0xFF00u;
    inst->uv_mode |= (blend << BOX_MODE_BLEND_SHIFT) | (back << BOX_MODE_BACK_SHIFT);
}

/** Write the enabled material layers of an instance into its mode word.
 *
 * @param inst The instance
 * @param mask One bit per registered layer, the bit being its index
 */
static inline void box_instance_set_layers(
    box_instance_t *inst,
    uint32_t mask)
{
    inst->uv_mode = (inst->uv_mode & 0xFFFFu) | (mask << BOX_MODE_LAYER_SHIFT);
}

/** Material layer mask the mode word of an instance holds.
 *
 * @param inst The instance
 * @return One bit per enabled layer, the bit being its index
 */
static inline uint32_t box_instance_layers(
    const box_instance_t *inst)
{
    return inst->uv_mode >> BOX_MODE_LAYER_SHIFT;
}

/** Blend mode the mode word of an instance holds.
 *
 * @param inst The instance
 * @return One of the BOX_BLEND constants
 */
static inline uint32_t box_instance_blend(
    const box_instance_t *inst)
{
    return (inst->uv_mode >> BOX_MODE_BLEND_SHIFT) & 0xFu;
}

/** Back face mode the mode word of an instance holds.
 *
 * @param inst The instance
 * @return One of the BOX_BACK constants
 */
static inline uint32_t box_instance_back(
    const box_instance_t *inst)
{
    return (inst->uv_mode >> BOX_MODE_BACK_SHIFT) & 0x3u;
}

/** Instance data written to a slot of a mesh's static buffer */
typedef struct box_static_update_t {
    uint32_t mesh;          /**< Mesh handle */
    uint32_t slot;          /**< Instance slot */
    box_instance_t data;    /**< New instance data */
} box_static_update_t;

/** Contiguous run of static instance slots of a mesh that share a layer mask.
 *
 * The runs of one mesh are contiguous in the packet array and replace the runs
 * the renderer holds for it. A mesh without runs has no layer restriction and
 * is drawn in one call, which is what a scene without layers produces.
 */
typedef struct box_static_group_t {
    uint32_t mesh;   /**< Mesh handle */
    uint32_t mask;   /**< Layers the slots of the run belong to, 0 for every view */
    uint32_t first;  /**< First instance slot of the run */
    uint32_t count;  /**< Instance slots in the run */
} box_static_group_t;

/** New static instance count of a mesh */
typedef struct box_static_count_t {
    uint32_t mesh;          /**< Mesh handle */
    uint32_t count;         /**< Instances in use */
} box_static_count_t;

/** One instance the sorted pass draws after the opaque batches */
typedef struct box_transparent_t {
    uint32_t mesh;       /**< Mesh handle */
    uint32_t blend;      /**< Blend equation, BOX_BLEND_OPAQUE for a two sided opaque draw */
    uint32_t back;       /**< Back face mode of the instance */
    float depth;         /**< View space depth of its origin, larger is farther away */
    box_instance_t data; /**< Instance data */
} box_transparent_t;

/** Range of dynamic instances sharing a mesh */
typedef struct box_dynamic_batch_t {
    uint32_t mesh;          /**< Mesh handle */
    uint32_t first;         /**< First instance in the dynamic array */
    uint32_t count;         /**< Instances in the batch */
} box_dynamic_batch_t;

/** All work the renderer performs for one frame */
typedef struct box_packet_t {
    uint32_t frame;                          /**< Frame number */
    double time;                             /**< Seconds since start */
    uint32_t width;                          /**< Target width in pixels */
    uint32_t height;                         /**< Target height in pixels */
    float clear[4];                          /**< Clear color, linear RGBA */
    char capture_path[BOX_CAPTURE_PATH_MAX]; /**< PNG to write, empty for none */
    box_view_t view;                         /**< Copy of the first view, for readers of one view */
    bool has_view;                           /**< The packet holds at least one view */
    box_vec_t views;                         /**< box_view_t sorted by priority */
    uint32_t capture_view;                   /**< View the capture reads, or BOX_VIEW_NONE */
    box_vec_t target_create;                 /**< box_target_cmd_t, also resizes a handle */
    box_vec_t target_destroy;                /**< uint32_t handles */
    box_vec_t mesh_create;                   /**< box_mesh_cmd_t */
    box_vec_t mesh_destroy;                  /**< uint32_t handles */
    box_vec_t texture_create;                /**< box_texture_cmd_t, also replaces a handle */
    box_vec_t texture_destroy;               /**< uint32_t handles */
    box_vec_t layer_create;                  /**< box_layer_cmd_t, the whole list when it changed */
    bool layers_changed;                     /**< layer_create holds a new registration list */
    uint32_t layer_count;                    /**< Layers the scene has registered */
    box_layer_params_t layer_params[BOX_LAYER_MAX]; /**< Parameters of every layer */
    box_vec_t static_updates;                /**< box_static_update_t */
    box_vec_t static_counts;                 /**< box_static_count_t */
    box_vec_t static_groups;                 /**< box_static_group_t of the meshes that regrouped */
    box_vec_t dynamic_batches;               /**< box_dynamic_batch_t */
    box_vec_t dynamic_instances;             /**< box_instance_t */
    box_vec_t transparent;                   /**< box_transparent_t, sorted back to front */
    box_vec_t lights;                        /**< box_light_t inside the view frustum */
    box_vec_t bake_lights;                   /**< box_light_t of every static light, unculled */
    box_vec_t emitters;                      /**< box_emitter_t of every static emissive instance */
    box_bake_t bake;                         /**< Settings of the baked lighting pass */
} box_packet_t;

/** Initialize the vectors of a packet.
 *
 * @param p The packet
 */
static inline void box_packet_init(
    box_packet_t *p)
{
    memset(p, 0, sizeof(box_packet_t));
    box_vec_init(&p->views, sizeof(box_view_t));
    box_vec_init(&p->target_create, sizeof(box_target_cmd_t));
    box_vec_init(&p->target_destroy, sizeof(uint32_t));
    box_vec_init(&p->mesh_create, sizeof(box_mesh_cmd_t));
    box_vec_init(&p->mesh_destroy, sizeof(uint32_t));
    box_vec_init(&p->texture_create, sizeof(box_texture_cmd_t));
    box_vec_init(&p->texture_destroy, sizeof(uint32_t));
    box_vec_init(&p->layer_create, sizeof(box_layer_cmd_t));
    box_vec_init(&p->static_updates, sizeof(box_static_update_t));
    box_vec_init(&p->static_counts, sizeof(box_static_count_t));
    box_vec_init(&p->static_groups, sizeof(box_static_group_t));
    box_vec_init(&p->dynamic_batches, sizeof(box_dynamic_batch_t));
    box_vec_init(&p->dynamic_instances, sizeof(box_instance_t));
    box_vec_init(&p->transparent, sizeof(box_transparent_t));
    box_vec_init(&p->lights, sizeof(box_light_t));
    box_vec_init(&p->bake_lights, sizeof(box_light_t));
    box_vec_init(&p->emitters, sizeof(box_emitter_t));
}

/** Reset per frame fields while keeping vector storage.
 *
 * @param p The packet
 */
static inline void box_packet_reset(
    box_packet_t *p)
{
    p->frame = 0;
    p->time = 0;
    p->width = 0;
    p->height = 0;
    p->capture_path[0] = '\0';
    p->has_view = false;
    p->capture_view = BOX_VIEW_NONE;
    memset(&p->view, 0, sizeof(box_view_t));
    box_vec_clear(&p->views);
    box_vec_clear(&p->target_create);
    box_vec_clear(&p->target_destroy);
    box_vec_clear(&p->mesh_create);
    box_vec_clear(&p->mesh_destroy);
    box_vec_clear(&p->texture_create);
    box_vec_clear(&p->texture_destroy);
    box_vec_clear(&p->layer_create);
    p->layers_changed = false;
    box_vec_clear(&p->static_updates);
    box_vec_clear(&p->static_counts);
    box_vec_clear(&p->static_groups);
    box_vec_clear(&p->dynamic_batches);
    box_vec_clear(&p->dynamic_instances);
    box_vec_clear(&p->transparent);
    box_vec_clear(&p->lights);
    box_vec_clear(&p->bake_lights);
    box_vec_clear(&p->emitters);
}

/** Release the vectors of a packet.
 *
 * @param p The packet
 */
static inline void box_packet_fini(
    box_packet_t *p)
{
    box_vec_fini(&p->views);
    box_vec_fini(&p->target_create);
    box_vec_fini(&p->target_destroy);
    box_vec_fini(&p->mesh_create);
    box_vec_fini(&p->mesh_destroy);
    box_vec_fini(&p->texture_create);
    box_vec_fini(&p->texture_destroy);
    box_vec_fini(&p->layer_create);
    box_vec_fini(&p->static_updates);
    box_vec_fini(&p->static_counts);
    box_vec_fini(&p->static_groups);
    box_vec_fini(&p->dynamic_batches);
    box_vec_fini(&p->dynamic_instances);
    box_vec_fini(&p->transparent);
    box_vec_fini(&p->lights);
    box_vec_fini(&p->bake_lights);
    box_vec_fini(&p->emitters);
}

#endif
