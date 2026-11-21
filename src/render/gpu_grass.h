/**
 * @file render/gpu_grass.h
 * @brief What the grass passes of the webgpu backend (gpu_grass.c) and the shadows of their
 * blades (gpu_grass_shadow.c) share: the buffers of the Grass entities, the passes and the params.
 */

#ifndef BOX_RENDER_GPU_GRASS_H
#define BOX_RENDER_GPU_GRASS_H

#ifdef BOX_RENDER_WGPU

/** Invocations of a workgroup of the place dispatch, as GROUP in grass_place.wgsl */
#define BOX_GPU_GRASS_GROUP (64u)

/** Vertex indices a level of detail takes, as GRASS_LOD_VERTICES in grass.wgsl */
#define BOX_GPU_GRASS_LOD_VERTICES (64u)

/** Edge of a patch of the grid in whole meters */
#define BOX_GPU_GRASS_PATCH (8)

/** Most cells along an edge of a patch */
#define BOX_GPU_GRASS_CELLS_MAX (128)

/** Most patches along an edge of the grid */
#define BOX_GPU_GRASS_PATCHES_MAX (128)

/** Bytes between the params of two Grass entities in a view, the uniform offset alignment */
#define BOX_GPU_GRASS_PARAMS_STRIDE (512u)

/** Fewest and most blades a level of a Grass holds */
#define BOX_GPU_GRASS_CAPACITY_MIN (4096u)
#define BOX_GPU_GRASS_CAPACITY_MAX (1u << 19)

/** Bytes of the counters of a Grass: the indirect arguments of its levels and their blades */
#define BOX_GPU_GRASS_COUNTERS (64u)

/** Bytes of the counts of a packet: blades placed and drawn per level, then the patches and the
 * blades culled by occlusion */
#define BOX_GPU_GRASS_STATS (32u)

/** Readbacks of the counts and timestamps in flight at most */
#define BOX_GPU_GRASS_READBACKS (4)

/** Timestamps of the grass passes of a packet at most, a begin and an end per pass */
#define BOX_GPU_GRASS_STAMPS (64)

/** Bytes of the resolved timestamps of a packet, a multiple of the resolve alignment */
#define BOX_GPU_GRASS_STAMP_BYTES (BOX_GPU_GRASS_STAMPS * sizeof(uint64_t))

/** Bytes of a readback: the counts, then the timestamps */
#define BOX_GPU_GRASS_READBACK_BYTES (BOX_GPU_GRASS_STATS + BOX_GPU_GRASS_STAMP_BYTES)

/** Nanoseconds above which a measured pass is dropped as a stale timestamp */
#define BOX_GPU_GRASS_MEASURE_MAX (1e9)

/** Draw pipelines, one per color format and depth mode */
#define BOX_GPU_GRASS_DRAWS (BOX_GPU_BLIT_FORMATS * 2)

/** The params of a Grass in a view (GrassParams of grass.wgsl) */
typedef struct box_gpu_grass_params_t {
    float view_proj[16];            /**< View projection of the view */
    float camera[4];                /**< Local camera, the distance the blades end at */
    int32_t patches[4];             /**< Absolute patch of the corner, patches and cells along
                                     *   an edge */
    float corner[4];                /**< Local x and z of the corner, edge of a patch and a cell */
    float ground[4];                /**< Lowest and highest ground, plane, thinning start */
    float lod[4];                   /**< Ends of levels 0 and 1, end and fade start of the blades */
    float blade[4];                 /**< Shortest, tallest, narrowest and widest blade */
    float base[4];                  /**< Linear color at the base */
    float tip[4];                   /**< Linear color at the tip */
    uint32_t config[4];             /**< Mask channel plus one, terrain, seed, capacity */
    float shape[4];                 /**< Edge of a clump, how strongly blades follow their
                                     *   clump, variation over the world, height
                                     *   below which blades fold */
    float pixel[4];                 /**< Size of a pixel per meter of distance and its size at
                                     *   any distance, one of them 0 */
    float wind_field[4];            /**< Direction the wind blows toward, local offset of its
                                     *   gust field (GrassWind of grass_wind.wgsl) */
    float wind_strength[4];         /**< Speed, gust, 1 over the gust scale, phase of the sway */
    float wind_time[4];             /**< Phase of the swing and flutter of the blades, 0 */
    float light[4];                 /**< Direction toward a directional light or local position
                                     *   of a spot or point light; 0 directional, 1 spot, 2
                                     *   plus the octant of a point light tile */
    float planes[4];                /**< Near and far plane of a point light tile, 0 */
    float caster[4];                /**< Distance the blades cast within, distance their
                                     *   shadows fade from, reach of the shadows that sweeps
                                     *   the frustum, 1 to place the blades that cast */
    uint32_t hiz[4];                /**< Width, height and levels of the depth pyramid of the
                                     *   view the placement culls by occlusion against, 0
                                     *   levels for none */
} box_gpu_grass_params_t;

