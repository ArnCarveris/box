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
#include "transition.h"

/** View index that names no view, the capture then reads the surface */
#define BOX_VIEW_NONE (0xFFFFFFFFu)

/** Longest capture path stored in a packet */
#define BOX_CAPTURE_PATH_MAX (512)

/** Version of the vertex layout, bumped whenever its channels change.
 *
 * The renderer, the mesh upload and the mesh shader read one stride, so a build
 * that mixes a vertex buffer of one version with a pipeline of another misreads
 * every vertex. The version keys the layout so a mismatch is caught where the
 * layout is asserted instead of showing up as scrambled geometry.
 */
#define BOX_VERTEX_VERSION (2)

/** Floats per vertex: position, normal, uv, the packed bounds local position and the color */
#define BOX_VERTEX_FLOATS (10)

/** Bytes from a vertex to its packed local position, a Unorm10_10_10_2 value */
#define BOX_VERTEX_LOCAL_OFFSET (32)

/** Bytes from a vertex to its per vertex color, a Unorm8x4 value */
#define BOX_VERTEX_COLOR_OFFSET (36)

/** Per vertex color of a mesh that carries none, which leaves the material alone */
#define BOX_VERTEX_COLOR_WHITE (0xFFFFFFFFu)

/** Material layers a scene can register, one bit each in the instance layer mask */
#define BOX_LAYER_MAX (16)

/** Light modulators a scene can register, one bit each in the light modulator mask */
#define BOX_MODULATOR_MAX (16)

/** Floats the parameter block of one light modulator holds */
#define BOX_MODULATOR_PARAMS (16)

/** Planes of a view frustum: left, right, bottom, top, near and far */
#define BOX_FRUSTUM_PLANES (6)

/** Words of one indexed indirect draw argument */
#define BOX_CULL_ARG_WORDS (5)

/** Instances one workgroup of the culling pass tests */
#define BOX_CULL_GROUP (64)

/** Instances one workgroup of the occlusion pass tests */
#define BOX_HIZ_GROUP (64)

/** Probes one workgroup of the bake passes fills */
#define BOX_BAKE_GROUP (64)

/** Mip levels of the prefiltered specular cubemap, one per roughness step */
#define BOX_ENV_MIPS (5)

/** Samples the specular prefilter takes per texel */
#define BOX_ENV_SPECULAR_SAMPLES (64)

/** Samples the irradiance convolution takes per texel */
#define BOX_ENV_IRRADIANCE_SAMPLES (128)

/** Samples the BRDF integration takes per texel */
#define BOX_ENV_BRDF_SAMPLES (128)

/** Screen heights a translucent surface bends the background sample it reads by */
#define BOX_GLASS_REFRACT (0.03)

/** Width of the transmittance table, one column per distance through the medium */
#define BOX_SKY_TRANS_W (256)

/** Height of the transmittance table, one row per altitude */
#define BOX_SKY_TRANS_H (64)

/** Width of the multiple scattering table, one column per sun elevation */
#define BOX_SKY_MS_W (32)

/** Height of the multiple scattering table, one row per altitude */
#define BOX_SKY_MS_H (32)

/** Width of the sky view table, one column per azimuth */
#define BOX_SKY_VIEW_W (192)

/** Height of the sky view table, one row per zenith angle */
#define BOX_SKY_VIEW_H (108)

/** Shader slot of the mesh pipelines, which the material layers compose */
#define BOX_SHADER_MESH (0)

/** Shader slot of the light cluster pass */
#define BOX_SHADER_CLUSTER (1)

/** Shader slot of the frustum culling pass */
#define BOX_SHADER_CULL (2)

/** Shader slot of the depth target halving of the occlusion pyramid */
#define BOX_SHADER_HIZ_COPY (3)

/** Shader slot of the level reduction of the occlusion pyramid */
#define BOX_SHADER_HIZ_REDUCE (4)

/** Shader slot of the two occlusion passes */
#define BOX_SHADER_HIZ (5)

/** Shader slot of the shadow cascade pass */
#define BOX_SHADER_SHADOW (6)

/** Shader slot of the environment gradient and BRDF passes */
#define BOX_SHADER_ENV (7)

/** Shader slot of the environment prefilter passes */
#define BOX_SHADER_ENV_FILTER (8)

/** Shader slot of the occupancy grid pass of the light bake */
#define BOX_SHADER_BAKE_GRID (9)

/** Shader slot of the probe pass of the light bake */
#define BOX_SHADER_BAKE (10)

/** Shader slot of the blur chain of the background copy */
#define BOX_SHADER_BLUR (11)

/** Shader slot of the pass that writes the motion vectors of a view */
#define BOX_SHADER_MOTION (12)

/** Shader slot of the pass that draws the rectangles of the frame rate panel */
#define BOX_SHADER_OVERLAY (13)

/** Shader slot of the pass that draws the debug line list */
#define BOX_SHADER_LINES (14)

/** Shader slots the renderer builds its own pipelines from */
#define BOX_SHADER_BUILTIN_MAX (15)

/** Floats the parameter block of a post process effect holds */
#define BOX_EFFECT_PARAM_MAX (16)

/** Vectors the parameter block of a post process effect holds */
#define BOX_EFFECT_PARAM_VECS (BOX_EFFECT_PARAM_MAX / 4)

/** Effects one view can chain */
#define BOX_EFFECT_MAX (8)

/** The pass reads the image the view rendered */
#define BOX_EFFECT_COLOR (1u)

/** The pass reads the depth target of the view */
#define BOX_EFFECT_DEPTH (2u)

/** The pass reads the world normals the mesh pass wrote */
#define BOX_EFFECT_NORMALS (4u)

/** The pass reads the motion vectors of the view */
#define BOX_EFFECT_MOTION (8u)

/** The pass reads the display transform lookup cube the engine ships */
#define BOX_EFFECT_TEXTURE (16u)

#define BOX_EFFECT_TEXTURE_MAX (8u)

#define BOX_EFFECT_TEXTURE_NONE UINT32_MAX

#define BOX_EFFECT_SETUP_NONE (0u)
#define BOX_EFFECT_SETUP_COMPUTE (1u)
#define BOX_EFFECT_SETUP_MIP_CHAIN (2u)
#define BOX_EFFECT_SETUP_TEMPORAL (3u)
#define BOX_EFFECT_TEXTURE_RG16 (0u)
#define BOX_EFFECT_TEXTURE_RGBA16 (1u)

