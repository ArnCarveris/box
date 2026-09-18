/**
 * @file renderer/renderer_internal.h
 * @brief Private renderer state shared between the renderer source files.
 */

#ifndef BOX_RENDERER_INTERNAL_H
#define BOX_RENDERER_INTERNAL_H

#include "renderer.h"
#include "cluster.h"
#include "bake.h"
#include "queue.h"
#include "gpu.h"

/** Faces of a cubemap */
#define BOX_ENV_FACES (6)

/** Edge length of the environment and prefiltered specular cubemaps */
#define BOX_ENV_SIZE (64)

/** Mip levels of the prefiltered specular cubemap, one per roughness step */
#define BOX_ENV_MIPS (5)

/** Edge length of the irradiance cubemap */
#define BOX_ENV_IRRADIANCE_SIZE (16)

/** Edge length of the split sum BRDF lookup texture */
#define BOX_ENV_BRDF_SIZE (64)

/** Samples the specular prefilter takes per texel */
#define BOX_ENV_SPECULAR_SAMPLES (64)

/** Samples the irradiance convolution takes per texel */
#define BOX_ENV_IRRADIANCE_SAMPLES (128)

/** Samples the BRDF integration takes per texel */
#define BOX_ENV_BRDF_SAMPLES (128)

/** Bytes between the uniforms of two environment passes */
#define BOX_ENV_STRIDE (256)

/** Bindings the environment takes at the start of the shared fragment bind group */
#define BOX_ENV_BINDINGS (4)

/** Passes of a prefilter: one per face of the source, every specular mip and the irradiance */
#define BOX_ENV_PASSES (BOX_ENV_FACES * (BOX_ENV_MIPS + 2))

/** Uniforms of one environment pass, written at a dynamic offset */
typedef struct box_env_uniform_t {
    float sky[4];     /**< Linear radiance of the upper hemisphere */
    float ground[4];  /**< Linear radiance of the lower hemisphere */
    float params[4];  /**< Cube face, roughness, unused, unused */
} box_env_uniform_t;

/** Uniforms shared by the occupancy and probe bake passes */
typedef struct box_bake_uniform_t {
    float grid_min[4];      /**< World minimum corner of the occupancy grid */
    float grid_cell[4];     /**< World size of one voxel, w is its diagonal */
    uint32_t grid_dim[4];   /**< Voxels on each axis, w unused */
    uint32_t light_count;   /**< Static lights in the bake light buffer */
    uint32_t emitter_count; /**< Emitters in the emitter buffer */
    uint32_t steps;         /**< Ray march samples per visibility test */
    uint32_t pad;           /**< Padding to the 16 byte GPU stride */
} box_bake_uniform_t;

/** Descriptor and job count of one mesh, the per mesh uniform of the bake passes */
typedef struct box_bake_info_t {
    uint32_t desc_count;  /**< Static instance slots of the mesh */
    uint32_t job_count;   /**< Probes the bake pass writes this frame */
    uint32_t pad[2];      /**< Padding to the 16 byte GPU stride */
} box_bake_info_t;

/** GPU resources of one mesh and its static instances */
typedef struct box_gpu_mesh_t {
    WGPUBuffer vertices;             /**< Interleaved vertex buffer */
    WGPUBuffer indices;              /**< Index buffer */
    uint32_t index_count;            /**< Indices to draw */
    WGPUBuffer statics;              /**< Static instance buffer */
    uint32_t static_capacity;        /**< Instances the static buffer can hold */
    uint32_t static_count;           /**< Static instances to draw */
    WGPUBuffer probe_descs;          /**< Probe grid descriptor per static slot */
    WGPUBuffer probes;               /**< Probe pool of the mesh, baked spherical harmonics */
    WGPUBuffer probe_jobs;           /**< Probes the bake pass writes this frame */
    WGPUBuffer probe_info;           /**< box_bake_info_t of the mesh */
    WGPUBindGroup probe_group;       /**< Probes and descriptors the mesh shader reads */
    WGPUBindGroup bake_group;        /**< Probe buffers of the bake pass */
    WGPUBindGroup grid_group;        /**< Probe descriptors of the occupancy pass */
    uint32_t desc_capacity;          /**< Descriptors the descriptor buffer can hold */
    uint32_t probe_capacity;         /**< Probes the probe buffer can hold */
    uint32_t job_capacity;           /**< Jobs the job buffer can hold */
    bool alive;                      /**< Handle is in use */
} box_gpu_mesh_t;