_Static_assert(sizeof(box_gpu_grass_params_t) == 352, "GrassParams of grass.wgsl");
_Static_assert(sizeof(box_gpu_grass_params_t) <= BOX_GPU_GRASS_PARAMS_STRIDE,
    "the params of a Grass must fit in their stride");

/** A blade (GrassBlade of grass.wgsl) */
typedef struct box_gpu_grass_blade_t {
    float position[3];              /**< Local position of its base */
    float yaw;                      /**< Facing around the vertical */
    float height;                   /**< Height */
    float width;                    /**< Width at the base */
    float lean;                     /**< Lean of the tip, 0 to 1 */
    float shade;                    /**< Brightness */
    float bend;                     /**< How far it bends over toward its tip, 0 to 1 */
    float curl;                     /**< Curl around its length, -1 to 1 */
    uint32_t tint;                  /**< Color variation over the world, half of it in RGB8,
                                     *   the phase of its clump in the high and its stiffness
                                     *   in the low 4 bits of A */
    float clump;                    /**< Distance from the center of its clump in clump edges */
} box_gpu_grass_blade_t;

_Static_assert(sizeof(box_gpu_grass_blade_t) == 48, "GrassBlade of grass.wgsl");

/** State of a readback of the counts and timestamps of a packet */
typedef enum box_gpu_grass_read_state_t {
    BOX_GPU_GRASS_FREE,             /**< Unused */
    BOX_GPU_GRASS_RECORDING,        /**< The passes of the packet being rendered write into it */
    BOX_GPU_GRASS_WRITTEN,          /**< Its counts copied, its timestamps resolved a packet
                                     *   later */
    BOX_GPU_GRASS_COPIED,           /**< Resolved and copied, not yet mapping */
    BOX_GPU_GRASS_MAPPING,          /**< Waiting for the map */
    BOX_GPU_GRASS_MAPPED,           /**< Mapped, the counts can be read */
    BOX_GPU_GRASS_FAILED            /**< The map failed */
} box_gpu_grass_read_state_t;

/** A readback of the counts and timestamps of a packet */
typedef struct box_gpu_grass_readback_t {
    WGPUBuffer buffer;              /**< Mappable copy of the counts and timestamps */
    int32_t state;                  /**< box_gpu_grass_read_state_t */
    uint32_t stamps;                /**< Timestamps the passes wrote */
} box_gpu_grass_readback_t;

/** The buffers of a Grass entity */
typedef struct box_gpu_grass_entity_t {
    box_render_grass_t grass;       /**< The Grass of the last packet */
    int64_t frame;                  /**< Frame of the last packet that had it */
    uint32_t capacity;              /**< Blades a level holds */
    WGPUBuffer blades;              /**< Blades, a range of capacity per level */
    WGPUBuffer counters;            /**< Indirect arguments of the levels and their blades */
    WGPUBindGroup place_group;      /**< Binds them to the place pass */
    WGPUBindGroup draw_group;       /**< Binds the blades to the draw */
    WGPUBuffer group_params;        /**< Params ring the groups bind */
    uint64_t recorded;              /**< Recording of the last view that placed its blades */
    uint32_t offset;                /**< Offset of its params in that view */
    bool late;                      /**< That view placed them after its depth pyramid, culled
                                     *   by occlusion */
    uint32_t shadow_capacity;       /**< Blades a level of the blades that cast holds, 0 when
                                     *   the Grass casts no shadows */
    WGPUBuffer shadow_blades;       /**< Blades that cast shadows, a range per level */
    WGPUBuffer shadow_counters;     /**< Their indirect arguments and counts */
    WGPUBindGroup shadow_place_group; /**< Binds them to the place pass */
    WGPUBindGroup shadow_draw_group; /**< Binds them to the shadow draws */
    uint64_t shadow_recorded;       /**< Recording of the last view that placed them */
    box_gpu_grass_params_t shadow_params; /**< Params they were placed with */
} box_gpu_grass_entity_t;