/** Shader handle of a shader that has none yet, which asks for a new one */
#define BOX_SHADER_NONE (0xFFFFFFFFu)

/** Render layers a scene can create, one bit each in the view and instance masks */
#define BOX_VIEW_LAYER_BITS (32)

/** Layer mask of a view that lists no layers, which shows every layer */
#define BOX_VIEW_LAYER_ALL (0xFFFFFFFFu)

/** Cascades a view can split its shadow map into */
#define BOX_SHADOW_CASCADE_MAX (4)

/** Radii the light frustum of a cascade reaches behind its sphere for casters */
#define BOX_SHADOW_EXTRUDE (2.0f)

/** Point and spot lights a packet can carry */
#define BOX_LIGHT_MAX (4096)

/** Light type of an omnidirectional light */
#define BOX_LIGHT_POINT (0)

/** Light type of a cone light */
#define BOX_LIGHT_SPOT (1)

/** Texture size classes, each a list of array textures of one edge length */
#define BOX_TEXTURE_CLASSES (4)

/** Layers one page holds, the array layer limit WebGPU guarantees on a device */
#define BOX_TEXTURE_PAGE_LAYERS (256)

/** Pages the pager hands out, shared by the size classes and bound as one list */
#define BOX_TEXTURE_PAGES (8)

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

/** Bit that keeps an instance out of the shadow map of a view */
#define BOX_MODE_NOCAST_SHIFT (14)

/** Bit that says the shape words of an instance hold a 2D shape */
#define BOX_MODE_SHAPE_SHIFT (15)

/** Bit the material layer mask of an instance starts at in its mode word */
#define BOX_MODE_LAYER_SHIFT (16)

/** Bit that says the 2D shape of an instance is a soft fill instead of a border */
#define BOX_MODE_SOFT_SHIFT (4)

#define BOX_MODE_LAYER_DATA_SHIFT (5)

/** Bit that says the clip words of an instance hold a clip rectangle */
#define BOX_MODE_CLIP_SHIFT (6)

#define BOX_MODE_BACKDROP_SHIFT (7)

#define BOX_LAYER_DATA_WORDS (2)

#define BOX_LAYER_DATA_SCALE_STEP (64.0f)

#define BOX_LAYER_DATA_SCALE_MAX (255.0f / BOX_LAYER_DATA_SCALE_STEP)

/** Words the packed 2D shape parameters of an instance take.
 *
 * The first holds the four corner radii, the second the four edge widths, the
 * third the border color and the fourth the two half extents of the rectangle
 * the radii and widths are measured against. They sit behind the bounding
 * sphere the culling passes read, so every word before them keeps its offset.
 */
#define BOX_SHAPE_WORDS (4)

/** Highest layer a texture handle names, every page of the pager filled */
#define BOX_TEXTURE_LAYER_MAX (BOX_TEXTURE_PAGES * BOX_TEXTURE_PAGE_LAYERS - 1)

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
    uint32_t shape[BOX_SHAPE_WORDS]; /**< Packed 2D shape parameters, zero without one */
    uint32_t layer_data[BOX_LAYER_DATA_WORDS];
    float clip[4];                   /**< World space clip rectangle: min x, min y, max x, max y */
} box_instance_t;

/** 32 bit words one instance takes in a GPU instance buffer */
#define BOX_INSTANCE_WORDS (45)

/** Word the bounding sphere of an instance starts at */
#define BOX_INSTANCE_BOUNDS (31)

/** Word the tiling, blend, back face, caster and layer modes of an instance sit in */
#define BOX_INSTANCE_MODE (30)

/** Word the packed 2D shape parameters of an instance start at */
#define BOX_INSTANCE_SHAPE (35)

/** Word the packed per entity layer amounts of an instance start at */
#define BOX_INSTANCE_LAYER_DATA (39)

/** Word the clip rectangle of an instance starts at */
#define BOX_INSTANCE_CLIP (41)

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
    uint32_t modulator;  /**< Modulator slots of the light, one bit each, 0 for none */
} box_light_t;

/** One emissive static instance, treated as a sphere light by the bake */
typedef struct box_emitter_t {
    float position[3];  /**< World space center of the instance bounds */
    float radius;       /**< Radius of the sphere the instance is treated as */
    float color[3];     /**< Linear emissive color times its strength */
    float range;        /**< Distance at which the emitted light reaches zero */
} box_emitter_t;

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

/** Atmosphere of the frame, the parameter block the sky shaders read
 *
 * The vectors hold the medium in the units the shaders integrate in: radii and
 * scale heights in kilometers and the scattering and absorption coefficients
 * per kilometer, so the renderer copies the block into its uniform and adds
 * only what belongs to the view it is drawing.
 */
