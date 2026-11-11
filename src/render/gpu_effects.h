/**
 * @file render/gpu_effects.h
 * @brief Limits, pass descriptions and per target state of the effect passes of gpu_effects.c.
 */

#ifndef BOX_RENDER_GPU_EFFECTS_H
#define BOX_RENDER_GPU_EFFECTS_H

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Color format of the edges SMAA finds */
#define BOX_GPU_SMAA_EDGES_FORMAT (WGPUTextureFormat_RG8Unorm)

/** Color format of the blending weights of SMAA */
#define BOX_GPU_SMAA_WEIGHTS_FORMAT (WGPUTextureFormat_RGBA8Unorm)

/** Bindings of the SMAA passes */
#define BOX_GPU_SMAA_BINDINGS (7)

/** Half resolution images of the depth of field passes */
#define BOX_GPU_DOF_IMAGES (4)

/** Bytes of the uniforms of an effect pass */
#define BOX_GPU_FX_UNIFORMS (256)

/** Bloom mips at most */
#define BOX_GPU_BLOOM_MIPS (12)

/** Smallest edge of the last bloom mip, in pixels, of a chain that is long enough */
#define BOX_GPU_BLOOM_LEAF (4)

/** Prefilter levels of a bloom at most, the halvings of the image above the first mip */
#define BOX_GPU_BLOOM_PREFILTER (12)

/** Invocations along each axis of a workgroup of the bloom shader */
#define BOX_GPU_BLOOM_GROUP (8)

/** Bytes between the blends of the upsample steps, the uniform offset alignment */
#define BOX_GPU_BLOOM_STEP (256)

/** Entry points of the bloom shader */
#define BOX_GPU_BLOOM_ENTRIES (5)

/** Bins of the luminance histogram of the exposure */
#define BOX_GPU_EXPOSURE_BINS (256)

/** Pixels along each axis of a workgroup of the histogram pass */
#define BOX_GPU_EXPOSURE_GROUP (16)

#define BOX_GPU_EXPOSURE_SAMPLES (1u << 21)

/** Stops the exposure is clamped to when no bound is set */
#define BOX_GPU_EXPOSURE_LIMIT (60.0f)

/** Format of the Tony McMapface LUT, a core sampleable and filterable format of WebGPU */
#define BOX_GPU_TONY_FORMAT (WGPUTextureFormat_RGB9E5Ufloat)

/** Kind of state that holds the measured exposure, next to box_render_effect_kind_t */
#define BOX_GPU_FX_MEASURE (100)

/** Kind of state of the pass that copies an HDR image into its target */
#define BOX_GPU_FX_COPY (101)

/** How a pass writes its output */
typedef enum box_gpu_fx_blend_t {
    BOX_GPU_FX_REPLACE,     /**< Replace the output */
    BOX_GPU_FX_OVER,        /**< Premultiplied alpha over the output */
    BOX_GPU_FX_ADD          /**< Add the output times the blend constant */
} box_gpu_fx_blend_t;