/** A draw pipeline of a color format and depth mode */
typedef struct box_gpu_grass_pipeline_t {
    WGPUTextureFormat format;       /**< Format of the color target */
    bool equal;                     /**< Tests the depth of the prepass without writing it */
    WGPURenderPipeline pipeline;    /**< The pipeline */
} box_gpu_grass_pipeline_t;

/** The grass passes */
typedef struct box_gpu_grass_t {
    WGPUBindGroupLayout place_layout; /**< Params, blades, counters and counts of the packet */
    WGPUBindGroupLayout draw_layout; /**< Params and blades */
    WGPUBindGroupLayout hiz_layout; /**< Depth pyramid the place pass culls against */
    WGPUPipelineLayout place_pipeline_layout; /**< Sampled terrain, place_layout, then
                                     *   hiz_layout */
    WGPUPipelineLayout draw_pipeline_layout; /**< View group, then draw_layout */
    WGPUComputePipeline reset;      /**< Writes the vertex counts and zeroes the blades */
    WGPUComputePipeline place;      /**< Places the blades of the patches */
    WGPUComputePipeline finish;     /**< Clamps the blades and adds the counts */
    int32_t place_revision;         /**< Revision of the shader of the compute pipelines */
    box_gpu_grass_pipeline_t draws[BOX_GPU_GRASS_DRAWS]; /**< Draw pipelines */
    int32_t draw_count;             /**< Draw pipelines created */
    WGPURenderPipeline depth;       /**< Draw pipeline of the depth prepass */
    WGPURenderPipeline shadow;      /**< Draws the blades that cast into a cascade or a spot
                                     *   light tile */
    WGPURenderPipeline shadow_point; /**< Draws them into an octant of a point light tile */
    int32_t draw_revision;          /**< Revision of the shader of the draw pipelines */
    WGPUBindGroupLayout empty_layout; /**< Layout of the empty group 0 of the shadow draws */
    WGPUBindGroup empty_group;      /**< The empty group 0 of the shadow draws */
    WGPUPipelineLayout shadow_pipeline_layout; /**< Empty group, then draw_layout */
    WGPUBuffer params;              /**< Ring of the params of the Grass entities in the views */
    uint32_t params_capacity;       /**< Params the ring holds */
    uint32_t params_used;           /**< Params the packet wrote */
    WGPUBuffer stats;               /**< Counts of the packet */
    WGPUBuffer no_params;           /**< Zero params of the sampled terrain of a Grass without */
    WGPUBuffer no_grid;             /**< Empty grid of that terrain */
    WGPUBindGroup no_terrain;       /**< Binds them, the terrain of the Grass on a plane */
    WGPUTexture no_pyramid;         /**< Pyramid of the placements that do not cull by
                                     *   occlusion */
    WGPUTextureView no_pyramid_view; /**< View of it */
    WGPUBindGroup no_hiz;           /**< Binds it */
    WGPUBindGroup hiz;              /**< Binds the pyramid of the view whose blades are being
                                     *   placed after it, NULL for no_hiz */
    ecs_map_t entities;             /**< Grass entity to box_gpu_grass_entity_t */
    uint64_t recording;             /**< Views that placed blades so far */
    const box_render_view_t *view;  /**< View whose blades the last recording placed, NULL when
                                     *   the last view placed none */
    bool active;                    /**< The packet has a Grass */
    WGPUQuerySet queries;           /**< Timestamps per readback, NULL without the feature */
    WGPUBuffer resolve;             /**< Resolved timestamps */
    box_gpu_grass_readback_t readbacks[BOX_GPU_GRASS_READBACKS]; /**< Count copies */
    int32_t current;                /**< Readback of the packet being rendered, -1 for none */
} box_gpu_grass_t;

/** Release a webgpu object when it exists.
 *
 * @param obj The object
 * @param release The release function of its type
 */
#define BOX_GPU_GRASS_RELEASE(obj, release) \
    if (obj) { \
        release(obj); \
        obj = NULL; \
    }