typedef struct box_sky_t {
    float sun_dir[4];       /**< Direction toward the sun, w is the display encode flag */
    float sun_color[4];     /**< Radiance of the sun over the medium, w the haze density */
    float ground_albedo[4]; /**< Color the planet reflects, w unused */
    float bounds[4];        /**< Planet radius, top radius, Rayleigh and Mie scale heights */
    float scat_rayleigh[4]; /**< Rayleigh scattering per kilometer, w unused */
    float scat_mie[4];      /**< Mie scattering per kilometer, w unused */
    float ext_mie[4];       /**< Mie extinction per kilometer, w unused */
    float abs_ozone[4];     /**< Ozone absorption per kilometer, w unused */
    float abs_strat[4];     /**< Stratospheric aerosol absorption per kilometer, w unused */
    float abs_haze[4];      /**< Grey haze absorption per kilometer, w unused */
    float mie_params[4];    /**< Anisotropy, haze distance, disk cosine and disk radiance */
    float world_scale[4];   /**< Units per kilometer, its inverse, sea level y, ground km */
    float night[4];         /**< Radiance the sky keeps after sunset, w is the overcast amount */
    float turbulence[4];    /**< Warp amount, spatial scale, speed and the time of the frame */
    float moon_dir[4];      /**< Direction toward the moon, w the cosine of its disk */
    float moon_color[4];    /**< Radiance of the moon over the medium, w its lit fraction */
    float stars_color[4];   /**< Radiance of the star field, w the part daylight leaves */
    float stars[4];         /**< Cells over a turn, star size, twinkle and the field time */
    float stars_rot[12];    /**< World to star space rotation, three rows of four floats */
    uint32_t active;        /**< The world holds an atmosphere entity */
    uint32_t lut_dirty;     /**< The transmittance and multiple scattering tables must rerun */
    uint32_t view_dirty;    /**< The sky view table must rerun */
    uint32_t generation;    /**< Rises every time the sky view table is rendered again */
} box_sky_t;

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
    uint32_t material_layer_count;
    uint32_t plain_blend;   /**< Blended fragments composite source over, without the glass model */
    uint32_t sun_modulator; /**< Modulator slots of the sun, one bit each, 0 for none */
    float frame_time;       /**< Seconds since the world started, for an animated modulator */
    uint32_t material_layers[BOX_LAYER_MAX];
    float shadow_matrix[BOX_SHADOW_CASCADE_MAX * 16]; /**< World to clip of every cascade */
    float shadow_split[4];  /**< View depth a cascade ends at, the last is the distance */
    float shadow_offset[4]; /**< World offset along the normal of a cascade, its texel size */
    float shadow_bias[4];   /**< Depth offset of the comparison of a cascade, in clip units */
    float shadow_fade;      /**< Fraction of the last cascade the shadows fade out over */
    float shadow_distance;  /**< View depth the shadows reach */
    float shadow_texel;     /**< Uv size of one texel of a cascade */
    float shadow_pad;       /**< Padding to the 16 byte GPU stride */
    uint32_t shadow_cascades; /**< Cascades the view renders, 0 when it has no shadows */
    uint32_t shadow_filter; /**< PCF taps the comparison takes per axis */
    uint32_t unlit;         /**< The view shows albedo and emissive without shading it */
    uint32_t shadow_pad2;   /**< Padding to the 16 byte GPU stride */
    uint32_t target;        /**< Render target handle, 0 draws into the surface */
    uint32_t width;         /**< Target width in pixels */
    uint32_t height;        /**< Target height in pixels */
    float clear[4];         /**< Clear color of the target, linear RGBA */
    uint32_t light_first;   /**< First light of the view in the packet light array */
    uint32_t transparent_first; /**< First sorted draw of the view in the packet */
    uint32_t transparent_count; /**< Sorted draws of the view */
    uint32_t translucent_count; /**< Sorted draws of the view that read the background copy */
    uint32_t shadows;       /**< The view casts and receives shadows */
    uint32_t effect_first;  /**< First pass of its chain in the packet effect array */
    uint32_t effect_count;  /**< Passes of its chain */
    uint32_t effect_inputs; /**< BOX_EFFECT bits the passes of its chain need */
    float clip[16];         /**< World to clip of this frame, for a pass that marches */
    float inv_clip[16];     /**< Clip to world of this frame, for the motion pass */
    float prev_clip[16];    /**< World to clip of the previous frame of the view */
    int32_t priority;       /**< Order in the frame, lower renders first */
    uint32_t layer_mask;    /**< Layers the view shows, BOX_VIEW_LAYER_ALL for every layer */
    uint32_t caster_mask;   /**< Layers that cast its shadows, BOX_VIEW_LAYER_ALL for all */
    uint32_t dynamic_first; /**< First dynamic batch of the view in the packet */
    uint32_t dynamic_count; /**< Dynamic batches of the view */
    uint32_t cull_enabled;  /**< Instances outside the frustum of the view are not drawn */
    uint32_t cull_threshold; /**< Static instances a batch needs before a compute pass culls it */
    uint32_t occlusion;     /**< Compute culled static batches are tested against a depth pyramid */
    uint32_t hiz_levels;    /**< Levels of that pyramid, 0 for the whole chain */
    uint32_t strict_layers; /**< A view that lists layers draws its members only */
    uint32_t shadow_resolution; /**< Edge of one cascade of its shadow map in texels */
    float shadow_bounds[BOX_SHADOW_CASCADE_MAX * 4]; /**< Sphere of a cascade, center and radius */
    uint32_t overlay;       /**< The view draws the frame rate panel over its image */
    uint32_t overlay_corner; /**< Corner of the view the panel is pinned to */
    float overlay_scale;    /**< Size multiplier of the panel, one for its natural size */
    float overlay_opacity;  /**< Opacity of the panel background, one for opaque */
    uint32_t sky;           /**< The view draws the atmosphere behind its geometry */
    uint32_t sky_env;       /**< Its environment is prefiltered from the atmosphere */
    uint32_t lod_enabled;   /**< The view selects LOD levels by their projected error */
    float lod_error_px;     /**< Projected error in pixels a level may not exceed */
    float lod_shadow_error_px; /**< The same threshold for the shadow passes of the view */
    float lod_hysteresis;   /**< Band around those thresholds a level switch has to cross */
} box_view_t;

/** Bytes of a view that the view uniform buffer holds */
#define BOX_VIEW_UNIFORM_SIZE (offsetof(box_view_t, target))

/** Check whether a view with a layer mask draws a membership mask.
 *
 * An entity that joined no layer is drawn by every view, which is what a scene
 * with one restricted view needs for its floor and props; a strict view lists
 * layers and draws their members only.
 *
 * @param mask The membership mask, 0 for an entity that joined no layer
 * @param view_mask The layer mask of the view
 * @param strict The view draws the members of its layers only
 * @return True when the view draws it
 */
static inline bool box_layer_shown(
    uint32_t mask,
    uint32_t view_mask,
    bool strict)
{
    return mask ? (mask & view_mask) != 0 : !strict;
}

/** Check whether a view draws the entities of a membership mask.
 *
 * @param v The view
 * @param mask The membership mask, 0 for an entity that joined no layer
 * @return True when the view draws it
 */
static inline bool box_view_shows(
    const box_view_t *v,
    uint32_t mask)
{
    return box_layer_shown(mask, v->layer_mask, v->strict_layers != 0);
}

/** Check whether the entities of a membership mask cast the shadows of a view.
 *
 * @param v The view
 * @param mask The membership mask, 0 for an entity that joined no layer
 * @return True when the shadow pass of the view draws it
 */
static inline bool box_view_casts(
    const box_view_t *v,
    uint32_t mask)
{
    return box_layer_shown(mask, v->caster_mask, v->strict_layers != 0);
}

/** Texels on one axis of the depth grid the renderer hands the LOD traversal */
#define BOX_LOD_DEPTH_MAX (64)

