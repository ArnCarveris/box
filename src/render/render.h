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

/** Cascades of the shadow map of a view at most */
#define BOX_RENDER_SHADOW_CASCADES (4)

/** Widest tent filter of a shadow lookup in texels, odd */
#define BOX_RENDER_SHADOW_FILTER_MAX (7)

/** Widest blend band at the edge of a cascade, a fraction of its half extent */
#define BOX_RENDER_SHADOW_BLEND_MAX (0.5f)


/** Shader handle of the engine shader of the forward pass that draws the 2D shapes */
#define BOX_RENDER_SHADER_SHAPE2D (7)

/** Shader handle of the engine compute shader that projects probe captures onto SH */
#define BOX_RENDER_SHADER_BAKE (8)

/** Buffer handle of the change buffer the scatter pass reads changed instances from */
#define BOX_RENDER_BUFFER_CHANGES (1)

/** Buffer handle of the clip buffer: the areas of the ClipChildren elements the 2D draws read,
 * box_render_clip_t by clip index */
#define BOX_RENDER_BUFFER_CLIPS (2)

/** Most nested clips a 2D draw intersects; clips further up the hierarchy are not applied */
#define BOX_RENDER_CLIP_DEPTH (16)

/** Buffer handle of the first instance page, the first handle of the instance pages.
 *
 * Every batch draws a run of slots of one instance page: BOX_RENDER_INSTANCE_STREAMS storage
 * buffers of at most BOX_DRAWS_CHUNK instances with the same slots, shared by the batches.
 * box_render_batch_t.instances is the transform stream of the page, first_instance its slot.
 *
 * Page p has the handle BOX_RENDER_BUFFER_FIRST + p * BOX_RENDER_INSTANCE_STREAMS + s for
 * stream s. A released page index is reused by the next page the store needs. */
#define BOX_RENDER_BUFFER_FIRST (16)

/** Buffers of an instance page, one per stream, at consecutive handles */
#define BOX_RENDER_INSTANCE_STREAMS (3)

/** Stream of the transforms of an instance page, box_render_transform_t per slot */
#define BOX_RENDER_STREAM_TRANSFORM (0)

/** Stream of the shading constants of an instance page, box_render_shading_t per slot */
#define BOX_RENDER_STREAM_SHADING (1)

/** Stream of the surface parameters of an instance page, box_render_surface_t per slot */
#define BOX_RENDER_STREAM_SURFACE (2)

/** Byte alignment of a scatter segment in the change buffer, the storage offset alignment */
#define BOX_RENDER_SCATTER_ALIGN (256)

/** Words of the header of a scatter segment: count, words per instance and two unused */
#define BOX_RENDER_SCATTER_HEADER (4)

/** First buffer handle of the light buffers of scenes */
#define BOX_RENDER_BUFFER_LIGHT_FIRST (0x20000000u)

/** First buffer handle of the reflection probe buffers of scenes */
#define BOX_RENDER_BUFFER_REFLECT_FIRST (0x30000000u)

/** First target handle of captures, the handles below belong to surfaces */
#define BOX_RENDER_CAPTURE_TARGET_FIRST (0x40000000u)

/** First shader handle of application shaders, the handles below are engine shaders */
#define BOX_RENDER_SHADER_FIRST (64)

/** Texture layers of a batch: albedo, emissive, ORM and normal, as in PbrTextures */
#define BOX_RENDER_TEXTURE_SLOTS (4)

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
    BOX_RENDER_ATMOSPHERE_ENVIRONMENT /**< Sky an Atmosphere renders into the cubemap of its
                                       *   handle */
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
    float color[3];     /**< Linear color times intensity, the irradiance of the light */
    float spot_scale;   /**< Cone falloff scale, 1 / (cos inner - cos outer) */
    float direction[3]; /**< Direction a spot light shines in */
    float spot_offset;  /**< Cone falloff offset, -cos outer times spot_scale */
} box_render_light_t;

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
    int32_t filter_size;    /**< Width of the tent filter in texels, odd, 1 to
                             *   BOX_RENDER_SHADOW_FILTER_MAX */
    float depth_bias;       /**< Depth offset of the comparison in texels */
    float normal_offset;    /**< Offset of the lookup along the geometric normal in texels at
                             *   grazing light, scaled by 1 - N.L */
    float fade;             /**< Fraction of the last cascade that fades out */
    float blend;            /**< Fraction of the half extent of a cascade that blends into
                             *   the next one at its edge */
    float near_;            /**< View depth the first cascade starts at */
    float splits[BOX_RENDER_SHADOW_CASCADES]; /**< View depth each cascade ends at */
    float direction[3];     /**< Direction toward the light that casts the shadows */
} box_render_view_shadows_t;

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
} box_render_view_atmos_t;

