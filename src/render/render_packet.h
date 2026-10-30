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


/** Shader handle of the engine shader of the forward pass that draws the 2D shapes */
#define BOX_RENDER_SHADER_SHAPE2D (7)

/** Shader handle of the engine compute shader that projects probe captures onto SH */
#define BOX_RENDER_SHADER_BAKE (8)

/** Shader handle of the variant of the shadow shader that clips the octants of the point light
 * tiles with clip distances, compiled only by a device with that feature */
#define BOX_RENDER_SHADER_SHADOW_CLIP (9)

/** Buffer handle of the change buffer the scatter pass reads changed instances from */
#define BOX_RENDER_BUFFER_CHANGES (1)

/** Buffer handle of the clip buffer: the areas of the ClipChildren elements the 2D draws read,
 * box_render_clip_t by clip index */
#define BOX_RENDER_BUFFER_CLIPS (2)

/** Buffer handle of the material table: box_render_material_t by MaterialId slot, which the mesh
 * instances reference by the id of their shading (box_render_mesh_shading_t). Bound with the
 * instance streams of every pass that draws meshes. */
#define BOX_RENDER_BUFFER_MATERIALS (3)

/** Records the material table holds at least, doubling from there */
#define BOX_RENDER_MATERIALS_MIN (64)

/** Records the material table holds at most, the 128 MiB storage binding every WebGPU device
 * supports; an id past it renders with slot 0 */
#define BOX_RENDER_MATERIALS_MAX ((uint32_t)((128u << 20) / sizeof(box_render_material_t)))

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

/** Buffer handle of the first instance page of the 2D shapes and glyphs; the pages from
 * BOX_RENDER_BUFFER_FIRST hold the meshes. Page p of the 2D pages has the handle
 * BOX_RENDER_BUFFER_FIRST_2D + p * BOX_RENDER_INSTANCE_STREAMS + s for stream s. */
#define BOX_RENDER_BUFFER_FIRST_2D \
    (BOX_RENDER_BUFFER_FIRST + BOX_RENDER_INSTANCE_STREAMS * 0x04000000u)

/** Stream of the transforms of an instance page, box_render_transform_t per slot */
#define BOX_RENDER_STREAM_TRANSFORM (0)

/** Stream of the shading constants of an instance page: box_render_mesh_shading_t per slot of
 * a mesh page, box_render_shading_t per slot of a 2D page */
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

/** First handle of the shadow atlases of scenes: a BOX_RENDER_SHADOW_ATLAS and the uniform
 * BOX_RENDER_BUFFER of its tile records share the handle */
#define BOX_RENDER_BUFFER_SHADOW_FIRST (0x50000000u)

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
    BOX_RENDER_ATMOSPHERE_ENVIRONMENT, /**< Sky an Atmosphere renders into the cubemap of its
                                       *   handle */
    BOX_RENDER_SHADOW_ATLAS /**< Depth32Float shadow atlas of the spot and point light tiles of
                             *   a scene */
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
    float near_;            /**< View depth the first cascade starts at */
    float splits[BOX_RENDER_SHADOW_CASCADES]; /**< View depth each cascade ends at */
    float direction[3];     /**< Direction toward the light that casts the shadows */
    bool spots;             /**< The view samples the spot and point light tiles of the atlas
                             *   of its scene: it renders shadows and is lit; filter_size,
                             *   depth_bias and normal_offset apply, in texels of a tile */
    uint32_t atlas;         /**< Handle of that atlas and its tile buffer, 0 for none */
    uint32_t tiles;         /**< Tile records of that atlas */
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
    ecs_entity_t entity;    /**< Entity with the Atmosphere of the scene of the view */
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

/** The ambient occlusion of a view, resolved from the first enabled Gtao of its effects */
typedef struct box_render_view_gtao_t {
    bool enabled;           /**< The view is lit, has a perspective camera and lists a Gtao */
    ecs_entity_t effect;    /**< The Gtao entity, keys the images the passes keep */
    Gtao settings;          /**< Resolved settings */
} box_render_view_gtao_t;

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
} box_render_view_t;

/** Kind of an effect pass of a view */
typedef enum box_render_effect_kind_t {
    BOX_RENDER_EFFECT_TONEMAP,  /**< Tonemap, optionally scaled by an adapted exposure first */
    BOX_RENDER_EFFECT_EXPOSURE, /**< AutoExposure on its own: scales by the adapted exposure */
    BOX_RENDER_EFFECT_BLOOM,    /**< Bloom */
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
    uint32_t surface_asset_count; /**< Surface asset vec4s after the indices */
    uint32_t vertex_count;  /**< Vertices after the description */
    uint32_t index_count;   /**< Indices after the vertices */
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
    float moon[4];          /**< Direction toward the moon, in w its illumination;
                             *   (0, -1, 0) without one */
    float moon_color[4];    /**< Top of atmosphere irradiance of the moon, 0 without one */
    float night[4];         /**< Linear night_tint times night_intensity */
} box_render_atmosphere_env_t;