/** Coarse depth of a view the renderer read back, for the LOD traversal.
 *
 * The grid is one level of the depth pyramid of a view, small enough to read
 * back every frame, together with the matrix that level was drawn with. The
 * traversal reduces it further on the CPU and tests the bounds of a node
 * against it, so a hidden node stops at its proxy instead of being descended
 * into. Nothing is dropped on it: the proxy is still drawn and culled on the
 * GPU, so a stale grid costs detail for a frame and never an object.
 */
typedef struct box_lod_depth_t {
    float values[BOX_LOD_DEPTH_MAX * BOX_LOD_DEPTH_MAX]; /**< Depth of every texel, row by row */
    float clip[16];  /**< World to clip matrix the grid was drawn with */
    uint32_t width;  /**< Texels across the grid */
    uint32_t height; /**< Texels down the grid */
    uint32_t valid;  /**< The grid holds a readback */
} box_lod_depth_t;

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

/** One post process pass of a view, in the order the chain runs them */
typedef struct box_effect_t {
    uint32_t shader;  /**< Shader handle of the pass */
    uint32_t texture;
    uint32_t setup_shader;
    uint32_t setup_kind;
    uint32_t setup_format;
    int32_t setup_parameter[4];
    uint32_t setup_workgroup_size;
    uint32_t setup_buffer_size[2];
    float setup_initial[4];
    uint32_t inputs;  /**< BOX_EFFECT bits of the inputs the pass declared */
    float scale;      /**< Resolution scale of its output, 1 for the view size */
    float params[BOX_EFFECT_PARAM_MAX]; /**< Uniform block of its parameter component */
} box_effect_t;

/** Size of an intermediate target at a resolution scale.
 *
 * @param width Width of the view in pixels
 * @param height Height of the view in pixels
 * @param scale Resolution scale, 0 and 1 both keep the view size
 * @param out_width Receives the width, never 0
 * @param out_height Receives the height, never 0
 */
static inline void box_effect_size(
    uint32_t width,
    uint32_t height,
    float scale,
    uint32_t *out_width,
    uint32_t *out_height)
{
    float s = scale > 0.0f ? scale : 1.0f;
    uint32_t w = (uint32_t)((float)width * s + 0.5f);
    uint32_t h = (uint32_t)((float)height * s + 0.5f);
    *out_width = w ? w : 1;
    *out_height = h ? h : 1;
}

static inline bool box_view_hdr(
    const box_view_t *v)
{
    return v->effect_count != 0;
}

/** Gamma encode flag of the view uniform, which the mesh pass encodes with.
 *
 * The float scene target holds linear light, so the encode moves to the pass
 * that writes the image of the view and the mesh pass only encodes when it
 * writes an 8 bit target the hardware does not encode for it.
 *
 * @param hdr The scene passes write the float target
 * @param srgb The target of the view is an sRGB format
 * @return 1 when the mesh pass encodes, 0 when it writes linear light
 */
static inline float box_view_gamma(
    bool hdr,
    bool srgb)
{
    return (hdr || srgb) ? 0.0f : 1.0f;
}

/** Decode flag a pass of a chain reads its image with.
 *
 * @param hdr The scene passes write the float target
 * @param srgb The target of the view is an sRGB format
 * @return 1 when the image the pass reads is display encoded
 */
static inline float box_effect_decode_flag(
    bool hdr,
    bool srgb)
{
    return (hdr || srgb) ? 0.0f : 1.0f;
}

/** Encode flag a pass of a chain writes its image with.
 *
 * On the float path every pass hands linear light to the next one and only the
 * last pass encodes, so the sum of the flags over a chain is one encode.
 *
 * @param hdr The scene passes write the float target
 * @param srgb The target of the view is an sRGB format
 * @param last The pass writes the image of the view
 * @return 1 when the pass encodes what it writes
 */
static inline float box_effect_encode_flag(
    bool hdr,
    bool srgb,
    bool last)
{
    float result = srgb ? 0.0f : 1.0f;
    if (hdr && !last) {
        result = 0.0f;
    }

    return result;
}

/** Display transform lookup cube to upload, the renderer owns the texel array */
typedef struct box_effect_texture_cmd_t {
    uint32_t handle;
    uint32_t dim;     /**< Texels along one edge of the cube */
    uint32_t *texels; /**< dim cubed RGB9E5 texels, freed by the renderer */
} box_effect_texture_cmd_t;

/** WGSL module to compile, the renderer takes ownership of the source */
typedef struct box_shader_cmd_t {
    uint32_t handle; /**< Shader handle, a BOX_SHADER constant for a builtin one */
    char *source;    /**< Preprocessed WGSL, freed by the renderer */
} box_shader_cmd_t;

/** Scene global parameters of one material layer, as the layer uniform expects them */
typedef struct box_layer_params_t {
    float a[4]; /**< First parameter vector */
    float b[4]; /**< Second parameter vector */
} box_layer_params_t;

/** Parameter block of one light modulator, as the modulator uniform expects it */
typedef struct box_modulator_params_t {
    float v[BOX_MODULATOR_PARAMS]; /**< Floats of the parameter component, in member order */
} box_modulator_params_t;

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
 * The layer counts across the pages of the pager, so its high bits are the page
 * the layer lives in and its low bits the layer inside that page.
 *
 * @param handle The handle
 * @return The layer inside its size class
 */
static inline uint32_t box_texture_layer(
    uint32_t handle)
{
    return (handle & 0xFFFFFF) - 1;
}

/** Page of the pager a texture handle lives in.
 *
 * @param handle The handle
 * @return The page
 */
static inline uint32_t box_texture_page(
    uint32_t handle)
{
    return box_texture_layer(handle) / BOX_TEXTURE_PAGE_LAYERS;
}

/** Array layer a texture handle takes inside its page.
 *
 * @param handle The handle
 * @return The layer inside the page
 */
static inline uint32_t box_texture_page_layer(
    uint32_t handle)
{
    return box_texture_layer(handle) % BOX_TEXTURE_PAGE_LAYERS;
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
 * them, so a slot of zero binds no texture and two slots fit in one word. The
 * layer spans the pages of the size class: its high bits name the page and its
 * low bits the layer inside it, which is what the shader splits it into.
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
    inst->uv_mode &= ~0x3F00u;
    inst->uv_mode |= (blend << BOX_MODE_BLEND_SHIFT) | (back << BOX_MODE_BACK_SHIFT);
}

/** Write whether an instance is drawn into the shadow map into its mode word.
 *
 * The bit is set when the instance is not a caster, so an instance without any
 * material component keeps a mode word of zero.
 *
 * @param inst The instance
 * @param caster The instance casts a shadow
 */