/** The 2D array texture of one texture size class */
typedef struct box_gpu_texclass_t {
    WGPUTexture texture;   /**< Array texture holding every layer of the class */
    WGPUTextureView view;  /**< Array view the mesh shader samples */
    WGPUSampler sampler;   /**< Sampler of the class, with the anisotropy setting */
    uint32_t layers;       /**< Layers the array texture holds */
} box_gpu_texclass_t;

#ifdef BOX_PLATFORM_WEB
/** Captures that can wait for their buffer map at the same time */
#define BOX_CAPTURE_PENDING_MAX (2)

/** A capture whose buffer is being mapped by the browser */
typedef struct box_capture_t {
    WGPUBuffer buffer;               /**< Mappable copy of the frame target */
    uint32_t width;                  /**< Image width in pixels */
    uint32_t height;                 /**< Image height in pixels */
    uint32_t stride;                 /**< Bytes per row in the buffer */
    bool bgra;                       /**< Rows need a BGRA to RGBA swap */
    bool busy;                       /**< Slot holds a capture in flight */
    char path[BOX_CAPTURE_PATH_MAX]; /**< PNG the callback writes */
} box_capture_t;
#endif

/** Everything the renderer needs to produce frames */
struct box_renderer_t {
    box_renderer_desc_t desc;        /**< Creation parameters */
    box_queue_t queue;               /**< Packet handoff */
    WGPUInstance instance;           /**< WebGPU instance */
    WGPUSurface surface;             /**< Window surface, NULL when offscreen */
    WGPUAdapter adapter;             /**< Selected adapter */
    WGPUDevice device;               /**< Logical device */
    WGPUQueue gpu_queue;             /**< Device queue */
    WGPUTextureFormat format;        /**< Color target format */
    WGPUTexture offscreen;           /**< Offscreen color target, NULL with a surface */
    WGPUTextureView offscreen_view;  /**< View of the offscreen target */
    WGPUTexture depth;               /**< Depth target */
    WGPUTextureView depth_view;      /**< View of the depth target */
    WGPURenderPipeline pipeline;     /**< Lit instanced mesh pipeline */
    WGPUBindGroupLayout view_layout; /**< Layout of the view uniform bind group */
    WGPUBindGroup view_group;        /**< Bind group with the view uniforms */
    WGPUBuffer view_buffer;          /**< View uniform buffer */
    box_vec_t meshes;                /**< box_gpu_mesh_t indexed by handle */
    WGPUBuffer dynamics;             /**< Shared dynamic instance buffer */
    uint32_t dynamic_capacity;       /**< Instances the dynamic buffer can hold */
    WGPUBuffer lights;               /**< Light storage buffer of the frame */
    uint32_t light_capacity;         /**< Lights the light buffer can hold */
    WGPUBuffer clusters;             /**< Light offset and count per cluster */
    WGPUBuffer light_indices;        /**< Light indices of every cluster */
    WGPUComputePipeline cluster_pipeline; /**< Cluster assignment pass */
    WGPUBindGroupLayout cluster_layout;   /**< Layout of the compute bind group */
    WGPUBindGroup cluster_group;     /**< View, lights and grid of the compute pass */
    WGPUBindGroupLayout light_layout; /**< Layout of the fragment light bind group */
    WGPUBindGroup light_group;       /**< Lights and grid the mesh shader reads */
    box_cluster_t grid;              /**< Grid the cluster buffers were built for */
    WGPUTexture env_source;          /**< Cubemap the environment gradient is rendered into */
    WGPUTextureView env_source_view; /**< Cube view of the environment */
    WGPUTexture env_specular;        /**< Specular cubemap, one mip per roughness step */
    WGPUTextureView env_specular_view; /**< Cube view of the specular mip chain */
    WGPUTexture env_irradiance;      /**< Diffuse irradiance cubemap */
    WGPUTextureView env_irradiance_view; /**< Cube view of the irradiance */
    WGPUTexture env_brdf;            /**< Split sum BRDF lookup table */
    WGPUTextureView env_brdf_view;   /**< View of the lookup table */
    WGPUSampler env_sampler;         /**< Linear sampler of the environment textures */
    WGPUBuffer env_uniform;          /**< Uniforms of every prefilter pass */
    WGPUBindGroupLayout env_pass_layout;   /**< Uniform only layout of the gradient and BRDF */
    WGPUBindGroup env_pass_group;    /**< Uniform bind group of the gradient and BRDF */
    WGPUBindGroupLayout env_filter_layout; /**< Uniform, source cube and sampler layout */
    WGPUBindGroup env_filter_group;  /**< Bind group of the specular and irradiance passes */
    WGPURenderPipeline env_gradient_pipeline;   /**< Sky and ground gradient into a face */
    WGPURenderPipeline env_specular_pipeline;   /**< GGX prefilter of one mip face */
    WGPURenderPipeline env_irradiance_pipeline; /**< Cosine convolution of one face */
    WGPURenderPipeline env_brdf_pipeline; /**< Split sum integration of the lookup table */
    WGPUBindGroupLayout env_layout;  /**< Layout of the environment group of the mesh shader */
    WGPUBindGroup env_group;         /**< Irradiance, specular, lookup table and sampler */
    uint32_t env_generation;         /**< View generation the prefilter was built for */
    box_bake_state_t bake;           /**< Probe descriptors, dirty set and previous inputs */
    WGPUBuffer bake_uniform;         /**< Grid and count uniforms of the bake passes */
    WGPUBuffer bake_lights;          /**< Static lights the bake accumulates */
    uint32_t bake_light_capacity;    /**< Lights the bake light buffer can hold */
    WGPUBuffer bake_emitters;        /**< Emissive static instances the bake accumulates */
    uint32_t bake_emitter_capacity;  /**< Emitters the emitter buffer can hold */
    WGPUBuffer occupancy;            /**< Occupancy bits of the static scene */
    uint32_t occupancy_words;        /**< Words the occupancy buffer holds */
    WGPUBindGroupLayout bake_layout; /**< Uniform, lights, emitters and occupancy */
    WGPUBindGroup bake_group;        /**< Global bind group of the bake pass */
    WGPUBindGroupLayout grid_layout; /**< Uniform and writable occupancy */
    WGPUBindGroup grid_group;        /**< Global bind group of the occupancy pass */
    WGPUBindGroupLayout bake_mesh_layout; /**< Per mesh layout of the bake pass */
    WGPUBindGroupLayout grid_mesh_layout; /**< Per mesh layout of the occupancy pass */
    WGPUBindGroupLayout probe_layout; /**< Probe and descriptor layout the mesh shader reads */
    WGPUBuffer probe_dummy;          /**< Zero probe bound by dynamic draws */
    WGPUBuffer desc_dummy;           /**< Zero descriptor bound by dynamic draws */
    WGPUBindGroup probe_dummy_group; /**< Bind group of the zero probe */
    WGPUComputePipeline bake_pipeline; /**< Probe bake pass */
    WGPUComputePipeline grid_pipeline; /**< Occupancy fill pass */
    box_gpu_texclass_t texclass[BOX_TEXTURE_CLASSES]; /**< One array texture per size class */
    WGPUTexture texture_dummy;       /**< Single texel array texture of an unused class */
    WGPUTextureView texture_dummy_view; /**< View of the single texel array texture */
    bool texture_dirty;              /**< A class texture changed, the bind group is stale */
    uint32_t width;                  /**< Current target width */
    uint32_t height;                 /**< Current target height */
    volatile bool ready;             /**< Device and targets are usable */
    volatile bool failed;            /**< Device creation failed */
    volatile uint32_t frames;        /**< Frames presented */
#ifdef BOX_PLATFORM_WEB
    box_capture_t captures[BOX_CAPTURE_PENDING_MAX]; /**< Captures waiting for their map */
#else
    pthread_t thread;                /**< Renderer thread when threaded */
    bool thread_started;             /**< Thread was created */
#endif
};

