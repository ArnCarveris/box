/**
 * @file render/render.h
 * @brief Render packets, the backend interface and the packet ring of the box.render sources.
 *
 * Main thread modules append to the packet of the frame during the RenderExtract phase; the
 * backend reads a submitted packet on the render thread, or on the main thread in a browser.
 */

#ifndef BOX_RENDER_INTERNAL_H
#define BOX_RENDER_INTERNAL_H

#include <box.h>

/** Byte alignment of every block in the data of a packet */
#define BOX_RENDER_DATA_ALIGN (16)

/** Shader handle of the engine shader that scales a scene texture to the presented image */
#define BOX_RENDER_SHADER_BLIT (1)

/** Shader handle of the engine shader of the forward pass that draws meshes */
#define BOX_RENDER_SHADER_MESH (2)

/** Shader handle of the engine compute shader that scatters changed instances into slots */
#define BOX_RENDER_SHADER_SCATTER (3)

/** Shader handle of the engine compute shader that assigns the lights of a view to clusters */
#define BOX_RENDER_SHADER_CLUSTER (4)

/** Shader handle of the engine shader that draws the casters into the cascades of a view */
#define BOX_RENDER_SHADER_SHADOW (5)

/** Shader handle of the engine compute shader that culls the instances of a view */
#define BOX_RENDER_SHADER_CULL (6)

/** Projected size in pixels below which a capture of a camera without a view culls */
#define BOX_RENDER_CULL_MIN_DIMENSION (1.0f)

/** Shader handle of the engine shader of the tone map and exposure passes and the effect output */
#define BOX_RENDER_SHADER_TONEMAP (40)

/** Shader handle of the engine compute shader that measures and adapts the exposure */
#define BOX_RENDER_SHADER_EXPOSURE (41)

/** Shader handle of the engine shader of the bloom passes */
#define BOX_RENDER_SHADER_BLOOM (42)

/** Shader handle of the engine shader of the ambient occlusion passes */
#define BOX_RENDER_SHADER_SSAO (43)

/** Shader handle of the engine shader of the height fog pass */
#define BOX_RENDER_SHADER_FOG (44)

/** Cascades of the shadow map of a view at most */
#define BOX_RENDER_SHADOW_CASCADES (4)

/** PCF taps per axis of a shadow lookup at most */
#define BOX_RENDER_SHADOW_FILTER_MAX (7)

/** Shader handle of the engine shader of the forward pass that draws the 2D shapes */
#define BOX_RENDER_SHADER_SHAPE2D (7)

/** Shader handle of the engine compute shader that projects probe captures onto SH */
#define BOX_RENDER_SHADER_BAKE (8)

/** Buffer handle of the change buffer the scatter pass reads changed instances from */
#define BOX_RENDER_BUFFER_CHANGES (1)

/** First buffer handle of the instance buffers of batches */
#define BOX_RENDER_BUFFER_FIRST (16)

/** Byte alignment of a scatter segment in the change buffer, the storage offset alignment */
#define BOX_RENDER_SCATTER_ALIGN (256)

/** Words of the header of a scatter segment: count, words per instance and two unused */
#define BOX_RENDER_SCATTER_HEADER (4)

/** First buffer handle of the light buffers of scenes */
#define BOX_RENDER_BUFFER_LIGHT_FIRST (0x20000000u)

/** First target handle of captures, the handles below belong to surfaces */
#define BOX_RENDER_CAPTURE_TARGET_FIRST (0x40000000u)

/** First shader handle of application shaders, the handles below are engine shaders */
#define BOX_RENDER_SHADER_FIRST (64)

/** Texture layers of a batch: albedo, emissive, roughness and normal, as in PbrTextures */
#define BOX_RENDER_TEXTURE_SLOTS (4)

/** Anisotropic samples of the texture samplers when no surface sets them */
#define BOX_RENDER_ANISOTROPY_DEFAULT (4)

/** Bytes of texture images a packet carries before the rest waits for the next frame */
#define BOX_RENDER_TEXTURE_BUDGET (32 * 1024 * 1024)

/** Flag of a texture handle that samples the image of the target in its low bits */
#define BOX_RENDER_TEXTURE_TARGET (0x80000000u)

/** Bytes of packet data a reused packet keeps, a larger block is freed when it is reset */
#define BOX_RENDER_DATA_KEEP (16 * 1024 * 1024)

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
    BOX_RENDER_VOLUME    /**< Irradiance probe volume of a scene with LightBake */
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
 */
typedef struct box_render_light_t {
    float position[3];  /**< World position, the direction toward a directional light */
    float range;        /**< Distance at which the light reaches zero, 0 for directional */
    float color[3];     /**< Linear color times intensity times pi */
    float spot_scale;   /**< Cone falloff scale, 1 / (cos inner - cos outer) */
    float direction[3]; /**< Direction a spot light shines in */
    float spot_offset;  /**< Cone falloff offset, -cos outer times spot_scale */
} box_render_light_t;