static inline void box_instance_set_caster(
    box_instance_t *inst,
    bool caster)
{
    inst->uv_mode &= ~(1u << BOX_MODE_NOCAST_SHIFT);
    inst->uv_mode |= caster ? 0u : (1u << BOX_MODE_NOCAST_SHIFT);
}

/** Check whether the mode word of an instance marks it as a shadow caster.
 *
 * @param inst The instance
 * @return True when the shadow pass draws it
 */
static inline bool box_instance_caster(
    const box_instance_t *inst)
{
    return (inst->uv_mode & (1u << BOX_MODE_NOCAST_SHIFT)) == 0;
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

static inline void box_instance_set_layer_data(
    box_instance_t *inst,
    const float *amounts,
    float seed,
    float scale)
{
    float packed[7];
    uint32_t i;
    for (i = 0; i < 6; i ++) {
        packed[i] = amounts[i];
    }

    packed[6] = seed;
    inst->layer_data[0] = 0;
    inst->layer_data[1] = 0;
    for (i = 0; i < 7; i ++) {
        float v = packed[i] < 0.0f ? 0.0f : packed[i];
        uint32_t byte = (uint32_t)(v > 1.0f ? 255.0f : v * 255.0f + 0.5f);
        inst->layer_data[i >> 2] |= byte << ((i & 3) * 8);
    }

    float steps = scale < 0.0f ? 0.0f : scale * BOX_LAYER_DATA_SCALE_STEP + 0.5f;
    uint32_t scale_byte = (uint32_t)(steps > 255.0f ? 255.0f : steps);

    inst->layer_data[1] |= scale_byte << 24;
    inst->uv_mode |= 1u << BOX_MODE_LAYER_DATA_SHIFT;
}

static inline bool box_instance_has_layer_data(
    const box_instance_t *inst)
{
    return (inst->uv_mode & (1u << BOX_MODE_LAYER_DATA_SHIFT)) != 0;
}

/** Read one of the per entity layer values of an instance back.
 *
 * @param inst The instance
 * @param index 0 to 5 the amounts, 6 the seed and 7 the feature scale
 */
static inline float box_instance_layer_data(
    const box_instance_t *inst,
    uint32_t index)
{
    float result = index == 6 ? 0.0f : 1.0f;
    if (box_instance_has_layer_data(inst)) {
        uint32_t byte = (inst->layer_data[index >> 2] >> ((index & 3) * 8)) & 0xFFu;
        result = index == 7 ?
            (float)byte / BOX_LAYER_DATA_SCALE_STEP : (float)byte / 255.0f;
    }

    return result;
}

/** Mark whether the shape words of an instance hold a 2D shape.
 *
 * The bit keeps the analytic rounded rectangle of the fragment stage out of
 * every draw that is not a 2D shape, so a scene without one is unchanged.
 *
 * @param inst The instance
 * @param on The shape words are filled
 */
static inline void box_instance_set_shape(
    box_instance_t *inst,
    bool on)
{
    inst->uv_mode &= ~(1u << BOX_MODE_SHAPE_SHIFT);
    inst->uv_mode |= on ? (1u << BOX_MODE_SHAPE_SHIFT) : 0u;
}

/** Check whether the shape words of an instance hold a 2D shape.
 *
 * @param inst The instance
 * @return True when the fragment stage evaluates the shape
 */
static inline bool box_instance_shape(
    const box_instance_t *inst)
{
    return (inst->uv_mode & (1u << BOX_MODE_SHAPE_SHIFT)) != 0;
}

/** Mark whether the 2D shape of an instance is a soft fill.
 *
 * A soft shape covers the rectangle its edge widths inset and fades out over
 * those widths instead of drawing a border ring, which is what a box shadow
 * draws with the rounded rectangle of a border.
 *
 * @param inst The instance
 * @param on The shape is a soft fill
 */
static inline void box_instance_set_soft(
    box_instance_t *inst,
    bool on)
{
    inst->uv_mode &= ~(1u << BOX_MODE_SOFT_SHIFT);
    inst->uv_mode |= on ? (1u << BOX_MODE_SOFT_SHIFT) : 0u;
}

/** Check whether the 2D shape of an instance is a soft fill.
 *
 * @param inst The instance
 * @return True when the fragment stage fades the shape out
 */
static inline bool box_instance_soft(
    const box_instance_t *inst)
{
    return (inst->uv_mode & (1u << BOX_MODE_SOFT_SHIFT)) != 0;
}

/** Give an instance the world space rectangle its fragments are clipped to.
 *
 * The rectangle is axis aligned in the plane the UI trees are laid out in, and
 * the fragment stage drops every fragment outside it, so a subtree is cut off
 * by the box of the element that clips it on every draw it takes part in.
 *
 * @param inst The instance
 * @param min_x Left edge of the rectangle
 * @param min_y Bottom edge of the rectangle
 * @param max_x Right edge of the rectangle
 * @param max_y Top edge of the rectangle
 */
static inline void box_instance_set_clip(
    box_instance_t *inst,
    float min_x,
    float min_y,
    float max_x,
    float max_y)
{
    inst->clip[0] = min_x;
    inst->clip[1] = min_y;
    inst->clip[2] = max_x;
    inst->clip[3] = max_y;
    inst->uv_mode |= 1u << BOX_MODE_CLIP_SHIFT;
}

/** Check whether the clip words of an instance hold a clip rectangle.
 *
 * @param inst The instance
 * @return True when the fragment stage clips it
 */
static inline bool box_instance_clipped(
    const box_instance_t *inst)
{
    return (inst->uv_mode & (1u << BOX_MODE_CLIP_SHIFT)) != 0;
}

/** Pack four ratios in 0..1 into one word of four bytes.
 *
 * @param values Four ratios, the first landing in the low byte
 * @return The packed word, which unpack4x8unorm reverses
 */
static inline uint32_t box_shape_pack4(
    const float *values)
{
    uint32_t result = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        float v = values[i] < 0.0f ? 0.0f : (values[i] > 1.0f ? 1.0f : values[i]);

        result |= (uint32_t)(v * 255.0f + 0.5f) << (i * 8);
    }

    return result;
}

/** Pack two ratios in 0..1 into one word of two halves.
 *
 * @param x The first ratio, which lands in the low half
 * @param y The second ratio
 * @return The packed word, which unpack2x16unorm reverses
 */