/** Create instance, surface, adapter and device; blocks on native.
 *
 * @param r The renderer
 * @return 0 on success, nonzero on failure
 */
int box_device_init(
    box_renderer_t *r);

/** Release device, adapter, surface and instance.
 *
 * @param r The renderer
 */
void box_device_fini(
    box_renderer_t *r);

/** Create or resize the color and depth targets for the packet size.
 *
 * @param r The renderer
 * @param width Target width
 * @param height Target height
 */
void box_device_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height);

/** Create the pipeline of the placeholder triangle.
 *
 * @param r The renderer
 */
void box_frame_init(
    box_renderer_t *r);

/** Render and present one packet.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_frame_render(
    box_renderer_t *r,
    const box_packet_t *p);

/** Apply the resource and instance changes of a packet to GPU buffers.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_scene_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Record draw calls for every mesh with instances.
 *
 * @param r The renderer
 * @param p The packet
 * @param pass The render pass
 */
void box_scene_draw(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPURenderPassEncoder pass);

/** Release all mesh and instance buffers.
 *
 * @param r The renderer
 */
void box_scene_fini(
    box_renderer_t *r);

/** Create the texture samplers and the placeholder array texture.
 *
 * @param r The renderer
 */
void box_texture_init(
    box_renderer_t *r);