/** The environment a view is lit with, resolved from LightSettings.environment */
typedef struct box_render_view_env_t {
    uint32_t handle;        /**< Handle of the BOX_RENDER_ENVIRONMENT, 0 for none */
    float intensity;        /**< Scale of the environment light */
    float yaw;              /**< Rotation of the environment about +Y in radians */
    bool background;        /**< Draw the environment in place of the clear color */
} box_render_view_env_t;

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
} box_render_view_t;

/** Kind of an effect pass of a view */
typedef enum box_render_effect_kind_t {
    BOX_RENDER_EFFECT_TONEMAP,  /**< Tonemap, optionally scaled by an adapted exposure first */
    BOX_RENDER_EFFECT_EXPOSURE, /**< AutoExposure on its own: scales by the adapted exposure */
    BOX_RENDER_EFFECT_BLOOM,    /**< Bloom */
    BOX_RENDER_EFFECT_SSAO,     /**< Ssao */
    BOX_RENDER_EFFECT_FOG,      /**< HeightFog */
    BOX_RENDER_EFFECT_SMAA      /**< Smaa */
} box_render_effect_kind_t;

/** An effect pass of a view with its settings resolved, in the order of View.effects */
typedef struct box_render_effect_t {
    box_render_effect_kind_t kind; /**< Kind of pass */
    ecs_entity_t entity;        /**< Effect entity, keys the state the pass keeps across frames */
    ecs_entity_t exposure;      /**< AutoExposure entity whose adapted exposure scales, 0 none */
    AutoExposure auto_exposure; /**< Resolved exposure settings when exposure is set */
    TonemapOperator tonemap;    /**< Operator of a tone map pass */
    Bloom bloom;                /**< Resolved settings of a bloom pass */
    Ssao ssao;                  /**< Resolved settings of an ambient occlusion pass */
    HeightFog fog;              /**< Resolved settings of a fog pass */
    Smaa smaa;                  /**< Resolved settings of an anti aliasing pass */
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
    uint32_t surface_asset_count;
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

/** Edge of the multi scattering LUT of an atmosphere in texels */
#define BOX_RENDER_ATMOS_MULTI_SIZE (32)

/** Width of the sky view LUT of a view in texels */
#define BOX_RENDER_ATMOS_SKY_WIDTH (192)

/** Height of the sky view LUT of a view in texels */
#define BOX_RENDER_ATMOS_SKY_HEIGHT (108)

/** Edge of the aerial perspective volume of a view in texels, its slices included */
#define BOX_RENDER_ATMOS_AERIAL_SIZE (32)

/** Width of the fog in scatter color of a view in texels: the ambient and the glow */
#define BOX_RENDER_ATMOS_FOG_WIDTH (2)

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

/** Description of a BOX_RENDER_ATMOSPHERE_ENVIRONMENT update: the sky of the atmosphere of a
 * scene, rendered into mip 0 of the cubemap of the BOX_RENDER_ENVIRONMENT of the handle.
 *
 * The backend renders a sky view LUT of the scene at its ground altitude, then the cubemap
 * from it before the environments are processed, and reports the cubemap written with version.
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
    float moon[4];          /**< Direction toward the moon, in w its illumination; (0, -1, 0) without one */
    float moon_color[4];    /**< Top of atmosphere irradiance of the moon, 0 without one */
    float night[4];         /**< Linear night_tint times night_intensity */
} box_render_atmosphere_env_t;