static inline uint32_t box_shape_pack2(
    float x,
    float y)
{
    float a = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    float b = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);

    return (uint32_t)(a * 65535.0f + 0.5f) | ((uint32_t)(b * 65535.0f + 0.5f) << 16);
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
    uint32_t transition;    /**< Transition the slot starts, BOX_UPDATE_STILL for none */
    uint32_t flags;
    box_instance_t data;    /**< New instance data */
} box_static_update_t;

#define BOX_STATIC_UPDATE_ADDED (1u)

/** Transition of an instance update that starts none.
 *
 * An update names its command as the index of the command plus one, so an
 * update that was only zeroed starts no transition.
 */
#define BOX_UPDATE_STILL (0)

/** Transition of an instance update the renderer cannot evaluate on its own */
#define BOX_UPDATE_SLOW (0xFFFFFFFFu)

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
    uint32_t flags;
} box_static_count_t;

#define BOX_STATIC_COUNT_REMOVED (1u)

/** One instance the sorted pass draws after the opaque batches.
 *
 * An entry with a group handle is not a lit draw but the composite of a subtree
 * that was rendered into that target: the quad shows the target instead of its
 * own material and its color alpha is the opacity the subtree composites with.
 */
typedef struct box_transparent_t {
    uint32_t mesh;       /**< Mesh handle */
    uint32_t blend;      /**< Blend equation, BOX_BLEND_OPAQUE for a two sided opaque draw */
    uint32_t back;       /**< Back face mode of the instance */
    uint32_t group;      /**< Render target the quad composites, 0 for an ordinary draw */
    float depth;         /**< View space depth of its origin, larger is farther away */
    box_instance_t data; /**< Instance data */
} box_transparent_t;

/** Range of dynamic instances sharing a mesh */
typedef struct box_dynamic_batch_t {
    uint32_t mesh;          /**< Mesh handle */
    uint32_t first;         /**< First instance in the dynamic array */
    uint32_t count;         /**< Instances in the batch */
} box_dynamic_batch_t;

/** Particles one workgroup of the spawn and the integrate pass covers */
#define BOX_PARTICLE_GROUP (64)

/** Workgroups one dispatch may have on an axis, the least a device guarantees */
#define BOX_PARTICLE_GROUPS_MAX (65535)

/** Particles one emitter can keep alive, a pool the widest dispatch still covers */
#define BOX_PARTICLE_MAX (BOX_PARTICLE_GROUPS_MAX * BOX_PARTICLE_GROUP)

/** Points the shape table of one emitter holds, sampled from a mesh or a curve */
#define BOX_PARTICLE_POINTS (64)

/** Word of the counter buffer holding the free slots of a pool */
#define BOX_PARTICLE_COUNT_FREE (0)

/** Word of the counter buffer holding the live particles of a pool */
#define BOX_PARTICLE_COUNT_LIVE (1)

/** Word of the counter buffer the compacted alive list is written through */
#define BOX_PARTICLE_COUNT_ALIVE (2)

/** Word of the counter buffer holding the free slots the frame allocates from */
#define BOX_PARTICLE_COUNT_BASE (3)

/** Word of the counter buffer holding the slots the frame allocated */
#define BOX_PARTICLE_COUNT_TAKE (4)

/** Words the counter buffer of a pool holds, one per counter */
#define BOX_PARTICLE_COUNTS (8)

/** Bytes one particle of a pool costs: its channels, its slot and its alive entry.
 *
 * The position and the velocity keep their full precision, sixteen bytes each
 * with the age and the lifetime in their w, and what only reaches the image is
 * packed into half floats: the color with its opacity into two words and the
 * rotation with the size into one. The dead list and the compacted alive list
 * add a word each.
 */
#define BOX_PARTICLE_BYTES (52)

/** First word of the draw arguments of the quad pass, four of them */
#define BOX_PARTICLE_ARG_DRAW (0)

/** First word of the draw arguments of the billboard pass, five of them */
#define BOX_PARTICLE_ARG_INDEXED (4)

/** Words the argument buffer of a pool holds, the arguments of its two draws.
 *
 * The arguments live apart from the counters because a buffer a draw reads its
 * instance count from may not be bound to the pass that writes it; only the
 * finish pass takes it, in a bind group of its own.
 */
#define BOX_PARTICLE_ARGS (12)

/** Spawn point of an emitter without a volume, its origin */
#define BOX_PARTICLE_POINT (0)

/** Spawn point drawn from the sphere of the radius */
#define BOX_PARTICLE_SPHERE (1)

/** Spawn point drawn from the box of the extents */
#define BOX_PARTICLE_BOX (2)

/** Spawn point drawn from the disc of the radius in the XZ plane */
#define BOX_PARTICLE_DISC (3)

/** Spawn point drawn from the surface table of a mesh */
#define BOX_PARTICLE_MESH (4)

/** Spawn point drawn from the sample table of a curve */
#define BOX_PARTICLE_SPLINE (5)

/** The pool of the emitter is emptied before the frame spawns into it */
#define BOX_PARTICLE_RESET (1u)

/** The particles of the emitter collide with the horizontal ground plane */
#define BOX_PARTICLE_GROUND (2u)

/** A particle of the emitter dies where it hits instead of bouncing */
#define BOX_PARTICLE_DIE (4u)

/** Colliders one emitter tests its particles against, the widest the table holds */
#define BOX_PARTICLE_COLLIDERS (16)

/** Collider of the table that is a sphere around its center */
#define BOX_PARTICLE_COLLIDER_SPHERE (0.0f)

/** Collider of the table that is the axis aligned box of its half extents */
#define BOX_PARTICLE_COLLIDER_BOX (1.0f)

/** The particles of the emitter are added to the image instead of blended over it */
#define BOX_PARTICLE_ADDITIVE (8u)

/** The particles of the emitter fade out against the geometry behind them */
#define BOX_PARTICLE_SOFT (16u)

/** The playback of the emitter is paused: its pool is drawn but never stepped */
#define BOX_PARTICLE_PAUSED (32u)

/** The particles of the emitter are drawn as a soft round sprite */
#define BOX_PARTICLE_DRAW_CIRCLE (0)

/** The particles of the emitter are drawn as a frame of their texture */
#define BOX_PARTICLE_DRAW_SPRITE (1)

/** The particles of the emitter are drawn as the mesh of the emitter */
#define BOX_PARTICLE_DRAW_BILLBOARD (2)