/** Describe the texture half of the shared fragment bind group layout.
 *
 * @param out Entries to fill, BOX_TEXTURE_CLASSES times two of them
 * @param base First binding of the texture half
 */
void box_texture_bind_layout(
    WGPUBindGroupLayoutEntry *out,
    uint32_t base);

/** Fill the texture half of the shared fragment bind group.
 *
 * @param r The renderer
 * @param out Entries to fill, BOX_TEXTURE_CLASSES times two of them
 * @param base First binding of the texture half
 */
void box_texture_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base);

/** Upload the textures of a packet, growing the array texture of their class.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_texture_apply(
    box_renderer_t *r,
    const box_packet_t *p);

/** Release the array textures, samplers and bind groups.
 *
 * @param r The renderer
 */
void box_texture_fini(
    box_renderer_t *r);

/** Create the cluster pipeline, its bind group layouts and the light buffer.
 *
 * @param r The renderer
 */
void box_lights_init(
    box_renderer_t *r);

/** Upload the packet lights and rebuild the grid buffers when the settings changed.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_lights_update(
    box_renderer_t *r,
    const box_packet_t *p);

/** Record the compute pass that assigns the lights to clusters.
 *
 * @param r The renderer
 * @param p The packet
 * @param encoder The command encoder
 */
void box_lights_dispatch(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder);

/** Release the light and cluster resources.
 *
 * @param r The renderer
 */
void box_lights_fini(
    box_renderer_t *r);

/** Create the environment textures, pipelines and bind groups and integrate the BRDF.
 *
 * @param r The renderer
 */
void box_environment_init(
    box_renderer_t *r);

/** Prefilter the environment of a packet when its generation changed.
 *
 * @param r The renderer
 * @param p The packet
 * @param encoder The command encoder
 */
void box_environment_update(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder);

/** Rebuild the shared fragment bind group of the environment and the texture classes.
 *
 * @param r The renderer
 */
void box_environment_rebuild(
    box_renderer_t *r);

/** Release the environment textures, pipelines and bind groups.
 *
 * @param r The renderer
 */
void box_environment_fini(
    box_renderer_t *r);

/** Create the bake pipelines, their layouts and the probe bookkeeping.
 *
 * @param r The renderer
 */
void box_bake_init(
    box_renderer_t *r);

/** Apply the bake inputs of a packet and record the occupancy and bake passes.
 *
 * @param r The renderer
 * @param p The packet
 * @param encoder The command encoder
 */
void box_bake_update(
    box_renderer_t *r,
    const box_packet_t *p,
    WGPUCommandEncoder encoder);

/** Bind group a draw of a mesh reads its probes through.
 *
 * @param r The renderer
 * @param mesh Mesh handle, ignored when the mesh has no probes
 * @param baked Draw is a static batch with probes
 * @return The bind group, the zero probe group when the mesh has none
 */
WGPUBindGroup box_bake_group(
    box_renderer_t *r,
    uint32_t mesh,
    bool baked);

/** Release the probe buffers and bind groups of a mesh.
 *
 * @param m The mesh entry
 */
void box_bake_mesh_release(
    box_gpu_mesh_t *m);

/** Release the bake pipelines, buffers and bookkeeping.
 *
 * @param r The renderer
 */
void box_bake_fini(
    box_renderer_t *r);

/** WGSL source of the lit instanced mesh shader */
extern const char *box_mesh_shader_wgsl;

/** WGSL source of the cluster assignment compute shader */
extern const char *box_cluster_shader_wgsl;

/** WGSL source of the environment gradient and BRDF integration shader */
extern const char *box_env_shader_wgsl;

/** WGSL source of the specular prefilter and irradiance convolution shader */
extern const char *box_env_filter_wgsl;

/** WGSL source of the occupancy grid fill shader */
extern const char *box_bake_grid_wgsl;

/** WGSL source of the probe bake shader */
extern const char *box_bake_shader_wgsl;

/** Read back a texture and write it as PNG.
 *
 * @param r The renderer
 * @param texture Texture with CopySrc usage
 * @param path Output file
 * @return 0 on success, nonzero on failure
 */
int box_capture_texture(
    box_renderer_t *r,
    WGPUTexture texture,
    const char *path);

/** Write an RGBA8 image as PNG.
 *
 * @param path Output file
 * @param width Image width
 * @param height Image height
 * @param stride Bytes per row
 * @param pixels Pixel data
 * @return 0 on success, nonzero on failure
 */
int box_image_write_png(
    const char *path,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    const void *pixels);

#endif