/** The lights a view is shaded with: the light buffer of its scene and its ambient light */
typedef struct box_render_view_lights_t {
    bool enabled;            /**< Shade with the lights, else draw base color and emission */
    uint32_t buffer;         /**< Handle of the light buffer, 0 for the default sun */
    uint32_t directional;    /**< Directional lights at the start of the buffer */
    uint32_t count;          /**< Lights in the buffer, the directional ones included */
    float ambient_sky[4];    /**< Linear ambient light from above */
    float ambient_ground[4]; /**< Linear ambient light from below */
    float sun[3];            /**< Direction toward the first directional light, if any */
    float sun_color[3];      /**< Linear color times intensity times pi of that light */
} box_render_view_lights_t;

/** The shadow settings of a view, resolved, with the view depths its cascades end at.
 *
 * The first directional light of the scene casts the shadows; the cascades are fitted to the
 * target the view renders into, because the frustum follows its aspect.
 */
typedef struct box_render_view_shadows_t {
    bool enabled;           /**< The view renders shadows and its scene has a directional light */
    int32_t cascades;       /**< Cascades, 1 to BOX_RENDER_SHADOW_CASCADES */
    int32_t resolution;     /**< Edge of a cascade in texels */
    int32_t filter_size;    /**< PCF taps per axis, 1 to BOX_RENDER_SHADOW_FILTER_MAX */
    float depth_bias;       /**< Depth offset of the comparison in texels */
    float normal_offset;    /**< Offset of the lookup along the normal in texels */
    float fade;             /**< Fraction of the last cascade that fades out */
    float near_;            /**< View depth the first cascade starts at */
    float splits[BOX_RENDER_SHADOW_CASCADES]; /**< View depth each cascade ends at */
    float direction[3];     /**< Direction toward the light that casts the shadows */
} box_render_view_shadows_t;

/** The culling of a view, resolved from its CullSettings */
typedef struct box_render_view_cull_t {
    bool enabled;           /**< Instances outside the frustum or too small are not drawn */
    bool occlusion;         /**< Instances hidden behind what the view drew are not drawn */
    float min_dimension;    /**< Projected size in pixels below which an instance is culled */
} box_render_view_cull_t;

/** A view of the frame: the camera it renders, the scene it draws and how it clears.
 *
 * The projection is built by the renderer per target, because its aspect follows the
 * surface; views are in render order, the order in which they were created.
 */
typedef struct box_render_view_t {
    ecs_entity_t view;          /**< Entity with the View component */
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
    uint32_t volume;            /**< Probe volume the view samples, 0 for the hemisphere ambient */
    uint32_t volume_buffer;     /**< Probe buffer of the volume the view samples, 0 or 1 */
} box_render_view_t;

/** Kind of an effect pass of a view */
typedef enum box_render_effect_kind_t {
    BOX_RENDER_EFFECT_TONEMAP,  /**< Tonemap, optionally scaled by an adapted exposure first */
    BOX_RENDER_EFFECT_EXPOSURE, /**< AutoExposure on its own: scales by the adapted exposure */
    BOX_RENDER_EFFECT_BLOOM,    /**< Bloom */
    BOX_RENDER_EFFECT_SSAO,     /**< Ssao */
    BOX_RENDER_EFFECT_FOG       /**< HeightFog */
} box_render_effect_kind_t;

/** An effect pass of a view with its settings resolved, in the order of View.effects */
typedef struct box_render_effect_t {
    box_render_effect_kind_t kind; /**< Kind of pass */
    ecs_entity_t entity;        /**< Effect entity, keys the state the pass keeps across frames */
    ecs_entity_t exposure;      /**< AutoExposure entity whose adapted exposure scales, 0 none */
    AutoExposure auto_exposure; /**< Resolved exposure settings when exposure is set */
    Bloom bloom;                /**< Resolved settings of a bloom pass */
    Ssao ssao;                  /**< Resolved settings of an ambient occlusion pass */
    HeightFog fog;              /**< Resolved settings of a fog pass */
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
    bool texture;           /**< Materials sample the image, so it renders before other targets */
} box_render_target_desc_t;

/** Description of a BOX_RENDER_MESH create, followed by its vertices and its indices.
 *
 * The vertices are box_vertex_t, the indices uint32_t with three per counter-clockwise
 * triangle. A mesh does not change under its handle; it is destroyed and created again.
 */
typedef struct box_render_mesh_desc_t {
    uint32_t vertex_count;  /**< Vertices after the description */
    uint32_t index_count;   /**< Indices after the vertices */
} box_render_mesh_desc_t;

/** Description of a BOX_RENDER_TEXTURE create, followed by its RGBA8 pixels row by row.
 *
 * The first row is the top of the image, at v 0. Creating an existing handle replaces the
 * image; the backend generates the mip levels.
 */
typedef struct box_render_texture_desc_t {
    uint32_t width;         /**< Width in pixels */
    uint32_t height;        /**< Height in pixels */
} box_render_texture_desc_t;

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
 * of the scene, the sun shadowed by one cascade over the bounds, the ambient term from the
 * source buffer (black for the first pass), and is written into the target buffer.
 *
 * Texels no shape covers see the hemisphere ambient. Captures run before the targets, in the
 * order of the packet, so a pass may read the buffer an earlier batch of the packet wrote.
 */