/** Keys one curve of an emitter holds, evenly spread over the life of a particle */
#define BOX_PARTICLE_KEYS (4)

/** One entry of the table of an emitter, a spawn point or a collider.
 *
 * The spawn points of the shape come first, in the space of the transform of
 * the emitter, and the colliders the particles bounce off follow them in world
 * space: their center sits in the position with the shape constant in its w and
 * their half extents in the normal, which is the radius in every component of a
 * sphere. Both live in one table because the simulation pass already binds the
 * eight storage buffers a device guarantees.
 */
typedef struct box_particle_point_t {
    float position[4]; /**< Point of the surface or the curve, or the center of a collider */
    float normal[4];   /**< Unit direction the point emits along, or the half extents */
} box_particle_point_t;

/** Parameters of one emitter, the only particle data the packet carries.
 *
 * The fields up to the padding are the uniform block the spawn and integrate
 * passes read and must keep their layout; the fields after it stay on the CPU
 * and tell the renderer which pool the block belongs to and where its shape
 * table sits in the packet.
 */
typedef struct box_particle_emitter_t {
    float transform[12];    /**< Rows of the 3x4 world matrix of the emitter */
    float color[4];         /**< Lowest linear color a particle starts with, w unused */
    float color_max[4];     /**< Highest linear color a particle starts with, w unused */
    float acceleration[4];  /**< Constant acceleration and gravity, w the drag per second */
    float wind[4];          /**< Velocity the drag pulls towards, w the turbulence */
    float collision[4];     /**< Height of the ground, the bounce and the friction, w unused */
    float direction[4];     /**< Emission direction in emitter space, w its spread cosine */
    float extents[4];       /**< Half extents of the box shape, w the radius of the others */
    float size_curve[4];    /**< Keys of the size multiplier over the normalised age */
    float opacity_curve[4]; /**< Keys of the opacity multiplier over the normalised age */
    float turn_curve[4];    /**< Keys of the turns added to the rotation over the age */
    float gradient[BOX_PARTICLE_KEYS][4]; /**< Linear colors the particle passes through */
    float sheet[4];         /**< Sheet columns and rows, its frames per second and the softness */
    float speed[2];         /**< Lowest and highest initial speed */
    float lifetime[2];      /**< Lowest and highest lifetime in seconds */
    float size[2];          /**< Lowest and highest initial size */
    float rotation[2];      /**< Lowest and highest initial rotation in radians */
    float opacity[2];       /**< Lowest and highest initial opacity */
    float delta;            /**< Seconds the simulation advances this frame */
    float stretch;          /**< World units a sprite grows along its velocity per unit of speed */
    uint32_t max;           /**< Particles the pool holds */
    uint32_t spawn;         /**< Particles the frame spawns, already clamped to the pool */
    uint32_t first;         /**< Particles the emitter spawned before this frame */
    uint32_t shape;         /**< One of the BOX_PARTICLE shape constants */
    uint32_t seed;          /**< Seed the random draw of a particle is hashed from */
    uint32_t point_count;   /**< Points of its shape table, 0 for a shape without one */
    uint32_t flags;         /**< BOX_PARTICLE_RESET and the playback bits above it */
    uint32_t collider_count; /**< Colliders of its table, which follow its shape points */
    uint32_t appearance;    /**< One of the BOX_PARTICLE_DRAW constants */
    uint32_t texture;       /**< Packed texture slot of a sprite, 0 for none */
    uint32_t pad2;          /**< Padding to the 16 byte GPU stride */
    uint32_t pad3;          /**< Padding to the 16 byte GPU stride */
    uint32_t id;            /**< Pool of the emitter, the low word of its entity */
    uint32_t point_first;   /**< First point of its shape table in the packet */
    uint32_t mesh;          /**< Mesh handle a billboard is drawn as, 0 for a quad */
    float origin[3];        /**< World position of the emitter, which sorts its pool */
    float reach;            /**< Radius its particles stay inside, which culls the emitter */
    uint32_t span;          /**< Slots of its pool that have ever held a particle */
} box_particle_emitter_t;

/** Bytes of an emitter that the uniform buffer of its pool holds */
#define BOX_PARTICLE_UNIFORM_SIZE (offsetof(box_particle_emitter_t, id))

/** One end of a debug line segment, two of them making a segment.
 *
 * The color is display encoded RGBA in one word, which the line pass converts
 * to the space of the target it writes, so a producer of lines never has to
 * know whether the view renders into a float or an eight bit image.
 */
typedef struct box_line_vertex_t {
    float pos[3];    /**< End of the segment in world space */
    uint32_t color;  /**< Display encoded RGBA, red in the low byte */
} box_line_vertex_t;

/** All work the renderer performs for one frame */
typedef struct box_packet_t {
    uint32_t frame;                          /**< Frame number */
    double time;                             /**< Seconds since start */
    float delta;                             /**< Seconds the frame advanced the world */
    uint32_t width;                          /**< Target width in pixels */
    uint32_t height;                         /**< Target height in pixels */
    uint32_t scaled_width;                   /**< Width the surface views render at */
    uint32_t scaled_height;                  /**< Height the surface views render at */
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
    box_vec_t shader_create;                 /**< box_shader_cmd_t of the shaders that changed */
    box_vec_t effect_texture_create;
    uint32_t layer_count;                    /**< Layers the scene has registered */
    box_layer_params_t layer_params[BOX_LAYER_MAX]; /**< Parameters of every layer */
    uint32_t modulator_count;                /**< Light modulators the scene has registered */
    box_modulator_params_t modulator_params[BOX_MODULATOR_MAX]; /**< Block of every modulator */
    box_vec_t static_updates;                /**< box_static_update_t */
    box_vec_t static_counts;                 /**< box_static_count_t */
    box_vec_t static_groups;                 /**< box_static_group_t of the meshes that regrouped */
    box_vec_t dynamic_batches;               /**< box_dynamic_batch_t */
    box_vec_t dynamic_instances;             /**< box_instance_t */
    box_vec_t transparent;                   /**< box_transparent_t, sorted back to front */
    box_vec_t transitions;                   /**< box_transition_cmd_t of the frame */
    box_vec_t transition_records;            /**< box_transition_rec_t the commands point into */
    box_vec_t transitions_done;              /**< box_transition_done_t the renderer wrote back */
    box_lod_depth_t lod_depth;               /**< Coarse depth the renderer read back */
    box_vec_t effects;                       /**< box_effect_t of the chain of every view */
    box_vec_t lights;                        /**< box_light_t inside the view frustum */
    box_vec_t bake_lights;                   /**< box_light_t of every static light, unculled */
    box_vec_t emitters;                      /**< box_emitter_t of every static emissive instance */
    box_vec_t lines;                         /**< box_line_vertex_t, two per debug line segment */
    box_vec_t particles;                     /**< box_particle_emitter_t of every emitter */
    box_vec_t particle_points;               /**< box_particle_point_t of the shape tables */
    uint32_t particle_sim_shader;            /**< Shader handle of the spawn and integrate passes */
    uint32_t particle_draw_shader;           /**< Shader handle of the point sprite pass */
    box_bake_t bake;                         /**< Settings of the baked lighting pass */
    box_sky_t sky;                           /**< Atmosphere of the frame, inactive without one */
    uint32_t sky_lut_shader;                 /**< Shader handle of the sky table passes */
    uint32_t sky_shader;                     /**< Shader handle of the sky and haze pass */
} box_packet_t;