/** How a buffer is used, flags of box_render_buffer_desc_t */
typedef enum box_render_buffer_usage_t {
    BOX_RENDER_BUFFER_INSTANCE = 1 << 0, /**< Per instance vertex data of batches */
    BOX_RENDER_BUFFER_STORAGE = 1 << 1,  /**< Read and written by compute shaders */
    BOX_RENDER_BUFFER_UNIFORM = 1 << 2   /**< Bound as uniforms */
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

/** Shading constants of a 2D shape or glyph, the shading stream of a 2D instance page, which
 * changes with its material.
 *
 * A 2D instance holds the words of its shape in material (see shape2d.wgsl) and its clip index
 * in ids[0].
 */
typedef struct box_render_shading_t {
    vec4 color;             /**< Linear base color, alpha is the opacity with its ancestors */
    vec4 emissive;          /**< Linear emitted color times its strength, w unused */
    vec4 material;          /**< Metallic, roughness, zw unused (the words of a 2D shape) */
    vec4 uv;                /**< TextureTransform: u scale, v scale, u offset, v offset */
    uint32_t ids[4];        /**< Clip index of a 2D instance (0 for none), its rank in the draw
                             *   order of its tree, two reserved 0 */
} box_render_shading_t;

/** A record of the material table (BOX_RENDER_BUFFER_MATERIALS), Material of material.wgsl: the
 * material of a prefab or of a shape that owns material components, slot 0 the default one */
typedef struct box_render_material_t {
    uint32_t color;         /**< Rgb, display encoded bytes: r | g << 8 | b << 16 */
    float metallic;         /**< PbrMaterial.metallic, clamped to 0..1 */
    float roughness;        /**< PbrMaterial.roughness, clamped to 0..1 */
    float cutoff;           /**< AlphaCutoff clamped to 0..1, 0 without one */
    float emissive[3];      /**< Linear emitted color times its strength */
    uint32_t reserved;      /**< 0 */
    float uv[4];            /**< TextureTransform: u scale, v scale, u offset, v offset */
} box_render_material_t;

/** Shading of a mesh instance, the shading stream of a mesh instance page, MeshShading of
 * instance.wgsl.
 *
 * The tint is the composed mix of the tints of the ancestors and of the instance's own Tint
 * (box_draw_tint_t): a display encoded color becomes color * keep + add, as four unorm16.
 */
typedef struct box_render_mesh_shading_t {
    uint32_t material;      /**< Slot of the material table the instance renders with */
    uint32_t tint[2];       /**< Tint add red | add green << 16, add blue | keep << 16 */
    float alpha;            /**< The opacity of the instance times that of its ancestors */
} box_render_mesh_shading_t;

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
    BOX_RENDER_TRANSITION_COMPONENTS        /**< Number of transitionable components */
} box_render_transition_component_t;

/** Bytes of the largest transitionable component, the size of a value in a record */
#define BOX_RENDER_TRANSITION_VALUE (16)

/** Fields of the transitionable component with the most fields */
#define BOX_RENDER_TRANSITION_FIELDS (4)

/** What a transition record does */
typedef enum box_render_transition_op_t {
    BOX_RENDER_TRANSITION_CREATE = 0,   /**< A node exists, with no values */
    BOX_RENDER_TRANSITION_DESTROY,      /**< A node no longer exists, its values are dropped */
    BOX_RENDER_TRANSITION_START         /**< A component of a node has a new target */
} box_render_transition_op_t;

/** A change the game thread found for the transition resolver of the render thread.
 *
 * A start without a previous value of the component, or with a duration and delay of 0, sets
 * the value without a transition; any other start transitions from the value the node renders
 * at the start time to the target. The records of a packet apply in the order they were made.
 */
typedef struct box_render_transition_record_t {
    box_render_transition_op_t op;  /**< What the record does */
    uint32_t node;                  /**< Node id, from 1 */
    box_render_transition_component_t component; /**< Component of a start */
    TimingFunction timing_function; /**< Curve of a start */
    float delay;                    /**< Seconds the value holds still after the start */
    float duration;                 /**< Seconds from the end of the delay to the target */
    double start;                   /**< World time of the start, the time of its packet */
    uint8_t value[BOX_RENDER_TRANSITION_VALUE]; /**< Target in the layout of the component,
                                     *   padding zeroed */
} box_render_transition_record_t;

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
    ecs_vec_t shadow_atlases; /**< box_render_shadow_atlas_t, the atlases with tiles */
    ecs_vec_t shadow_tiles; /**< box_render_shadow_tile_t of those atlases */
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
    int64_t resources;          /**< Resource changes received in total */
    int64_t bytes;              /**< Upload bytes received in total */
    int64_t targets;            /**< Targets received in total */
    int64_t transitions;        /**< Transition records received in total */
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