typedef struct box_render_bake_batch_t {
    uint32_t volume;        /**< Handle of the volume the probes are written into */
    uint32_t generation;    /**< Bake of the volume the probes belong to, reported back */
    int32_t pass;           /**< Pass of the bake, 0 bakes direct light and emission */
    int32_t source;         /**< Probe buffer the faces shade with, or BOX_RENDER_BAKE_BLACK */
    int32_t target;         /**< Probe buffer the probes are written into, 0 or 1 */
    ecs_entity_t scene;     /**< Scene root of the batches the faces draw */
    int32_t size;           /**< Edge of a face in texels */
    box_render_view_lights_t lights; /**< Lights of the scene, enabled */
    float bounds_min[3];    /**< Minimum corner of the static bounds of the scene */
    float bounds_max[3];    /**< Maximum corner of the static bounds of the scene */
    int32_t first;          /**< First probe of the batch in the probes of the packet */
    int32_t count;          /**< Probes of the batch */
} box_render_bake_batch_t;

/** How a buffer is used, flags of box_render_buffer_desc_t */
typedef enum box_render_buffer_usage_t {
    BOX_RENDER_BUFFER_INSTANCE = 1 << 0, /**< Per instance vertex data of batches */
    BOX_RENDER_BUFFER_STORAGE = 1 << 1   /**< Read and written by compute shaders */
} box_render_buffer_usage_t;

/** Description of a BOX_RENDER_BUFFER create; creating an existing handle replaces it */
typedef struct box_render_buffer_desc_t {
    uint32_t size;          /**< Bytes of the buffer */
    uint32_t usage;         /**< box_render_buffer_usage_t flags */
} box_render_buffer_desc_t;

/** Instance data of the forward pass, one per drawn instance in an instance buffer.
 *
 * The shader derives the normal matrix from the model matrix, so an instance holds the
 * transform and the material constants only.
 */
typedef struct box_render_instance_t {
    mat4 model;             /**< World transform with the instance scale, affine */
    vec4 color;             /**< Linear base color, alpha is the opacity with its ancestors */
    vec4 emissive;          /**< Linear emitted color times its strength, w unused */
    vec4 material;          /**< Metallic, roughness, z and w unused */
    vec4 uv;                /**< TextureTransform: u scale, v scale, u offset, v offset */
} box_render_instance_t;

/** Compute pass that copies changed instances from the change buffer into their slots.
 *
 * The segment at offset holds BOX_RENDER_SCATTER_HEADER words (count, words per instance)
 * followed by count entries of a slot index and the words of the instance.
 */
typedef struct box_render_scatter_t {
    uint32_t buffer;        /**< Handle of the instance buffer the slots are in */
    uint32_t changes;       /**< Handle of the buffer holding the segment */
    uint32_t offset;        /**< Byte offset of the segment, a BOX_RENDER_SCATTER_ALIGN multiple */
    uint32_t size;          /**< Bytes of the segment */
    uint32_t count;         /**< Instances the segment holds */
} box_render_scatter_t;

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

/** Instanced draw of one mesh with one pipeline, drawn by the views of its scene.
 *
 * Opaque batches come first in a packet; the blended instances follow as draws of one view,
 * sorted back to front, so they draw after the opaque ones in the order of the packet.
 */
typedef struct box_render_batch_t {
    int32_t view;               /**< Index of the view that draws it, -1 for every view */
    ecs_entity_t scene;         /**< Scene root of the instances, 0 for the default scene */
    uint32_t pipeline;          /**< Shader handle of the pipeline */
    uint32_t blend;             /**< BlendMode of the pipeline, Opaque writes depth */
    uint32_t back_face;         /**< BackFaceMode of the pipeline, BackFaceCull culls */
    bool shadow;                /**< The instances cast shadows, false for NoShadow */
    bool bake;                  /**< Static 3D instances without NoBake, drawn by probe captures */
    uint32_t mesh;              /**< Mesh handle */
    uint32_t textures[BOX_RENDER_TEXTURE_SLOTS]; /**< Texture handles of the layers, 0 for none */
    uint32_t wrap;              /**< TextureWrap of the samplers */
    uint32_t instances;         /**< Buffer handle holding the instance data */
    uint32_t first_instance;    /**< First instance in the instance buffer */
    uint32_t instance_count;    /**< Instances to draw */
} box_render_batch_t;

/** Everything the renderer needs of one frame, filled on the main thread.
 *
 * The vectors keep their storage when the packet is reused, so a frame that needs no more
 * than an earlier one does not allocate. Resources apply first, then uploads, then the
 * scatters, then every target is cleared, drawn by its views and presented.
 */