/** How a buffer is used, flags of box_render_buffer_desc_t */
typedef enum box_render_buffer_usage_t {
    BOX_RENDER_BUFFER_INSTANCE = 1 << 0, /**< Per instance vertex data of batches */
    BOX_RENDER_BUFFER_STORAGE = 1 << 1   /**< Read and written by compute shaders */
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

/** Shading constants of an instance, the stream of an instance page that changes with its
 * material.
 *
 * A 2D instance holds the words of its shape in material (see shape2d.wgsl) and its clip index
 * in ids[0].
 */
typedef struct box_render_shading_t {
    vec4 color;             /**< Linear base color, alpha is the opacity with its ancestors */
    vec4 emissive;          /**< Linear emitted color times its strength, w unused */
    vec4 material;          /**< Metallic, roughness, the AlphaCutoff of an alpha tested
                             *   instance (0 otherwise), w unused */
    vec4 uv;                /**< TextureTransform: u scale, v scale, u offset, v offset */
    uint32_t ids[4];        /**< Clip index of a 2D instance (0 for none), three reserved 0 */
} box_render_shading_t;

/** Surface parameters of an instance, the stream of an instance page that changes with its
 * ShapeMesh: the parametric surface the vertex shaders evaluate (surface.wgsl), 0 for a mesh.
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
    bool shadow;                /**< The instances cast shadows, false for NoShadow */
    bool bake;                  /**< Static 3D instances without NoBake of a scene with LightBake or
                                 *   ReflectionBake, drawn by the captures */
    uint32_t mesh;              /**< Mesh handle */
    uint32_t textures[BOX_RENDER_TEXTURE_SLOTS]; /**< Texture handles of the layers, 0 for none */
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
    float fps;              /**< Frame rate cap of the frame, App.fps, 0 when uncapped */
    bool vsync;             /**< A window target of the frame presents with vsync */
    float refresh;          /**< Refresh rate in Hz of the display of that window, 0 unknown */
    uint32_t anisotropy;    /**< Anisotropic samples of the texture samplers, 1 to 16 */
    ecs_vec_t resources;    /**< box_render_resource_t in the order they were made */
    ecs_vec_t uploads;      /**< box_render_upload_t in the order they were made */
    ecs_vec_t scatters;     /**< box_render_scatter_t, run after the uploads */
    ecs_vec_t views;        /**< box_render_view_t in render order */
    ecs_vec_t batches;      /**< box_render_batch_t */
    ecs_vec_t targets;      /**< box_render_target_t in the order they render */
    ecs_vec_t effects;      /**< box_render_effect_t, the effect passes of the views */
    ecs_vec_t bakes;        /**< box_render_bake_batch_t, the probe captures of the frame */
    ecs_vec_t probes;       /**< box_render_probe_capture_t of the bake and reflection batches */
    ecs_vec_t reflections;  /**< box_render_reflect_batch_t, the reflection captures of the frame */
    ecs_vec_t particles;    /**< box_render_particle_pool_t, simulated after the scatters */
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
    int32_t draws;              /**< Draw calls recorded, those of the depth prepasses
                                 *   excluded */
    int32_t prepasses;          /**< Depth prepass render passes recorded, of the lit views
                                 *   that draw opaque meshes: one per frame, two for a view
                                 *   that culls by occlusion */
    int32_t prepass_draws;      /**< Draw calls recorded in the depth prepasses */
    int32_t instance_binds;     /**< Instance bind groups set in the cull, depth prepass,
                                 *   forward, shadow and capture passes, only when the page (or
                                 *   surface assets of the mesh) differs from the one bound last
                                 *   in the pass */
    int32_t scatters;           /**< Scatter dispatches recorded */
    int32_t scattered;          /**< Instances the scatter dispatches copied */
    int32_t probes;             /**< Probes captured */
    int64_t cull_ns;            /**< GPU time of the cull passes of views read back, 0 without
                                 *   timestamp queries */
    int32_t cull_passes;        /**< Cull passes whose GPU time cull_ns holds */
    int32_t texture_layers;     /**< maxTextureArrayLayers of the device, 0 before it exists */
    int32_t atmosphere_luts;    /**< Atmospheres whose transmittance and multi scattering LUTs
                                 *   exist */
    int32_t atmosphere_views;   /**< Views and targets whose sky view and aerial perspective
                                 *   exist */
    int32_t atmosphere_builds;  /**< Times the LUTs of an atmosphere were built */
    int64_t atmosphere_ns[BOX_RENDER_ATMOS_PASSES]; /**< GPU time of each atmosphere pass read
                                 *   back, by box_render_atmos_pass_t, 0 without timestamp
                                 *   queries */
    int32_t atmosphere_passes[BOX_RENDER_ATMOS_PASSES]; /**< Passes whose GPU time
                                 *   atmosphere_ns holds */
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

/** Take the reflection probes the GPU backend of a world captured since the last call.
 *
 * Each result has the texture handle as volume, the ticket of the probe as generation and its
 * slot as probe. A probe of a packet that was rendered and is not among them was not captured.
 *
 * @param world The world
 * @param out Receives box_render_bake_result_t
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_reflect_captured(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Take the GPU time of the reflection captures the GPU backend of a world measured, as
 * box_render_gpu_bake_timing does for the probe captures.
 *
 * @param world The world
 * @param out Receives the time, zero when nothing arrived
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_reflect_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out);

/** Copy mip 0 of one face of a reflection probe cubemap into memory, waiting for the copy.
 *
 * Texel (x, y) of the face, y down, follows the WebGPU cubemap convention. Only call this when
 * the main thread renders, after box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param texture Handle of the BOX_RENDER_REFLECTION
 * @param slot Cubemap slot
 * @param face Face, 0 to 5 for +x, -x, +y, -y, +z, -z
 * @param out Receives resolution squared linear RGBA texels, four floats each
 * @param count Floats out holds
 * @return Whether the texture exists and the texels were copied
 */
bool box_render_gpu_read_reflection(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    uint32_t face,
    float *out,
    int32_t count);

/** Copy the L2 SH irradiance of a reflection probe cubemap slot into memory, waiting for the
 * copy.
 *
 * The coefficients E_lm = A_l * L_lm of the capture are in the basis and order of mesh.wgsl, as
 * the probes of a volume; a slot gets them with its prefiltered mips. Only call this when the
 * main thread renders, after box_render_sync, as box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param texture Handle of the BOX_RENDER_REFLECTION
 * @param slot Cubemap slot
 * @param out Receives the linear rgb coefficients, w unused
 * @return Whether the texture exists and the coefficients were copied
 */
bool box_render_gpu_read_reflection_sh(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    float out[BOX_RENDER_SH_COEFFICIENTS][4]);

/** Copy the L2 SH irradiance of a probe of a probe buffer of a volume into memory, waiting for
 * the copy. Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param volume Handle of the BOX_RENDER_VOLUME
 * @param buffer Probe buffer, 0 or 1
 * @param probe Index of the probe
 * @param out Receives the linear rgb coefficients, w unused
 * @return Whether the volume has the probe and the coefficients were copied
 */
bool box_render_gpu_read_volume_sh(
    ecs_world_t *world,
    uint32_t volume,
    uint32_t buffer,
    uint32_t probe,
    float out[BOX_RENDER_SH_COEFFICIENTS][4]);

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

/** Check whether the GPU backend of a world may still write the capture of a file: its packet
 * rendered and the readback of the image is still mapping.
 *
 * @param world The world
 * @param path The file
 * @return Whether a capture of the file is mapping
 */
bool box_render_gpu_capture_pending(
    ecs_world_t *world,
    const char *path);

/** Destroy the device of the GPU backend of a world before its next packet, which loses it the
 * way a driver reset or a removed GPU does, to test the recovery.
 *
 * @param world The world
 */
void box_render_gpu_lose_device(
    ecs_world_t *world);

/** When the GPU backend applies the outcome of a shader compile.
 *
 * Natively an outcome arrives during the compile, in the browser after the frame; tests pick
 * a delayed delivery to run the asynchronous path of the browser natively.
 */
typedef enum box_render_compile_delivery_t {
    BOX_RENDER_COMPILE_AT_ONCE = 0,     /**< During the compile, the native default */
    BOX_RENDER_COMPILE_HOLD = 1,        /**< Held until the delivery changes */
    BOX_RENDER_COMPILE_NEXT_PACKET = 2, /**< At the start of the next packet, oldest first */
    BOX_RENDER_COMPILE_NEWEST_FIRST = 3 /**< At the start of the next packet, newest first */
} box_render_compile_delivery_t;

/** Set when the GPU backend of a world applies shader compile outcomes, for tests.
 *
 * Outcomes held when the delivery returns to BOX_RENDER_COMPILE_AT_ONCE are applied at the
 * start of the next packet, oldest first.
 *
 * @param world The world
 * @param delivery When outcomes are applied
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_compile_delivery(
    ecs_world_t *world,
    box_render_compile_delivery_t delivery);

/** When the GPU backend publishes the cull counters it read back.
 *
 * Copies finish mapping in any order; tests hold them and publish them newest first to make
 * an older frame arrive after a newer one.
 */
typedef enum box_render_readback_delivery_t {
    BOX_RENDER_READBACK_AT_ONCE = 0,     /**< Every mapped copy, in slot order, the default */
    BOX_RENDER_READBACK_HOLD = 1,        /**< Mapped copies wait until the delivery changes */
    BOX_RENDER_READBACK_NEWEST_FIRST = 2, /**< Every mapped copy, newest frame first */
    BOX_RENDER_READBACK_ONE_NEWEST = 3   /**< Only the newest mapped copy per frame */
} box_render_readback_delivery_t;

/** Set the slots one page of the cull pass holds, for tests.
 *
 * The visible slots of the draws of a target and the occlusion history of a view are split
 * over pages of at most this many 4 byte words, each bound whole as storage; the default is
 * the 128 MiB storage binding every WebGPU device supports. Applies from the next cull pass.
 *
 * @param world The world
 * @param slots Words per page, at least 1
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_cull_window(
    ecs_world_t *world,
    uint32_t slots);

/** Set when the GPU backend of a world publishes cull counters, for tests.
 *
 * @param world The world
 * @param delivery When and in which order mapped copies are published
 * @return Whether the world renders with the GPU backend
 */
bool box_render_gpu_readback_delivery(
    ecs_world_t *world,
    box_render_readback_delivery_t delivery);

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
 * box_render_sync, so the device is used by one thread; a call from another thread than the
 * one that renders asserts, and returns false when asserts are compiled out.
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
 * box_render_sync, so the device is used by one thread; a call from another thread than the
 * one that renders asserts, and returns false when asserts are compiled out.
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
    int32_t scatters;           /**< Scatters the last extract sent, one per instance page with
                                 *   static changes */
    int32_t pages;              /**< Instance pages alive */
    int64_t upload_bytes;       /**< Bytes the last extract uploaded into instance buffers, the
                                 *   sum of the bytes of the streams */
    int64_t transform_bytes;    /**< Bytes the last extract uploaded into the transform streams:
                                 *   the runs of batches whose transforms upload (every frame for
                                 *   DynamicTransform) and the scatter segments of moved slots */
    int64_t shading_bytes;      /**< Bytes the last extract uploaded into the shading streams:
                                 *   the runs of batches whose shading uploads (every frame
                                 *   for DynamicMaterial) and scatter segments of materials */
    int64_t surface_bytes;      /**< Bytes the last extract uploaded into the surface streams,
                                 *   runs and scatter segments, only when a ShapeMesh changed */
    int32_t clips;              /**< ClipChildren elements in the clip buffer */
    int64_t clip_bytes;         /**< Bytes the last extract uploaded into the clip buffer */
} box_render_draws_stats_t;