/** Create a buffer.
 *
 * @param gpu The backend
 * @param size Bytes
 * @param usage Usage
 * @return The buffer
 */
WGPUBuffer box_gpu_grass_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage);

/** Release the draw pipelines, which are created again with the current draw shader.
 *
 * @param s The grass passes
 */
void box_gpu_grass_draw_reset(
    box_gpu_grass_t *s);

/** Make the compute pipelines match the place shader, requested until each exists.
 *
 * @param gpu The backend
 * @return Whether they all exist
 */
bool box_gpu_grass_place_pipelines(
    box_gpu_t *gpu);

/** Fill the params of a Grass around a camera, all but the view projection and the size of a
 * pixel: a grid of patches out to a distance, levels of detail, blades and wind.
 *
 * @param packet The packet
 * @param e The Grass
 * @param camera Local position of the camera
 * @param distance Distance from the camera the blades end at
 * @param capacity Blades a level holds
 * @param out Receives the params
 * @return The patches along an edge of the grid
 */
uint32_t box_gpu_grass_params(
    const box_render_packet_t *packet,
    const box_gpu_grass_entity_t *e,
    const float camera[3],
    float distance,
    uint32_t capacity,
    box_gpu_grass_params_t *out);

/** Get the timestamp writes of a grass pass when there is room for its timestamps.
 *
 * @param s The grass passes
 * @param writes Receives the writes
 * @return writes, NULL when the pass is not timed
 */
const WGPUPassTimestampWrites* box_gpu_grass_writes(
    box_gpu_grass_t *s,
    WGPUPassTimestampWrites *writes);

/** Begin the compute pass of a view, timed when there is room for its timestamps.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param encoder The encoder
 * @return The pass
 */
WGPUComputePassEncoder box_gpu_grass_pass(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUCommandEncoder encoder);

/** Record the reset, place and finish dispatches of a placement of the blades of a Grass.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param pass The compute pass
 * @param e The Grass
 * @param params The params of the placement
 * @param n Patches along an edge of its grid
 * @param group Its place group, of its blades or of those that cast shadows
 * @param offset Receives the offset of its params in the ring
 * @return Whether the blades were placed, false without its sampled terrain or when the ring
 * is full
 */
bool box_gpu_grass_dispatch(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUComputePassEncoder pass,
    const box_gpu_grass_entity_t *e,
    const box_gpu_grass_params_t *params,
    uint32_t n,
    WGPUBindGroup group,
    uint32_t *offset);

/** Fill the size of a pixel of the params of a Grass in a view: per meter of distance with a
 * perspective projection, at any distance with an orthographic one.
 *
 * @param u The uniforms of the view
 * @param out The pixel of the params, the other of its first two values left as it is
 */
void box_gpu_grass_pixel(
    const box_gpu_view_uniforms_t *u,
    float out[4]);

/** Get the blades a level of the blades of a Grass that cast shadows holds: those within its
 * shadow distance, the full density within the end of level 0 and the thinned field beyond.
 *
 * @param g The Grass
 * @return The blades, a multiple of the workgroup size, 0 for a Grass that casts no shadows
 */
uint32_t box_gpu_grass_shadow_capacity(
    const box_render_grass_t *g);

/** Make the buffers of the blades of a Grass entity that cast shadows hold a capacity, or
 * release them when it casts none.
 *
 * @param gpu The backend
 * @param e The entity
 * @param capacity Blades a level holds, 0 for none
 */
void box_gpu_grass_entity_shadows(
    box_gpu_t *gpu,
    box_gpu_grass_entity_t *e,
    uint32_t capacity);

/** Record the placement of the blades of a Grass that cast shadows into the cascades of a view
 * in its compute pass, when it casts into any: within its shadow distance, at the coarser levels
 * of the shadows, those whose shadows reach into the frustum of the view.
 *
 * @param gpu The backend
 * @param s The grass passes
 * @param pass The compute pass of the view
 * @param packet The packet
 * @param e The Grass
 * @param v The view
 * @param u The uniforms of the view
 */
void box_gpu_grass_place_shadows(
    box_gpu_t *gpu,
    box_gpu_grass_t *s,
    WGPUComputePassEncoder pass,
    const box_render_packet_t *packet,
    box_gpu_grass_entity_t *e,
    const box_render_view_t *v,
    const box_gpu_view_uniforms_t *u);

#endif

#endif