typedef struct box_render_packet_t {
    int64_t frame;          /**< Frame the packet was extracted in */
    double time;            /**< World time at the end of the frame, in seconds */
    float delta;            /**< Seconds the frame advanced the world */
    uint32_t anisotropy;    /**< Anisotropic samples of the texture samplers, 1 to 16 */
    ecs_vec_t resources;    /**< box_render_resource_t in the order they were made */
    ecs_vec_t uploads;      /**< box_render_upload_t in the order they were made */
    ecs_vec_t scatters;     /**< box_render_scatter_t, run after the uploads */
    ecs_vec_t views;        /**< box_render_view_t in render order */
    ecs_vec_t batches;      /**< box_render_batch_t */
    ecs_vec_t targets;      /**< box_render_target_t in the order they render */
    ecs_vec_t effects;      /**< box_render_effect_t, the effect passes of the views */
    ecs_vec_t bakes;        /**< box_render_bake_batch_t, the probe captures of the frame */
    ecs_vec_t probes;       /**< box_render_probe_capture_t of the bake batches */
    ecs_vec_t data;         /**< Bytes the resources and uploads point into */
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
    int64_t resources;          /**< Resource changes received in total */
    int64_t bytes;              /**< Upload bytes received in total */
    int64_t targets;            /**< Targets received in total */
    box_render_packet_t last;   /**< Copy of the last packet */
} box_render_null_t;

/** Counters of the GPU backend, written on the render thread */
typedef struct box_render_gpu_stats_t {
    bool ready;                 /**< The device exists */
    bool failed;                /**< Creating the device failed */
    int32_t targets;            /**< Targets alive */
    int32_t created;            /**< Targets created */
    int32_t configured;         /**< Times a target was configured, creation included */
    int32_t destroyed;          /**< Targets released */
    int32_t frames;             /**< Target frames rendered */
    int32_t captures;           /**< PNG files written */
    int32_t errors;             /**< Device errors and failed captures */
    int32_t width;              /**< Width of the last configured target */
    int32_t height;             /**< Height of the last configured target */
    int32_t render_width;       /**< Render width of the last configured target */
    int32_t render_height;      /**< Render height of the last configured target */
    int32_t shaders;            /**< Shader modules alive */
    int32_t compiles;           /**< Shader modules compiled */
    int32_t shader_errors;      /**< Shader modules that failed to compile */
    int32_t meshes;             /**< Meshes alive */
    int32_t buffers;            /**< Buffers alive */
    int32_t passes;             /**< View passes rendered */
    int32_t draws;              /**< Draw calls recorded */
    int32_t scatters;           /**< Scatter dispatches recorded */
    int32_t scattered;          /**< Instances the scatter dispatches copied */
    int32_t probes;             /**< Probes captured */
} box_render_gpu_stats_t;

/** GPU time of the probe captures the GPU backend measured since the last time it was taken */
typedef struct box_render_bake_timing_t {
    int32_t probes;             /**< Probes the measured capture batches held */
    double ms;                  /**< Milliseconds the GPU spent on them */
    bool measured;              /**< The device measures with timestamp queries */
} box_render_bake_timing_t;

/** A probe the GPU backend captured, reported back to the main thread */
typedef struct box_render_bake_result_t {
    uint32_t volume;            /**< Handle of the volume */
    uint32_t generation;        /**< Generation of the bake batch of the probe */
    int32_t pass;               /**< Pass of the bake batch of the probe */
    uint32_t probe;             /**< Index of the probe in the volume */
} box_render_bake_result_t;

/** Take the probes the GPU backend of a world captured since the last call.
 *
 * A probe of a packet that was rendered and is not among them was not captured.
 *
 * @param world The world
 * @param out Receives box_render_bake_result_t
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Take the GPU time of the probe captures the GPU backend of a world measured.
 *
 * Timestamps are read back without waiting, so a measurement arrives a few frames after its
 * captures. Without timestamp queries the time from recording until the queue reports the work
 * done is measured on the CPU instead, which includes waits and errs on the expensive side.
 *
 * @param world The world
 * @param out Receives the time, zero when nothing arrived
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out);

/** What the cull pass of a view tested and kept in the last target the GPU backend rendered */
typedef struct box_render_cull_stats_t {
    int32_t tested;             /**< Instances the view tested */
    int32_t visible;            /**< Instances the view draws */
    int32_t casters_tested;     /**< Shadow casters tested, summed over the cascades */
    int32_t casters_visible;    /**< Shadow casters drawn, summed over the cascades */
} box_render_cull_stats_t;

/** The culling of a view the GPU backend read back, for its CullStatus */
typedef struct box_render_cull_status_t {
    ecs_entity_t view;          /**< Entity with the View */
    CullStatus status;          /**< The counts */
} box_render_cull_status_t;

/** Packet ring between the main thread and the consumer of the packets */
typedef struct box_render_ring_t box_render_ring_t;

/** Create a null backend.
 *
 * @return The backend state, freed by box_render_null_free
 */
box_render_null_t* box_render_null_new(void);

/** Free a null backend.
 *
 * @param ctx The backend state
 */
void box_render_null_free(
    void *ctx);

/** Get the backend interface of a null backend that the caller keeps ownership of.
 *
 * @param null The backend state
 * @return The backend interface, with a NULL free
 */