/** Initialize the vectors of a packet.
 *
 * @param p The packet
 */
static inline void box_packet_init(
    box_packet_t *p)
{
    memset(p, 0, sizeof(box_packet_t));
    p->sky_lut_shader = BOX_SHADER_NONE;
    p->sky_shader = BOX_SHADER_NONE;
    p->particle_sim_shader = BOX_SHADER_NONE;
    p->particle_draw_shader = BOX_SHADER_NONE;
    box_vec_init(&p->views, sizeof(box_view_t));
    box_vec_init(&p->target_create, sizeof(box_target_cmd_t));
    box_vec_init(&p->target_destroy, sizeof(uint32_t));
    box_vec_init(&p->mesh_create, sizeof(box_mesh_cmd_t));
    box_vec_init(&p->mesh_destroy, sizeof(uint32_t));
    box_vec_init(&p->texture_create, sizeof(box_texture_cmd_t));
    box_vec_init(&p->texture_destroy, sizeof(uint32_t));
    box_vec_init(&p->shader_create, sizeof(box_shader_cmd_t));
    box_vec_init(&p->effect_texture_create, sizeof(box_effect_texture_cmd_t));
    box_vec_init(&p->static_updates, sizeof(box_static_update_t));
    box_vec_init(&p->static_counts, sizeof(box_static_count_t));
    box_vec_init(&p->static_groups, sizeof(box_static_group_t));
    box_vec_init(&p->dynamic_batches, sizeof(box_dynamic_batch_t));
    box_vec_init(&p->dynamic_instances, sizeof(box_instance_t));
    box_vec_init(&p->transparent, sizeof(box_transparent_t));
    box_vec_init(&p->transitions, sizeof(box_transition_cmd_t));
    box_vec_init(&p->transition_records, sizeof(box_transition_rec_t));
    box_vec_init(&p->transitions_done, sizeof(box_transition_done_t));
    box_vec_init(&p->effects, sizeof(box_effect_t));
    box_vec_init(&p->lights, sizeof(box_light_t));
    box_vec_init(&p->bake_lights, sizeof(box_light_t));
    box_vec_init(&p->emitters, sizeof(box_emitter_t));
    box_vec_init(&p->lines, sizeof(box_line_vertex_t));
    box_vec_init(&p->particles, sizeof(box_particle_emitter_t));
    box_vec_init(&p->particle_points, sizeof(box_particle_point_t));
}

/** Reset per frame fields while keeping vector storage.
 *
 * The transitions the renderer reported as finished and the depth grid it read
 * back survive the reset, because a producer reads both from the packet it just
 * acquired.
 *
 * @param p The packet
 */
static inline void box_packet_reset(
    box_packet_t *p)
{
    p->frame = 0;
    p->time = 0;
    p->delta = 0;
    p->width = 0;
    p->height = 0;
    p->scaled_width = 0;
    p->scaled_height = 0;
    p->capture_path[0] = '\0';
    p->has_view = false;
    p->capture_view = BOX_VIEW_NONE;
    p->sky_lut_shader = BOX_SHADER_NONE;
    p->sky_shader = BOX_SHADER_NONE;
    p->particle_sim_shader = BOX_SHADER_NONE;
    p->particle_draw_shader = BOX_SHADER_NONE;
    memset(&p->view, 0, sizeof(box_view_t));
    memset(&p->sky, 0, sizeof(box_sky_t));
    box_vec_clear(&p->views);
    box_vec_clear(&p->target_create);
    box_vec_clear(&p->target_destroy);
    box_vec_clear(&p->mesh_create);
    box_vec_clear(&p->mesh_destroy);
    box_vec_clear(&p->texture_create);
    box_vec_clear(&p->texture_destroy);
    box_vec_clear(&p->shader_create);
    box_vec_clear(&p->effect_texture_create);
    box_vec_clear(&p->static_updates);
    box_vec_clear(&p->static_counts);
    box_vec_clear(&p->static_groups);
    box_vec_clear(&p->dynamic_batches);
    box_vec_clear(&p->dynamic_instances);
    box_vec_clear(&p->transparent);
    box_vec_clear(&p->transitions);
    box_vec_clear(&p->transition_records);
    box_vec_clear(&p->effects);
    box_vec_clear(&p->lights);
    box_vec_clear(&p->bake_lights);
    box_vec_clear(&p->emitters);
    box_vec_clear(&p->lines);
    box_vec_clear(&p->particles);
    box_vec_clear(&p->particle_points);
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
    box_vec_fini(&p->shader_create);
    box_vec_fini(&p->effect_texture_create);
    box_vec_fini(&p->static_updates);
    box_vec_fini(&p->static_counts);
    box_vec_fini(&p->static_groups);
    box_vec_fini(&p->dynamic_batches);
    box_vec_fini(&p->dynamic_instances);
    box_vec_fini(&p->transparent);
    box_vec_fini(&p->transitions);
    box_vec_fini(&p->transition_records);
    box_vec_fini(&p->transitions_done);
    box_vec_fini(&p->effects);
    box_vec_fini(&p->lights);
    box_vec_fini(&p->bake_lights);
    box_vec_fini(&p->emitters);
    box_vec_fini(&p->lines);
    box_vec_fini(&p->particles);
    box_vec_fini(&p->particle_points);
}

#endif