/** Register the observers and the system that keep the shapes of the world in batches.
 *
 * Shapes are instanced in batches keyed by scene, mesh, pipeline and whether their transform
 * and their material are dynamic, which persist between frames.
 *
 * An instance page holds a transform, a shading and a surface stream. The transforms of
 * DynamicTransform shapes and the shading of DynamicMaterial shapes upload every frame; every
 * other stream is updated on change through the scatter pass, only the streams that changed.
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

/** Get the clip area of a ClipChildren element: its LayoutBounds, else its Rect, with the
 * corners rounded by its Border.radius.
 *
 * @param world The world
 * @param e The element
 * @param half Receives half its width and half its height, 0 when it has neither
 * @return The corner radius, clamped to half the smaller side
 */
float box_render_clip_area(
    const ecs_world_t *world,
    ecs_entity_t e,
    float half[2]);

/** Check whether a world position is inside the clip areas of the ClipChildren ancestors of an
 * entity, in the local XY plane of each, as the 2D shader clips it. A clip with an empty area
 * contains no position. The ancestors of a floating element (LayoutFloat) do not clip it.
 *
 * @param world The world
 * @param e The entity, whose own ClipChildren does not clip it
 * @param point The world position
 * @return Whether no ancestor clips the position away
 */
bool box_render_clip_contains(
    const ecs_world_t *world,
    ecs_entity_t e,
    const float point[3]);