box_render_backend_t box_render_null_backend(
    box_render_null_t *null);

/** Create the backend that renders with webgpu, available when built with BOX_RENDER_WGPU.
 *
 * The device is created by init on the thread that renders and released by fini.
 *
 * @return The backend, which frees its state with its free callback
 */
box_render_backend_t box_render_gpu_backend(void);

/** Read the counters of the GPU backend of a world.
 *
 * @param world The world
 * @param stats The counters
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_stats(
    ecs_world_t *world,
    box_render_gpu_stats_t *stats);

/** Take the oldest shader compile result the GPU backend of a world has not handed out yet.
 *
 * @param world The world
 * @param result Receives the result, its message is released with ecs_os_free
 * @return Whether there was a result
 */
bool box_render_gpu_shader_result(
    ecs_world_t *world,
    box_render_shader_result_t *result);

/** Get the backend of a world.
 *
 * @param world The world
 * @return The backend, valid until the backend is replaced
 */
const box_render_backend_t* box_render_get_backend(
    const ecs_world_t *world);

/** Render with the GPU backend unless the world was given a backend of its own.
 *
 * The box.surface module calls this when a surface is set, so a world without surfaces never
 * creates a device. Without BOX_RENDER_WGPU the null backend stays.
 *
 * @param world The world
 */
void box_render_use_gpu(
    ecs_world_t *world);

/** Copy bytes of a buffer of the GPU backend of a world into memory, waiting for the copy.
 *
 * Only call this when the main thread renders (RenderSettings.main_thread), after
 * box_render_sync, so the device is used by one thread.
 *
 * @param world The world
 * @param handle Handle of the buffer
 * @param offset Byte offset in the buffer, a multiple of 4
 * @param size Bytes to copy, a multiple of 4
 * @param out Receives the bytes
 * @return Whether the backend is the GPU backend and the bytes were copied
 */
bool box_render_gpu_read_buffer(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t offset,
    uint32_t size,
    void *out);

/** Read back what the cull pass of a view kept in the last target the GPU backend rendered.
 *
 * Only call this when the main thread renders (RenderSettings.main_thread), after
 * box_render_sync, so the device is used by one thread.
 *
 * @param world The world
 * @param view Entity with the View
 * @param out Receives the counts
 * @return Whether the last rendered target culled for the view
 */
bool box_render_gpu_cull_stats(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_cull_stats_t *out);

/** Take the culling status of the views the GPU backend read back since the last call.
 *
 * @param world The world
 * @param out Receives box_render_cull_status_t, one per view, the latest counts
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_cull_status(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Register the system that writes the CullStatus of the views.
 *
 * @param world The world
 */
void box_render_cull_status_init(
    ecs_world_t *world);

/** Counters of the instanced batches of a world, written by the draw extract */
typedef struct box_render_draws_stats_t {
    int32_t batches;            /**< Batches alive */
    int32_t instances;          /**< Static instances in batches */
    int32_t dynamic;            /**< Dynamic instances in batches */
    int32_t processed;          /**< Changed entities processed by the last extract */
    int32_t scattered;          /**< Instances the last extract sent through the scatter pass */
    int64_t upload_bytes;       /**< Bytes the last extract uploaded into instance buffers */
} box_render_draws_stats_t;

/** Register the observers and the system that keep the shapes of the world in batches.
 *
 * Shapes are instanced in batches keyed by scene, mesh, pipeline and whether they are dynamic,
 * which persist between frames. Static instances live in instance buffers that are updated
 * on change through the scatter pass, dynamic instances are uploaded every frame.
 *
 * @param world The world
 */
void box_render_draws_init(
    ecs_world_t *world);

/** Read the counters of the instanced batches of a world.
 *
 * @param world The world
 * @param stats Receives the counters
 */
void box_render_draws_stats(
    const ecs_world_t *world,
    box_render_draws_stats_t *stats);

/** Forget the texture entities surfaces render into, before the surfaces register them again.
 *
 * @param world The world
 */
void box_render_texture_targets_clear(
    ecs_world_t *world);

/** Register the texture entity a surface renders into, so materials that sample the entity
 * sample the image of the target of the surface.
 *
 * @param world The world
 * @param texture The texture entity
 * @param target Handle of the target
 */
void box_render_texture_target(
    ecs_world_t *world,
    ecs_entity_t texture,
    uint32_t target);

/** Get the texture handle of the image of the target that renders into a texture entity.
 *
 * @param world The world
 * @param texture The texture entity
 * @return BOX_RENDER_TEXTURE_TARGET with the target handle, 0 when no surface renders into it
 */
uint32_t box_render_texture_target_handle(
    const ecs_world_t *world,
    ecs_entity_t texture);

/** Register the system that sends loaded texture images to the backend.
 *
 * It also writes the anisotropy of the samplers into the packet, the largest one any surface
 * asks for, and loads every texture again when the backend changes.
 *
 * @param world The world
 */
void box_render_textures_init(
    ecs_world_t *world);