/** A render pipeline of an effect shader entry point, format and blend */
typedef struct box_gpu_fx_pipeline_t {
    uint32_t shader;                /**< Shader handle */
    int32_t revision;               /**< Revision of the module the pipeline was made with */
    const char *entry;              /**< Fragment entry point */
    WGPUTextureFormat format;       /**< Format of the output */
    box_gpu_fx_blend_t blend;       /**< How the output is written */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_fx_pipeline_t;

/** Bind groups of the dispatches of a bloom a state keeps, enough for every dispatch of its
 * chain */
#define BOX_GPU_BLOOM_GROUPS (2 * BOX_GPU_BLOOM_MIPS + BOX_GPU_BLOOM_PREFILTER + 4)

/** The bind group of a dispatch of a bloom, kept while its mip chain lives */
typedef struct box_gpu_fx_bloom_group_t {
    WGPUTextureView input;          /**< View it reads */
    WGPUTextureView aux;            /**< Second view it reads, NULL for none */
    WGPUTextureView output;         /**< View it writes */
    int32_t step;                   /**< Step of the upsample it reads the blend of */
    WGPUBindGroup group;            /**< The group */
} box_gpu_fx_bloom_group_t;

/** State of an effect of a view in a target, kept while the effect runs every frame */
typedef struct box_gpu_fx_state_t {
    uint32_t target;                /**< Handle of the target */
    ecs_entity_t view;              /**< View entity */
    ecs_entity_t effect;            /**< Effect entity */
    int32_t kind;                   /**< box_render_effect_kind_t or a BOX_GPU_FX_ kind */
    int64_t frame;                  /**< Frame of the last packet that used the state */
    WGPUBuffer uniforms;            /**< Uniforms of the passes */
    WGPUBuffer histogram;           /**< Luminance histogram of a measured exposure */
    WGPUBuffer exposure;            /**< Adapted exposure */
    WGPUTexture bloom;              /**< Mip chain of a bloom */
    WGPUTextureView mips[BOX_GPU_BLOOM_MIPS]; /**< View of every mip */
    int32_t mip_count;              /**< Mips of the chain */
    uint32_t width;                 /**< Width of the first mip */
    uint32_t height;                /**< Height of the first mip */
    uint32_t render_width;          /**< Render width the chain was made for */
    uint32_t render_height;         /**< Render height the chain was made for */
    WGPUTexture prefilter;          /**< Halvings of the image above the first mip of a bloom,
                                     *   NULL when the first mip is half the image */
    WGPUTextureView prefilter_mips[BOX_GPU_BLOOM_PREFILTER]; /**< View of every level */
    int32_t prefilter_count;        /**< Levels of the prefilter */
    WGPUTexture up;                 /**< Upsampled levels of a bloom, one fewer than the chain */
    WGPUTextureView up_mips[BOX_GPU_BLOOM_MIPS]; /**< View of every upsampled level */
    WGPUBuffer steps;               /**< Blend of each upsample, BOX_GPU_BLOOM_STEP bytes apart */
    WGPUTexture history[2];         /**< First mip of the previous and the current frame */
    WGPUTextureView history_views[2]; /**< Views of the history images */
    int32_t history_index;          /**< History image that holds the last frame */
    bool history_valid;             /**< The history holds a frame */
    box_gpu_fx_bloom_group_t groups[BOX_GPU_BLOOM_GROUPS]; /**< Bind groups of the dispatches
                                     *   of a bloom */
    int32_t group_count;            /**< Bind groups kept */
} box_gpu_fx_state_t;

/** HDR images of a target, the size it renders at */
typedef struct box_gpu_fx_target_t {
    uint32_t target;                /**< Handle of the target */
    int64_t frame;                  /**< Frame of the last packet that used the images */
    int32_t width;                  /**< Render width */
    int32_t height;                 /**< Render height */
    WGPUTexture hdr[2];             /**< The images a chain alternates between */
    WGPUTextureView hdr_views[2];   /**< Views of the images */
    WGPUTexture smaa_edges;         /**< Edges of SMAA, created on first use */
    WGPUTextureView smaa_edges_view; /**< View of the edges */
    WGPUTexture smaa_weights;       /**< Blending weights of SMAA, created on first use */
    WGPUTextureView smaa_weights_view; /**< View of the blending weights */
    WGPUTexture dof[BOX_GPU_DOF_IMAGES]; /**< Half resolution images of the depth of field,
                                     *   created on first use */
    WGPUTextureView dof_views[BOX_GPU_DOF_IMAGES]; /**< Views of the depth of field images */
} box_gpu_fx_target_t;

struct box_gpu_effects_t {
    WGPUBindGroupLayout layout;     /**< Bindings of the fullscreen effect passes */
    WGPUPipelineLayout pipeline_layout; /**< Pipeline layout of the fullscreen passes */
    WGPUBindGroupLayout smaa_layout; /**< Bindings of the SMAA passes */
    WGPUPipelineLayout smaa_pipeline_layout; /**< Pipeline layout of the SMAA passes */
    WGPUBindGroupLayout measure_layout; /**< Bindings of the exposure compute passes */
    WGPUPipelineLayout measure_pipeline_layout; /**< Pipeline layout of the compute passes */
    WGPUComputePipeline histogram;  /**< Pipeline that fills the luminance histogram */
    WGPUComputePipeline adapt;      /**< Pipeline that adapts the exposure */
    int32_t measure_revision;       /**< Revision of the exposure shader of those pipelines */
    WGPUBindGroupLayout bloom_layout; /**< Bindings of the bloom compute passes */
    WGPUPipelineLayout bloom_pipeline_layout; /**< Pipeline layout of the bloom passes */
    WGPUComputePipeline bloom[BOX_GPU_BLOOM_ENTRIES]; /**< Pipeline of each bloom entry point */
    int32_t bloom_revision;         /**< Revision of the bloom shader of those pipelines */
    WGPUSampler sampler;            /**< Linear clamp sampler */
    WGPUTexture placeholder;        /**< 1x1 image bound where a pass reads no second image */
    WGPUTextureView placeholder_view; /**< View of the placeholder image */
    WGPUTexture placeholder_3d;     /**< 1x1x1 volume bound where a pass reads no LUT */
    WGPUTextureView placeholder_3d_view; /**< View of the placeholder volume */
    WGPUTexture smaa_area;          /**< Area texture of SMAA, uploaded on first use */
    WGPUTextureView smaa_area_view; /**< View of the area texture */
    WGPUTexture smaa_search;        /**< Search texture of SMAA, uploaded with the area */
    WGPUTextureView smaa_search_view; /**< View of the search texture */
    WGPUTexture tony;               /**< The Tony McMapface LUT, loaded on first use */
    WGPUTextureView tony_view;      /**< View of the LUT */
    bool tony_failed;               /**< The LUT could not be loaded, which was logged once */
    WGPUBuffer unit_exposure;       /**< Exposure of 0 stops, bound where a pass scales nothing */
    WGPUBuffer no_uniforms;         /**< Uniforms bound where a pass has none */
    ecs_vec_t pipelines;            /**< box_gpu_fx_pipeline_t */
    ecs_vec_t states;               /**< box_gpu_fx_state_t* */
    ecs_vec_t targets;              /**< box_gpu_fx_target_t* */
    box_gpu_fx_target_t *current;   /**< HDR images of the view being rendered */
};

/** One fullscreen effect pass: its pipeline, inputs and output */
typedef struct box_gpu_fx_pass_t {
    uint32_t shader;                /**< Shader handle */
    const char *vertex;             /**< Vertex entry point, NULL for vs */
    const char *entry;              /**< Fragment entry point */
    WGPUTextureView input;          /**< Image at binding 0 */
    WGPUTextureView aux;            /**< Image at binding 4, NULL for the placeholder */
    WGPUBuffer uniforms;            /**< Uniforms at binding 2, NULL for none */
    WGPUBuffer exposure;            /**< Exposure at binding 5, NULL for 0 stops */
    WGPUTextureView lut;            /**< Volume at binding 6, NULL for the placeholder */
    WGPUTextureView depth;          /**< Depth at binding 3 */
    WGPUTextureView output;         /**< Image the pass writes */
    WGPUTextureFormat format;       /**< Format of the output */
    box_gpu_fx_blend_t blend;       /**< How the output is written */
    float constant;                 /**< Blend constant of BOX_GPU_FX_ADD */
} box_gpu_fx_pass_t;

/** Where the effect of a chain writes and what it reads */
typedef struct box_gpu_fx_ctx_t {
    box_gpu_target_t *t;            /**< The target */
    box_gpu_fx_target_t *ft;        /**< HDR images of the target */
    WGPUCommandEncoder encoder;     /**< The encoder of the frame */
    const box_render_packet_t *packet; /**< The packet */
    const box_render_view_t *v;     /**< The view */
    WGPUTextureView input;          /**< Image so far */
    WGPUTextureView output;         /**< Image the effect writes */
    WGPUTextureFormat format;       /**< Format of the output */
    box_gpu_fx_blend_t blend;       /**< How the effect writes its output */
} box_gpu_fx_ctx_t;

/** Uniforms of the tone map pass, matching Tonemap in etc/shaders/tonemap.wgsl */
typedef struct box_gpu_tonemap_uniforms_t {
    uint32_t flags[4];              /**< Scale by the exposure, operator (0 none, 1 PBR Neutral,
                                     *   2 Tony McMapface), two unused */
} box_gpu_tonemap_uniforms_t;

/** Uniforms of the exposure passes, matching Settings in etc/shaders/exposure.wgsl */
typedef struct box_gpu_measure_uniforms_t {
    float range[4];                 /**< Min log luminance, its range, low and high percentile */
    float adapt[4];                 /**< Speed up, speed down, log2 min and max brightness */
    float target[4];                /**< Log2 target, compensation, log2 min and max exposure */
    float frame[4];                 /**< Frame delta, 1 with a target brightness, two unused */
} box_gpu_measure_uniforms_t;

/** Uniforms of the bloom passes, matching Bloom in etc/shaders/bloom.wgsl */
typedef struct box_gpu_bloom_uniforms_t {
    float threshold[4];             /**< Threshold, threshold - knee, 2 knee, 0.25 / knee */
    float params[4];                /**< Firefly clamp, blend of the first mip, history blend */
    float first[4];                 /**< Size of a texel of the first mip in uv */
} box_gpu_bloom_uniforms_t;

/** Uniforms of the SMAA passes, matching Smaa in etc/shaders/smaa.wgsl */
typedef struct box_gpu_smaa_uniforms_t {
    float metrics[4];               /**< 1 / width, 1 / height, width, height */
    float params[4];                /**< Threshold, search steps, diagonal search steps,
                                     *   corner rounding 0..1 */
} box_gpu_smaa_uniforms_t;

/** Uniforms of the color grading pass, matching Grading in etc/shaders/grading.wgsl */
typedef struct box_gpu_grading_uniforms_t {
    float balance[3][4];            /**< Rows of the white balance matrix times the exposure */
    float params[4];                /**< Contrast, saturation, vibrance, 1 for display range */
    float lift[4];                  /**< Lift, LUT strength */
    float gain[4];                  /**< Gain, slices of the LUT or 0 for none */
    float inv_gamma[4];             /**< 1 / gamma, 1 when lift or gamma change the color */
} box_gpu_grading_uniforms_t;

/** Uniforms of the vignette pass, matching Vignette in etc/shaders/vignette.wgsl */
typedef struct box_gpu_vignette_uniforms_t {
    float params[4];                /**< Intensity, radius, smoothness, roundness */
    float shape[4];                 /**< Center x and y, width over height of the image, unused */
    float color[4];                 /**< Linear color of the vignette, unused */
} box_gpu_vignette_uniforms_t;

/** Uniforms of the chromatic aberration pass, matching ChromaticAberration in
 * etc/shaders/chromatic_aberration.wgsl */
typedef struct box_gpu_chromatic_aberration_uniforms_t {
    float params[4];                /**< Largest offset in pixels, taps, falloff, 1 / distance
                                     *   from the center to its farthest corner in pixels */
    float shape[4];                 /**< Center x and y in pixels, 1 / width, 1 / height */
} box_gpu_chromatic_aberration_uniforms_t;

/** Uniforms of the depth of field passes, matching Dof in etc/shaders/depth_of_field.wgsl */
typedef struct box_gpu_dof_uniforms_t {
    float depth[4];                 /**< View depth of a reversed Z depth d:
                                     *   (x + y * d) / (z * d + w) */
    float focus[4];                 /**< Focus distance, focus range, near and far transition */
    float params[4];                /**< Largest radius in half resolution pixels, 1 when the
                                     *   near field blurs, CoC of a pixel without geometry,
                                     *   unused */
    float tilt[4];                  /**< Band center, band half height, transition, in units
                                     *   of the image height, 1 for a tilt shift */
    float tilt_normal[4];           /**< Sin and cos of the band angle, width over height,
                                     *   unused */
} box_gpu_dof_uniforms_t;

/** Uniforms of the fog pass, matching Fog in etc/shaders/fog.wgsl */
typedef struct box_gpu_fog_uniforms_t {
    mat4 inv_view_proj;             /**< Inverse of the projection times the view matrix */
    float color[4];                 /**< Linear fog color, density */
    float params[4];                /**< Falloff, base height, max opacity, 1 when lit */
    float sun[4];                   /**< Direction toward the sun, 1 when there is one */
    float sun_color[4];             /**< Sun radiance times its inscatter */
    float ambient[4];               /**< Ambient light times its inscatter */
    float atmos[4];                 /**< 1 for the color of the atmosphere, phase g, sun and
                                     *   ambient inscatter */
} box_gpu_fog_uniforms_t;

/** Create a 2D image that passes render into and sample.
 *
 * @param gpu The backend
 * @param width Width in pixels
 * @param height Height in pixels
 * @param format Color format
 * @param mips Mip levels
 * @return The texture
 */
WGPUTexture box_gpu_fx_texture(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format,
    uint32_t mips);

/** Release a texture and its view when they exist.
 *
 * @param texture The texture
 * @param view The view
 */
void box_gpu_fx_release(
    WGPUTexture *texture,
    WGPUTextureView *view);

/** Record a fullscreen effect pass.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param pass The pass
 * @return Whether the pass was recorded, false while its shader has no module
 */
bool box_gpu_fx_draw(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_gpu_fx_pass_t *pass);

/** Get the state of an effect of a view in a target, created on first use.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param effect The effect entity
 * @param kind Kind of state
 * @return The state, marked as used by the packet
 */
box_gpu_fx_state_t* box_gpu_fx_state(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    ecs_entity_t effect,
    int32_t kind);

/** Write the uniforms of a state.
 *
 * @param gpu The backend
 * @param s The state
 * @param data The uniforms
 * @param size Bytes of the uniforms, at most BOX_GPU_FX_UNIFORMS
 */
void box_gpu_fx_uniforms(
    box_gpu_t *gpu,
    box_gpu_fx_state_t *s,
    const void *data,
    size_t size);

/** Release the half resolution images of the depth of field of a target.
 *
 * @param ft The images
 */
void box_gpu_fx_dof_release(
    box_gpu_fx_target_t *ft);

/** Record the depth of field passes: the half resolution image with its circle of confusion,
 * the dilated near CoC, the far and near gathers, their median filter and the composite at full
 * resolution. A largest radius of 0 records nothing.
 *
 * @param gpu The backend
 * @param ctx The chain
 * @param e The pass
 * @return Whether the image was written to the output
 */
bool box_gpu_fx_depth_of_field(
    box_gpu_t *gpu,
    const box_gpu_fx_ctx_t *ctx,
    const box_render_effect_t *e);

#endif

#endif