/** Check whether the ClipChildren ancestors of an entity clip all of it away: its bounds (its
 * LayoutBounds, else its Rect, else its origin) have no part of positive size inside the area of
 * one of them, except a clip that scrolls (with a LayoutScroll). A clip with an empty area hides everything below it. The ancestors of a floating
 * element (LayoutFloat) do not clip it.
 *
 * @param world The world
 * @param e The entity
 * @return Whether no part of it is drawn
 */
bool box_render_clip_hides(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Set the instances one batch holds before its shapes continue in another, for tests.
 *
 * A batch is one run of slots of an instance page, so the default keeps it within the 128 MiB
 * storage binding every WebGPU device supports; shapes added after a change follow the new size.
 *
 * @param world The world
 * @param instances Instances per batch, at least 1
 */
void box_render_draws_chunk(
    ecs_world_t *world,
    int32_t instances);

/** Set the slots of an instance page, for tests.
 *
 * The default is BOX_DRAWS_CHUNK, the 128 MiB storage binding every WebGPU device supports. A
 * batch never spans pages, so a key also continues in another batch once a batch holds this
 * many shapes. Pages created after the change hold at most this many slots.
 *
 * @param world The world
 * @param slots Slots per page, at least 1
 */
void box_render_draws_page(
    ecs_world_t *world,
    int32_t slots);

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

/** Make the light extract attenuate the sun and moon of the scenes with an Atmosphere and
 * write their AtmosphereStatus, called by the import of box.atmosphere.
 *
 * @param world The world
 */
void box_render_lights_atmosphere(
    ecs_world_t *world);

/** Get the sun of the valid atmosphere of a scene as the light extract of this frame found it.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param direction Receives the normalized direction toward the sun
 * @param color Receives its linear color times intensity, before the transmittance
 * @return Whether the scene has a valid atmosphere and a sun
 */
bool box_render_lights_atmos_sun(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color);

/** Get the moon of the valid atmosphere of a scene as the light extract of this frame found it.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param direction Receives the normalized direction toward the moon
 * @param color Receives its linear color times intensity, before the transmittance
 * @param illumination Receives its lit fraction, 0 without a sun
 * @return Whether the scene has a valid atmosphere and a moon
 */
bool box_render_lights_atmos_moon(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color,
    float *illumination);

/** Get the main light of the valid atmosphere of a scene as the light extract of this frame
 * attenuated it: the sun or the moon, whichever is brighter after the transmittance and the
 * horizon fade, the sun by day and the moon by night.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param direction Receives the normalized direction toward the light
 * @param color Receives its linear color times intensity, times the transmittance and
 * the horizon fade, and for the moon its illumination
 * @param fade Receives the fade of its disk at the horizon
 * @param moon Receives whether the light is the moon
 * @return Whether the scene has a valid atmosphere with a sun or a moon
 */
bool box_render_lights_atmos_main(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color,
    float *fade,
    bool *moon);

/** Register the system that keeps the transmittance and multi scattering LUTs of every scene
 * with a valid Atmosphere, a BOX_RENDER_ATMOSPHERE per scene.
 *
 * Runs before the light extract, which writes the lut_version of each build into the
 * AtmosphereStatus. The LUTs are built again only when the medium changes: radii, scale
 * heights, coefficients, tint and ground albedo, not the sun, the colors or the cameras.
 *
 * @param world The world
 */
void box_render_atmosphere_init(
    ecs_world_t *world);

/** Register the system that gives every view of a scene with atmosphere LUTs its sky view and
 * aerial perspective inputs, after the light extract found the sun of the scene.
 *
 * @param world The world
 */
void box_render_atmosphere_views_init(
    ecs_world_t *world);

/** Register the system that makes the entity with the Atmosphere of every scene with LUTs an
 * environment source, which renders its sky into the cubemap of its valid Environment as a
 * BOX_RENDER_ATMOSPHERE_ENVIRONMENT.
 *
 * It renders when the cubemap is empty, the medium changed, the sun or moon moved more than
 * Atmosphere.ibl_update_angle or a channel of their color changed by more than
 * BOX_RENDER_ATMOS_ENV_COLOR, and never otherwise. Runs after the light extract.
 *
 * @param world The world
 */
void box_render_atmosphere_environment_init(
    ecs_world_t *world);

/** Relative change of the color of the sun or moon, against its brightest channel, above
 * which the environment of an atmosphere is rendered again */
#define BOX_RENDER_ATMOS_ENV_COLOR (0.02f)

/** Make the atmosphere extract follow the Atmosphere components, called by the import of
 * box.atmosphere.
 *
 * @param world The world
 */
void box_render_atmosphere_import(
    ecs_world_t *world);

/** Get the lut_version an atmosphere has after the extract of this frame.
 *
 * @param world The world
 * @param scene The scene of the atmosphere
 * @param entity The entity with the Atmosphere
 * @param current Its lut_version so far
 * @return The version of the last build of the LUTs of the scene when they were built from the
 *         entity, else current
 */
int32_t box_render_atmosphere_version(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t entity,
    int32_t current);

/** Get the handle of the atmosphere LUTs of a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @return Handle of its BOX_RENDER_ATMOSPHERE, 0 when the scene has no valid Atmosphere
 */
uint32_t box_render_atmosphere_handle(
    const ecs_world_t *world,
    ecs_entity_t scene);

/** Check whether a packet runs the atmosphere shader.
 *
 * @param p The packet
 * @return Whether it builds atmosphere LUTs or a view has an atmosphere
 */
bool box_render_atmosphere_use(
    const box_render_packet_t *p);

/** Get where the transmittance LUT holds a radius and a direction, as atmosphere.wgsl maps them.
 *
 * @param bottom Radius of the ground in km
 * @param top Radius of the top of the atmosphere in km
 * @param r Radius of the start of the ray in km
 * @param mu Cosine of the angle between the ray and the up direction
 * @param uv Receives the texture coordinates, 0 to 1, v 0 at the first row
 */
void box_render_atmos_transmittance_uv(
    float bottom,
    float top,
    float r,
    float mu,
    float *uv);

/** Copy the transmittance or the multi scattering LUT of an atmosphere into memory, waiting for
 * the copy.
 *
 * Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param handle Handle of the BOX_RENDER_ATMOSPHERE
 * @param lut BOX_RENDER_ATMOS_TRANSMITTANCE or BOX_RENDER_ATMOS_MULTI
 * @param out Receives the linear RGBA texels row by row, four floats each
 * @param count Floats out holds
 * @return Whether the LUTs exist, were built and the texels were copied
 */
bool box_render_gpu_read_atmosphere(
    ecs_world_t *world,
    uint32_t handle,
    box_render_atmos_pass_t lut,
    float *out,
    int32_t count);

/** Make the light extract attenuate the sun and moon of the scenes with a TimeOfDay and no
 * Atmosphere by the fallback of the time of day, called by the import of box.atmosphere.
 *
 * @param world The world
 */
void box_render_lights_time_of_day(
    ecs_world_t *world);

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

/** Resolve shadow settings: every field is clamped to its valid range and an even
 * filter_size rounds up to the next odd one, other values apply as set.
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

/** Get the probes left to bake in the volumes whose bake can progress and has not settled.
 *
 * A volume can progress when its settings are valid, its budget_ms is positive, a view of its
 * scene was in the packet of the last frame and the GPU backend renders; a volume that cannot
 * progress counts nothing.
 *
 * A volume settles when it first converges after it was filled, or when a bake completes
 * after a light, ambient or environment change queued every probe, as a time of day does.
 *
 * @param world The world
 * @return Probe captures of every pass not reported yet, those of the probes a change queued
 *         included, 0 when every bake that can progress settled
 */
int32_t box_render_bake_pending(
    const ecs_world_t *world);

/** Texture array layers the reflection probe slots assume while no device reports its limit,
 * the WebGPU default of maxTextureArrayLayers */
#define BOX_RENDER_REFLECT_LAYERS (256)

/** A resident reflection probe as the reflection probe buffer of its scene holds it.
 *
 * The buffer lists the resident probes of a scene in blend order: higher priority first, then
 * the smaller influence box, the scene probe last. The boxes are centered on center and
 * oriented by axes.
 */
typedef struct box_render_reflection_probe_t {
    float center[3];        /**< World center of the influence and projection boxes */
    float blend;            /**< Distance inside the influence box over which it fades out */
    float axes[3][4];       /**< World directions of the local x, y and z axes, unit length, w
                             *   unused */
    float extent[3];        /**< Half size of the influence box */
    int32_t priority;       /**< ReflectionProbe.priority, INT32_MIN for the scene probe */
    float projection[3];    /**< Half size of the projection box, zero for no box projection */
    uint32_t slot;          /**< Cubemap slot of the probe */
    float capture[3];       /**< World capture point */
    uint32_t scene;         /**< 1 for the scene probe, 0 for a ReflectionProbe */
    uint32_t ready;         /**< 1 when views can sample the slot: its capture was reported or
                             *   runs before the targets of this packet */
    uint32_t pad[3];        /**< Unused */
} box_render_reflection_probe_t;

/** Register the system that keeps the reflection probes of every scene with ReflectionBake.
 *
 * Runs after the bake extract, whose change records queue the probes a change reaches for a new
 * capture. Probes keep their cubemap slot while they stay resident; the resident probes of a
 * scene are uploaded to its reflection probe buffer when they change.
 *
 * @param world The world
 */
void box_render_reflect_init(
    ecs_world_t *world);

/** Get the resident reflection probes of a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param count Receives the number of probes
 * @return The probes in blend order, valid until the next extract, NULL when the scene has
 *         no ReflectionBake
 */
const box_render_reflection_probe_t* box_render_reflect_probes(
    const ecs_world_t *world,
    ecs_entity_t scene,
    int32_t *count);

/** Get the reflection probe cubemap array of a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @return Handle of its BOX_RENDER_REFLECTION, 0 when the scene has none
 */
uint32_t box_render_reflect_texture(
    const ecs_world_t *world,
    ecs_entity_t scene);

/** Get the reflection probes left to capture in the scenes whose captures can progress and
 * have not settled.
 *
 * Captures can progress when the resolution is valid, budget_ms is positive, a view of the
 * scene was in the packet of the last frame and the GPU backend renders.
 *
 * Once the light bake of the scene settled, a scene settles when it first converges, or when
 * every resident probe has a capture after a light, ambient, environment or bake change
 * queued every probe.
 *
 * @param world The world
 * @return Resident probes without a capture of what they show now, 0 when every scene that
 *         can progress settled
 */
int32_t box_render_reflect_pending(
    const ecs_world_t *world);

/** Get the resident reflection probes of a scene that wait for a capture, in the order they are
 * captured: the scene probe first, then nearer to the cameras of the scene.
 *
 * A probe waits until its first capture is in a packet, and again from a change of what it
 * shows until the capture after that change is in a packet; until then it shows its previous
 * capture.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param out Receives the entity of each probe, 0 for the scene probe, NULL for none
 * @param max Entities out holds
 * @return Probes that wait, which may be more than max
 */
int32_t box_render_reflect_queue(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t *out,
    int32_t max);

/** Mark the resident reflection probes of a scene captured with nothing queued, as if the
 * backend reported their captures, for tests that run without the GPU backend. The
 * ReflectionBakeStatus follows in the next frame.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 */
void box_render_reflect_settle(
    ecs_world_t *world,
    ecs_entity_t scene);

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

/** Resolve the settings of an anti aliasing pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_smaa_settings(
    const Smaa *in,
    Smaa *out);

/** Texels along each edge of the Tony McMapface LUT */
#define BOX_RENDER_TONY_SIZE (48)

/** Texels of the Tony McMapface LUT */
#define BOX_RENDER_TONY_TEXELS (BOX_RENDER_TONY_SIZE * BOX_RENDER_TONY_SIZE * BOX_RENDER_TONY_SIZE)

/** File of the Tony McMapface LUT, found like a shader file */
#define BOX_RENDER_TONY_PATH "etc/luts/tony_mc_mapface.dds"

/** Load the Tony McMapface LUT from its DDS file: 48^3 RGB9E5 texels, x fastest, then y, then z.
 *
 * @param path The file, relative paths are found like shader files
 * @param error Receives why the file cannot be used, NULL when it loads
 * @return The texels, freed with ecs_os_free, NULL when the file cannot be used
 */
uint32_t* box_render_tony_load(
    const char *path,
    char **error);

/** Decode a texel of the shared exponent format RGB9E5.
 *
 * @param texel The texel: 9 bit mantissas of r, g and b from the low bits, 5 bit exponent on top
 * @param out Receives the linear color
 */
void box_render_rgb9e5_decode(
    uint32_t texel,
    float *out);

/** Map a linear color with the Khronos PBR Neutral curve, as etc/shaders/tonemap.wgsl does.
 *
 * @param in The color, after the exposure
 * @param out Receives the mapped color
 */
void box_render_tonemap_pbr_neutral(
    const float *in,
    float *out);

/** Map a linear color with Tony McMapface, as etc/shaders/tonemap.wgsl does without its dither:
 * the LUT sampled with trilinear filtering at c / (c + 1), with half texel insets.
 *
 * @param lut The texels of the LUT, as box_render_tony_load returns them
 * @param in The color, after the exposure
 * @param out Receives the mapped color
 */
void box_render_tonemap_tony(
    const uint32_t *lut,
    const float *in,
    float *out);

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

/** Empty a packet and keep its storage, except a data block above BOX_RENDER_DATA_KEEP that
 * the frames used little of for BOX_RENDER_DATA_TRIM_RESETS resets in a row, which shrinks to
 * the largest of those frames.
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

/** Reserve an aligned block of bytes in the data of a packet.
 *
 * The block moves when more data is reserved; keep the offset, not the pointer.
 *
 * @param p The packet
 * @param size Bytes to reserve
 * @param offset_out Receives the data offset of the block
 * @return The block, NULL when size is 0
 */
void* box_render_packet_reserve(
    box_render_packet_t *p,
    uint32_t size,
    uint32_t *offset_out);

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

/** Wait until the backend rendered every submitted packet, also while the ring is held, and
 * flushed it, so every capture of those packets is written, and bring RenderStatus up to date.
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

#include "particles.h"

#endif