/** Counters of the light extract of a world */
typedef struct box_render_lights_stats_t {
    int32_t scenes;             /**< Scenes that have lights */
    int32_t lights;             /**< Lights that light something, of every scene */
    int64_t upload_bytes;       /**< Bytes the last extract uploaded into light buffers */
} box_render_lights_stats_t;

/** Register the system that sends the lights of every scene and the lights of every view.
 *
 * Lights are collected per scene into a light buffer that is uploaded when it changed; every
 * view of the packet gets the buffer and the ambient light of the scene of its camera.
 *
 * @param world The world
 */
void box_render_lights_init(
    ecs_world_t *world);

/** Read the counters of the light extract of a world.
 *
 * @param world The world
 * @param stats Receives the counters
 */
void box_render_lights_stats(
    const ecs_world_t *world,
    box_render_lights_stats_t *stats);

/** Get the state of the light extract of a world, valid as long as the world.
 *
 * @param world The world
 * @return The state, NULL without the light extract
 */
const void* box_render_lights_state(
    const ecs_world_t *world);

/** Get the lights the light extract collected for a scene in this frame.
 *
 * The directional lights come first, then the point and spot lights, in query order; a scene
 * without light entities has none here and is lit by the default sun.
 *
 * @param state The state of the light extract
 * @param scene The scene, 0 for the default scene
 * @param count Receives the number of lights
 * @param entities Receives the entity of each light
 * @return The lights, NULL when the scene has none
 */
const box_render_light_t* box_render_lights_scene(
    const void *state,
    ecs_entity_t scene,
    int32_t *count,
    const ecs_entity_t **entities);

/** Register the cached query that knows the scene of every entity in a scene.
 *
 * @param world The world
 */
void box_render_scenes_init(
    ecs_world_t *world);

/** Get the scene an entity belongs to from the cached scene membership query.
 *
 * @param world The world
 * @param e The entity
 * @return The entity when it has Scene, else its nearest ancestor with Scene, 0 for the
 * default scene
 */
ecs_entity_t box_render_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Get the scene of an entity of a query result with an optional Scene(self|up) term.
 *
 * @param it The iterator
 * @param field Index of the Scene field
 * @param row Row of the entity in the result
 * @return The entity when it has Scene, else the source of the field, 0 for the default scene
 */
ecs_entity_t box_render_scene_field(
    const ecs_iter_t *it,
    int8_t field,
    int32_t row);

/** Register the system that resolves the shadow settings of every view of the packet.
 *
 * Runs after the light extract, because a view only renders shadows when its scene has a
 * directional light.
 *
 * @param world The world
 */
void box_render_shadows_init(
    ecs_world_t *world);

/** Resolve shadow settings: every field is clamped to its valid range, values apply as set.
 *
 * @param settings The settings of a view
 * @param out Receives the resolved settings
 */
void box_render_shadow_settings(
    const ShadowSettings *settings,
    ShadowSettings *out);

/** Split the view depths between near and distance into cascades.
 *
 * A split mixes the uniform split near + (distance - near) * i / n with the logarithmic split
 * near * (distance / near) ^ (i / n) by lambda: 0 is uniform, 1 logarithmic.
 *
 * @param near_ View depth the first cascade starts at
 * @param distance View depth the last cascade ends at
 * @param lambda Weight of the logarithmic split
 * @param cascades Number of cascades
 * @param out Receives the view depth each cascade ends at, the last one is distance
 */
void box_render_shadow_splits(
    float near_,
    float distance,
    float lambda,
    int32_t cascades,
    float *out);

/** A cascade fitted to a slice of the view frustum */
typedef struct box_render_cascade_t {
    mat4 matrix;            /**< World to shadow clip space, reversed Z: 1 nearest the light */
    float texel;            /**< World size of a texel of the cascade */
    float depth_range;      /**< World distance the depth of the cascade spans */
} box_render_cascade_t;

/** Fit a cascade around the bounding sphere of a slice of the view frustum.
 *
 * The sphere only depends on the projection and the slice depths, so its size is the same in
 * every frame; its center is snapped to whole texels in light space, so a moving camera does
 * not make the shadow edges shimmer.
 *
 * The depth range reaches back toward the light by distance, which keeps the casters between
 * the light and the slice.
 *
 * @param camera World transform of the camera
 * @param proj Projection of the view
 * @param near_ View depth the slice starts at
 * @param far_ View depth the slice ends at
 * @param direction Direction toward the light
 * @param resolution Edge of the cascade in texels
 * @param distance Distance the casters may be in front of the slice toward the light
 * @param out Receives the cascade
 */
void box_render_shadow_cascade(
    mat4 camera,
    mat4 proj,
    float near_,
    float far_,
    const float *direction,
    int32_t resolution,
    float distance,
    box_render_cascade_t *out);

/** Register the system that keeps the irradiance probe volume of every scene with LightBake.
 *
 * Runs after the light extract, because the probes start out as the ambient light of the views
 * of their scene. Views with lighting.baked sample the volume of their scene.
 *
 * A volume is a grid over the static bounds of its scene plus bounds_padding, probe_spacing
 * apart; it is sent again when the grid changes and refilled when the ambient changes.
 *
 * @param world The world
 */
void box_render_bake_init(
    ecs_world_t *world);

/** A probe of the volume of a scene */
typedef struct box_render_probe_t {
    vec3 position;          /**< World position of the probe */
    bool valid;             /**< The probe is outside static geometry, so views sample it */
} box_render_probe_t;

/** Find the probe of the volume of a scene nearest to a position.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param position The position
 * @param out Receives the probe
 * @return false when the scene has no probes
 */
bool box_render_bake_probe(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 position,
    box_render_probe_t *out);

/** Get the probes left to bake in the volumes whose bake can progress.
 *
 * A volume can progress when its settings are valid, its budget_ms is positive, a view of its
 * scene was in the packet of the last frame and the GPU backend renders; a volume that cannot
 * progress counts nothing.
 *
 * @param world The world
 * @return Probe captures of every pass not reported yet, those of the probes a change queued
 *         included, 0 when every bake that can progress converged
 */
int32_t box_render_bake_pending(
    const ecs_world_t *world);

/** Register the system that adds the effect passes of every view to the packet.
 *
 * Runs after the light extract, because fog is lit by the lights of the scene of the view.
 *
 * @param world The world
 */
void box_render_effects_init(
    ecs_world_t *world);

/** Resolve settings of effects: clamp every field to its valid range, values apply as set.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_bloom_settings(
    const Bloom *in,
    Bloom *out);

/** Resolve the settings of an ambient occlusion pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_ssao_settings(
    const Ssao *in,
    Ssao *out);

/** Resolve the settings of a fog pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_fog_settings(
    const HeightFog *in,
    HeightFog *out);

/** Resolve the settings of an adapted exposure.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_exposure_settings(
    const AutoExposure *in,
    AutoExposure *out);

/** Check whether the effect passes of a packet run an engine shader.
 *
 * @param p The packet
 * @param handle Handle of the shader
 * @return Whether a pass of a view uses the shader
 */
bool box_render_effects_use(
    const box_render_packet_t *p,
    uint32_t handle);

/** A one-off render of a camera into an offscreen image that is written as a PNG file.
 *
 * The camera is an entity read when the frame is extracted, or, when camera is 0, the
 * settings, transform and scene in the description.
 *
 * A camera that a view renders is captured as its first view renders it, effects included
 * (exposure and temporal bloom start fresh). Other cameras render lit, without shadows or
 * effects, with the default culling.
 */
typedef struct box_render_capture_desc_t {
    ecs_entity_t camera;        /**< Entity with Camera, 0 to use the fields below */
    Camera camera_data;         /**< Settings of the camera when camera is 0 */
    mat4 transform;             /**< World transform of the camera when camera is 0 */
    ecs_entity_t scene;         /**< Scene root drawn when camera is 0, 0 for the default scene */
    ClearSettings clear;        /**< Clear of the image, opaque black when not enabled */
    int32_t width;              /**< Width of the image in pixels */
    int32_t height;             /**< Height of the image in pixels */
    const char *path;           /**< PNG file, copied */
} box_render_capture_desc_t;

/** Register the capture queue and the system that adds the captures to the packet.
 *
 * @param world The world
 */
void box_render_captures_init(
    ecs_world_t *world);

/** Ask for a capture in the next extracted frame.
 *
 * The image is written on the thread that renders the packet of that frame, through a
 * temporary offscreen target; the world switches to the GPU backend unless it was given one.
 *
 * @param world The world
 * @param desc The capture
 * @return 0 when the capture was queued, -1 when the description is invalid
 */
int box_render_capture(
    ecs_world_t *world,
    const box_render_capture_desc_t *desc);

/** Get the number of captures that were requested and not yet added to a packet.
 *
 * @param world The world
 * @return The number of queued captures
 */
int32_t box_render_captures_pending(
    const ecs_world_t *world);

/** Initialize the vectors of a packet.
 *
 * @param p The packet
 */
void box_render_packet_init(
    box_render_packet_t *p);

/** Free the storage of a packet.
 *
 * @param p The packet
 */
void box_render_packet_fini(
    box_render_packet_t *p);

/** Empty a packet and keep its storage.
 *
 * @param p The packet
 */
void box_render_packet_reset(
    box_render_packet_t *p);

/** Copy the contents of a packet into another one, reusing its storage.
 *
 * @param dst The destination packet
 * @param src The source packet
 */
void box_render_packet_copy(
    box_render_packet_t *dst,
    const box_render_packet_t *src);

/** Get the packet of the current frame, acquiring it on first use in the frame.
 *
 * Only call this from the main thread during the RenderExtract phase. It waits when every
 * packet is in flight.
 *
 * @param world The world
 * @return The packet
 */
box_render_packet_t* box_render_packet(
    ecs_world_t *world);

/** Append a view to a packet.
 *
 * @param p The packet
 * @return The zeroed view
 */
box_render_view_t* box_render_packet_view(
    box_render_packet_t *p);

/** Append a batch to a packet.
 *
 * @param p The packet
 * @return The zeroed batch
 */
box_render_batch_t* box_render_packet_batch(
    box_render_packet_t *p);

/** Append a target to a packet.
 *
 * @param p The packet
 * @param handle Handle of the target
 * @param capture PNG file the frame of the target is written to, NULL for none
 * @return The target, with a zero clear color
 */
box_render_target_t* box_render_packet_target(
    box_render_packet_t *p,
    uint32_t handle,
    const char *capture);

/** Append an upload to a packet.
 *
 * @param p The packet
 * @param buffer Handle of the destination buffer
 * @param offset Byte offset in the destination buffer
 * @param size Bytes to write
 * @return Where the caller writes the bytes, valid until the next append to the packet
 */
void* box_render_packet_upload(
    box_render_packet_t *p,
    uint32_t buffer,
    uint32_t offset,
    uint32_t size);

/** Append a scatter to a packet.
 *
 * @param p The packet
 * @return The zeroed scatter
 */
box_render_scatter_t* box_render_packet_scatter(
    box_render_packet_t *p);

/** Append a resource change to a packet.
 *
 * @param p The packet
 * @param kind Kind of object
 * @param op What happens to it
 * @param handle Handle of the object
 * @param size Bytes of the description, 0 for none
 * @return Where the caller writes the description, valid until the next append, or NULL
 */
void* box_render_packet_resource(
    box_render_packet_t *p,
    box_render_resource_kind_t kind,
    box_render_resource_op_t op,
    uint32_t handle,
    uint32_t size);

/** Get the bytes an upload or a resource change points to.
 *
 * @param p The packet
 * @param offset The data offset of the upload or the resource change
 * @return The bytes
 */
const void* box_render_packet_data(
    const box_render_packet_t *p,
    uint32_t offset);

/** Replace the backend, after the packets in flight rendered with the current one.
 *
 * The current backend runs its fini and is freed; the new one runs init before its first
 * packet. Renderer modules call this when they are imported.
 *
 * @param world The world
 * @param backend The backend, copied
 */
void box_render_set_backend(
    ecs_world_t *world,
    const box_render_backend_t *backend);

/** Hold or release the packets on their way to the backend.
 *
 * A held ring still accepts packets until all of them are in flight. Shutting down renders
 * the packets in flight even while they are held.
 *
 * @param world The world
 * @param paused Whether the backend stops receiving packets
 */
void box_render_pause(
    ecs_world_t *world,
    bool paused);

/** Wait until the backend rendered every submitted packet, also while the ring is held,
 * and bring RenderStatus up to date.
 *
 * @param world The world
 */
void box_render_sync(
    ecs_world_t *world);

/** Create a packet ring with the null backend.
 *
 * @return The ring, freed by box_render_ring_free
 */
box_render_ring_t* box_render_ring_new(void);

/** Stop the consumer after it rendered every packet in flight and free the ring.
 *
 * @param ring The ring
 */
void box_render_ring_free(
    box_render_ring_t *ring);

/** Change the settings of a ring, applied before the next packet is acquired.
 *
 * @param ring The ring
 * @param settings The settings
 */
void box_render_ring_configure(
    box_render_ring_t *ring,
    const RenderSettings *settings);

/** Take the next free packet, waiting while every packet is in flight.
 *
 * @param ring The ring
 * @return The empty packet
 */
box_render_packet_t* box_render_ring_acquire(
    box_render_ring_t *ring);

/** Get the packet that was acquired and not yet submitted.
 *
 * @param ring The ring
 * @return The packet, or NULL when none is acquired
 */
box_render_packet_t* box_render_ring_current(
    box_render_ring_t *ring);

/** Hand the acquired packet to the consumer, or render it when the main thread renders.
 *
 * @param ring The ring
 */
void box_render_ring_submit(
    box_render_ring_t *ring);

/** Replace the backend of a ring.
 *
 * @param ring The ring
 * @param backend The backend, copied
 */
void box_render_ring_set_backend(
    box_render_ring_t *ring,
    const box_render_backend_t *backend);

/** Get the backend of a ring.
 *
 * @param ring The ring
 * @return The backend
 */
const box_render_backend_t* box_render_ring_backend(
    const box_render_ring_t *ring);

/** Check whether a ring still has the null backend it was created with.
 *
 * @param ring The ring
 * @return Whether no backend was set
 */
bool box_render_ring_is_default(
    const box_render_ring_t *ring);

/** Hold or release the packets of a ring.
 *
 * @param ring The ring
 * @param paused Whether the consumer stops taking packets
 */
void box_render_ring_pause(
    box_render_ring_t *ring,
    bool paused);

/** Wait until every submitted packet of a ring was rendered.
 *
 * @param ring The ring
 */
void box_render_ring_sync(
    box_render_ring_t *ring);

/** Read the counters of a ring.
 *
 * @param ring The ring
 * @param status The counters
 */
void box_render_ring_status(
    box_render_ring_t *ring,
    RenderStatus *status);

#endif
