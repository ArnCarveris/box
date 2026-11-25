/**
 * @file render/gpu_texture_graph.h
 * @brief Types of the texture graphs of the webgpu backend, shared by its evaluation and its
 * readbacks.
 */

#ifndef BOX_RENDER_GPU_TEXTURE_GRAPH_H
#define BOX_RENDER_GPU_TEXTURE_GRAPH_H

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Texels of the edge of a workgroup of a stage */
#define BOX_GPU_TG_GROUP (8)

/** Materialized inputs one stage reads at most, within the sampled textures every device binds
 * per stage */
#define BOX_GPU_TG_INPUTS (12)

/** Binding of the first materialized input of a stage, after the parameters, the uniforms, the
 * output and the texture of the inputs without a node */
#define BOX_GPU_TG_FIRST_INPUT (4)

/** Textures of sampling nodes one stage binds at most, with its materialized inputs and the
 * texture of the inputs without a node within the 16 sampled textures every device binds */
#define BOX_GPU_TG_TEXTURES (3)

/** Binding of the first texture the sampling nodes of a stage sample, after the bindings of
 * the most materialized inputs */
#define BOX_GPU_TG_FIRST_TEXTURE (BOX_GPU_TG_FIRST_INPUT + BOX_GPU_TG_INPUTS)

/** Bytes after the parameters of a graph, so the four vec4 a node function gets stay in the
 * buffer for the last node */
#define BOX_GPU_TG_PARAM_PAD (64)

/** Evaluation of a packet that crossfades the two keys of an interpolated graph into its
 * image, see box_gpu_tg_eval_t.key */
#define BOX_GPU_TG_BLEND (2)

/** Bytes of BoxTexGraphStage */
#define BOX_GPU_TG_UNIFORM_SIZE (64)

/** Bytes of the cached values no graph uses kept by default (TextureGraphCache.budget) */
#define BOX_GPU_TG_CACHE_BUDGET (256ll * 1024 * 1024)

/** Texels of graph nodes a frame that spreads its evaluations (box_render_packet_t.spread)
 * computes per sixtieth of a second, about the heights and masks of a terrain tile */
#define BOX_GPU_TG_SPREAD_TEXELS (700000.0f)

/** Share of the texels waiting such a frame computes at least, so a backlog still drains */
#define BOX_GPU_TG_SPREAD_SHARE (16)

/** Packets a pooled intermediate that no graph used stays in the pool */
#define BOX_GPU_TG_POOL_KEEP (120)

/** Timestamp readbacks of the evaluations in flight */
#define BOX_GPU_TG_STAMPS (4)

/** Bytes between the resolved timestamps of two readbacks, the alignment of a resolve */
#define BOX_GPU_TG_STAMP_STRIDE (256)

/** Longest evaluation measured in nanoseconds, longer ones are discarded as invalid */
#define BOX_GPU_TG_MEASURE_MAX (1e9)

/** Offset basis of the 64 bit FNV-1a hash of the program keys */
#define BOX_GPU_TG_FNV_BASIS (14695981039346656037ull)

/** Prime of the 64 bit FNV-1a hash of the program keys */
#define BOX_GPU_TG_FNV_PRIME (1099511628211ull)

/** A node kind the main thread sent */
typedef struct box_gpu_tg_kind_t {
    uint32_t output;            /**< TextureValueType of its value */
    uint32_t input_count;       /**< Inputs of a node */
    uint32_t param_size;        /**< Bytes of the parameters before the input constants */
    uint32_t coords_inputs;     /**< Bit j: input j without a node is the domain coordinates */
    uint32_t flags;             /**< BOX_TEXTURE_NODE_NEIGHBOURHOOD, _ANIMATED, _TIME_SCALE,
                                 *   _PLANE, _SAMPLES */
    uint32_t passes;            /**< Passes of the function, from 1 */
    char *function;             /**< Name of the WGSL function */
    char *source;               /**< WGSL source of the function */
    uint64_t hash;              /**< Hash of the description, part of the program keys */
} box_gpu_tg_kind_t;

/** Lines of the code of a stage that came from a node, to name the node of a compile error */
typedef struct box_gpu_tg_line_t {
    int32_t first;              /**< First line, from 1 */
    int32_t last;               /**< Last line */
    int32_t node;               /**< Index of the node, -1 for the shared code */
} box_gpu_tg_line_t;

/** Bind group and pipeline layout of the stages with a number of inputs, of sampled textures
 * and an output format */
typedef struct box_gpu_tg_layout_t {
    int32_t inputs;             /**< Materialized inputs */
    int32_t textures;           /**< Sampled textures */
    WGPUTextureFormat format;   /**< Format of the storage texture the stage writes */
    WGPUBindGroupLayout group;  /**< The bind group layout */
    WGPUPipelineLayout pipeline; /**< The pipeline layout */
} box_gpu_tg_layout_t;

/** A stage of a program: one compute shader that materializes one node, or one pass but the
 * last of a node of a kind of more than one pass */
typedef struct box_gpu_tg_stage_t {
    uint32_t shader;            /**< Shader handle of its module */
    int32_t root;               /**< Node whose value, or whose pass, it stores */
    int32_t pass;               /**< Pass of root it stores, -1 for the value of root */
    int32_t slot;               /**< Slot of the texture it writes: root for its value, from the
                                 *   node count for the earlier passes of the nodes */
    bool output;                /**< It stores the image */
    WGPUTextureFormat format;   /**< Format of the texture it writes */
    int32_t input_count;        /**< Materialized slots it reads */
    int32_t inputs[BOX_GPU_TG_INPUTS]; /**< Those slots, binding BOX_GPU_TG_FIRST_INPUT + i */
    int32_t texture_count;      /**< Textures its sampling nodes sample */
    int32_t textures[BOX_GPU_TG_TEXTURES]; /**< Those textures by index in the textures of
                                 *   the graph, binding BOX_GPU_TG_FIRST_TEXTURE + i */
    box_gpu_tg_layout_t *layout; /**< Its layouts, owned by the graph state */
    WGPUComputePipeline pipeline; /**< Its pipeline once created */
    ecs_vec_t lines;            /**< box_gpu_tg_line_t of its code */
} box_gpu_tg_stage_t;

/** The stages of a structure, shared by the graphs whose key is the same */
typedef struct box_gpu_tg_program_t {
    uint64_t key;               /**< Key of the structure, see box_gpu_tg_key */
    int32_t refs;               /**< Graphs that use it as current or pending program */
    int32_t node_count;         /**< Nodes of the structure */
    int32_t slot_count;         /**< Texture slots of an evaluation: one per node, then one per
                                 *   pass but the last of each node of more than one pass */
    int32_t stage_count;        /**< Stages in stages */
    box_gpu_tg_stage_t *stages; /**< The stages in order, the output stage last */
    bool failed;                /**< A stage did not compile, or the structure cannot be made */
    char *error;                /**< Why it failed */
    int32_t error_node;         /**< Node the error came from, -1 for none */
} box_gpu_tg_program_t;

/** A pooled intermediate */
typedef struct box_gpu_tg_pooled_t {
    WGPUTexture texture;        /**< The texture */
    WGPUTextureView view;       /**< View of its one level */
    uint32_t width;             /**< Width */
    uint32_t height;            /**< Height */
    WGPUTextureFormat format;   /**< Format */
    bool used;                  /**< An evaluation of this packet holds it */
    int64_t last;               /**< Packet it was last used in */
} box_gpu_tg_pooled_t;

/** List of the graph state a graph is in, which decides when a packet looks at it */
typedef enum box_gpu_tg_track_t {
    BOX_GPU_TG_IDLE,            /**< None: up to date, not live and it samples no texture, so
                                 *   no packet looks at it until it is activated */
    BOX_GPU_TG_ACTIVE,          /**< tg->active: every packet looks at it */
    BOX_GPU_TG_WATCHER          /**< tg->watchers: up to date and not live, but it samples
                                 *   textures, so the packets after an image changed look at it */
} box_gpu_tg_track_t;

/** A texture graph the main thread sent, under its texture handle */
typedef struct box_gpu_tg_graph_t {
    uint32_t handle;            /**< Texture handle */
    box_render_texture_graph_t desc; /**< Its description */
    box_render_texture_graph_node_t *nodes; /**< Its nodes */
    uint8_t *params;            /**< Its parameter bytes */
    box_render_texture_graph_texture_t *textures; /**< The textures its nodes sample */
    uint64_t *bound;            /**< Serial of the image of each texture its last evaluation
                                 *   read, see box_gpu_tg_source_serial */
    ecs_vec_t forced;           /**< int32_t, nodes it keeps materialized for readbacks */
    uint64_t key;               /**< Key of the program it needs, 0 to compute again */
    box_gpu_tg_program_t *current; /**< Program it evaluates with */
    box_gpu_tg_program_t *pending; /**< Program of its key being created, NULL for none */
    WGPUBuffer param_buffer;    /**< Its parameters */
    uint64_t param_capacity;    /**< Bytes of param_buffer */
    WGPUBuffer uniforms;        /**< Its BoxTexGraphStage */
    char *invalid;              /**< Why the description cannot be evaluated, NULL when valid */
    bool dirty;                 /**< The image is older than the description */
    bool written;               /**< The image of the handle was written by the graph */
    uint32_t reported;          /**< Serial whose outcome was reported, 0 for none */
    uint64_t *hashes;           /**< Content hash of each node, computed with the key */
    bool *animated;             /**< Whether each node depends on time, computed with the key */
    bool *sampling;             /**< Whether each node depends on a sampled texture */
    bool is_animated;           /**< The output depends on time */
    struct box_gpu_tg_entry_t **entries; /**< Cache entry each node of the last evaluation
                                 *   held, by node of the current program, NULL for none */
    int32_t entry_count;        /**< Nodes in entries */
    int64_t sampled;            /**< Packet of the graph state whose draws last sampled the
                                 *   image, -1 for none */
    double time;                /**< World time of the last evaluation */
    box_gpu_tg_pooled_t keys[2]; /**< Key images of an interpolated graph, never in the pool,
                                 *   texture NULL until the first key evaluation */
    int64_t key_index[2];       /**< Key k of the world clock (time k / rate) each key holds */
    bool keys_valid;            /**< Both keys hold k and k + 1 of the current description */
    WGPUBuffer key_uniforms[2]; /**< BoxTexGraphStage of the evaluation of each key */
    WGPUBuffer blend_uniforms;  /**< BoxTexGraphStage of the crossfade, its time the factor */
    double blended;             /**< World time of the last crossfade, NAN for none */
    int64_t mark;               /**< Packet whose evaluation order it was put in */
    int32_t eval;               /**< Index of its evaluation in the packet, -1 for none */
    box_gpu_tg_track_t track;   /**< List of the graph state that holds it */
    int32_t slot;               /**< Its index in that list */
    bool live;                  /**< Its image changes without a change of the graph: it is
                                 *   animated, or samples a target or the image of a live graph */
    bool steady;                /**< It is live and up to date, so its evaluations change no
                                 *   image a watching graph samples */
} box_gpu_tg_graph_t;

/** A materialized value of the cache, shared by the graphs whose node has its content hash.
 *
 * An entry the graphs use has references; one without is in the LRU list of the cache, most
 * recently used last, until it is evicted or used again.
 */
typedef struct box_gpu_tg_entry_t {
    uint64_t hash;              /**< Content hash of the node */
    box_gpu_tg_pooled_t value;  /**< Texture of the value, never in the pool */
    uint64_t bytes;             /**< Bytes of the texture */
    int32_t refs;               /**< Graphs that hold it */
    bool sampling;              /**< Its value depends on the image of a sampled texture, so
                                 *   it is freed when no graph holds it, and reused in place for
                                 *   the next image by the one graph that holds it */
    struct box_gpu_tg_entry_t *prev; /**< Previous entry of the LRU list */
    struct box_gpu_tg_entry_t *next; /**< Next entry of the LRU list */
    bool listed;                /**< It is in the LRU list */
} box_gpu_tg_entry_t;

/** Where a readback is */
typedef enum box_gpu_tg_read_state_t {
    BOX_GPU_TG_READ_WAITING,    /**< Waits for an evaluation or for the image */
    BOX_GPU_TG_READ_COPIED,     /**< The copy was recorded in this packet */
    BOX_GPU_TG_READ_MAPPING,    /**< The copy was submitted and maps */
    BOX_GPU_TG_READ_MAPPED,     /**< The copy mapped */
    BOX_GPU_TG_READ_FAILED      /**< The read cannot be done */
} box_gpu_tg_read_state_t;

/** A readback of a graph in flight */
typedef struct box_gpu_tg_read_t {
    box_render_texture_read_t read; /**< What was asked, and the size of the copy */
    box_gpu_tg_read_state_t state; /**< Where it is */
    WGPUBuffer buffer;          /**< The copy */
    uint32_t stride;            /**< Bytes of a row of the copy */
    WGPUTextureFormat format;   /**< Format of the texels of the copy */
} box_gpu_tg_read_t;

/** Where the timestamps of an evaluation are */
typedef enum box_gpu_tg_stamp_state_t {
    BOX_GPU_TG_STAMP_FREE,      /**< Unused */
    BOX_GPU_TG_STAMP_WRITTEN,   /**< A pass wrote them, resolved in the next packet */
    BOX_GPU_TG_STAMP_COPIED,    /**< Resolved and copied into the readback */
    BOX_GPU_TG_STAMP_MAPPING,   /**< The readback maps */
    BOX_GPU_TG_STAMP_MAPPED,    /**< The readback mapped */
    BOX_GPU_TG_STAMP_FAILED     /**< The map failed */
} box_gpu_tg_stamp_state_t;

/** Timestamps of the evaluations of one packet */
typedef struct box_gpu_tg_stamp_t {
    WGPUBuffer buffer;          /**< Readback of the two timestamps */
    box_gpu_tg_stamp_state_t state; /**< Where it is */
} box_gpu_tg_stamp_t;

/** What an evaluation of a packet holds until its copies are recorded */
typedef struct box_gpu_tg_eval_t {
    box_gpu_tg_graph_t *graph;  /**< The graph */
    box_gpu_tg_pooled_t **textures; /**< Intermediate of each materialized slot, by slot: the
                                 *   nodes, then the earlier passes */
    box_gpu_tg_pooled_t *temp;  /**< Texture the output stage writes before a blit, or NULL */
    box_gpu_tg_entry_t **entries; /**< Cache entry of each node it reads or writes, by node,
                                 *   which the graph holds after the evaluation */
    bool held;                  /**< The intermediates stay until the end of the packet */
    bool stored;                /**< The image of the handle was written */
    bool update;                /**< Only an update: the graph did not change, its animated
                                 *   nodes or the textures it samples did */
    int32_t key;                /**< -1 for an evaluation into the image, 0 or 1 for one into
                                 *   that key of an interpolated graph, BOX_GPU_TG_BLEND for the
                                 *   crossfade of its keys into the image */
    int64_t key_index;          /**< Key k an evaluation into a key evaluates, at k / rate */
    int32_t pass;               /**< Compute pass it is recorded in: a graph that samples the
                                 *   single channel image of a graph of the packet comes after
                                 *   the blit of that image */
} box_gpu_tg_eval_t;

/** Texture graphs of the backend */
typedef struct box_gpu_texgraphs_t {
    ecs_map_t kinds;            /**< Kind id to box_gpu_tg_kind_t* */
    ecs_map_t graphs;           /**< Texture handle to box_gpu_tg_graph_t* */
    ecs_map_t programs;         /**< Key to box_gpu_tg_program_t* */
    ecs_map_t stages;           /**< Shader handle of a stage to its box_gpu_tg_program_t* */
    ecs_vec_t layouts;          /**< box_gpu_tg_layout_t* */
    ecs_vec_t pool;             /**< box_gpu_tg_pooled_t* */
    ecs_vec_t reads;            /**< box_gpu_tg_read_t* */
    char *prelude;              /**< Shared code of the stages, NULL until it arrived */
    int32_t prelude_lines;      /**< Lines of prelude */
    uint64_t prelude_hash;      /**< Hash of prelude */
    uint32_t next_shader;       /**< Shader handle of the next stage */
    WGPUTexture none;           /**< 1x1 zero texture bound for inputs without a node */
    WGPUTextureView none_view;  /**< Its view */
    int64_t frame;              /**< Packets evaluated */
    WGPUQuerySet queries;       /**< Two timestamps per readback, NULL without the feature */
    WGPUBuffer resolve;         /**< Resolved timestamps */
    box_gpu_tg_stamp_t stamps[BOX_GPU_TG_STAMPS]; /**< Timestamp readbacks */
    ecs_map_t cache;            /**< Content hash to box_gpu_tg_entry_t* */
    box_gpu_tg_entry_t *lru_first; /**< Least recently used entry no graph holds */
    box_gpu_tg_entry_t *lru_last; /**< Most recently used entry no graph holds */
    uint64_t budget;            /**< Bytes the cache holds at most, evicting from the LRU list */
    double time;                /**< World time of the packet being evaluated */
    uint32_t blend_shaders[2];  /**< Shader handles of the crossfades of RGBA8Unorm and of
                                 *   RGBA16Float images, 0 before they are compiled */
    WGPUComputePipeline blend_pipelines[2]; /**< Their pipelines once created */
    uint64_t blend_prelude;     /**< Hash of the shared code they were compiled with */
    ecs_vec_t active;           /**< box_gpu_tg_graph_t*, the graphs every packet looks at */
    ecs_vec_t watchers;         /**< box_gpu_tg_graph_t*, the graphs looked at after an image
                                 *   changed */
    int32_t active_holes;       /**< Entries of active a graph left, NULL until compacted */
    int32_t watcher_holes;      /**< Entries of watchers a graph left, NULL until compacted */
    uint32_t serial;            /**< Serial of the last image written when the last packet was
                                 *   evaluated */
} box_gpu_texgraphs_t;

/** Get the bytes of a texel of a format of an intermediate, a cached value or a readback.
 *
 * @param format The format
 * @return The bytes
 */
static inline uint32_t box_gpu_tg_texel_size(
    WGPUTextureFormat format)
{
    return format == WGPUTextureFormat_RGBA32Float ? 16
        : format == WGPUTextureFormat_RGBA16Float ? 8
        : format == BOX_GPU_TEXTURE_SINGLE ? 1 : 4;
}

/** Get the format of the texture of the image of a graph.
 *
 * @param format The TextureGraphFormat
 * @return RGBA8Unorm for color and linear, R8Unorm for single, RGBA16Float for HDR,
 * R32Float and RGBA32Float for the float formats
 */
static inline WGPUTextureFormat box_gpu_tg_image_format(
    uint32_t format)
{
    return format == GraphSingle ? BOX_GPU_TEXTURE_SINGLE
        : format == GraphHdr ? WGPUTextureFormat_RGBA16Float
        : format == GraphFloat ? WGPUTextureFormat_R32Float
        : format == GraphFloat4 ? WGPUTextureFormat_RGBA32Float : BOX_GPU_TEXTURE_FORMAT;
}

/** Get the format the output stage of a graph writes: the image, or for an R8Unorm image an
 * RGBA16Float texture that a blit copies into it, R8Unorm not being a storage format.
 *
 * @param format The TextureGraphFormat
 * @return The format
 */
static inline WGPUTextureFormat box_gpu_tg_output_format(
    uint32_t format)
{
    return format == GraphColor || format == GraphLinear ? BOX_GPU_TEXTURE_FORMAT
        : format == GraphFloat ? WGPUTextureFormat_R32Float
        : format == GraphFloat4 ? WGPUTextureFormat_RGBA32Float
        : WGPUTextureFormat_RGBA16Float;
}

/** Get the size of the grid the stages of a graph before its output evaluate: its image grown
 * by its apron on each side on DomainWorld, its image on the other domains.
 *
 * @param desc The description of the graph
 * @param width Receives the width
 * @param height Receives the height
 */
static inline void box_gpu_tg_grid(
    const box_render_texture_graph_t *desc,
    uint32_t *width,
    uint32_t *height)
{
    *width = desc->width + 2 * desc->apron;
    *height = desc->height + 2 * desc->apron;
}

/** Fill the BoxTexGraphStage of a graph.
 *
 * @param desc The description of the graph
 * @param encode How the output stage stores its value
 * @param time Time of the evaluation, or factor of a crossfade
 * @param out Receives the 16 words
 */
static inline void box_gpu_tg_uniforms(
    const box_render_texture_graph_t *desc,
    uint32_t encode,
    float time,
    uint32_t out[16])
{
    ecs_os_memset(out, 0, 16 * ECS_SIZEOF(uint32_t));
    out[0] = desc->width;
    out[1] = desc->height;
    out[2] = desc->domain;
    out[3] = encode;
    ecs_os_memcpy(&out[4], &time, ECS_SIZEOF(float));
    out[5] = desc->apron;
    ecs_os_memcpy(&out[8], desc->region, ECS_SIZEOF(desc->region));
    ecs_os_memcpy(&out[12], desc->spacing, ECS_SIZEOF(desc->spacing));
}

/** Get a node kind by id.
 *
 * @param tg The graph state
 * @param id Id of the kind
 * @return The kind, NULL when the backend has none of the id
 */
static inline box_gpu_tg_kind_t* box_gpu_tg_kind(
    box_gpu_texgraphs_t *tg,
    uint32_t id)
{
    return ecs_map_get_deref(&tg->kinds, box_gpu_tg_kind_t, id);
}

/** Get the passes of a kind.
 *
 * @param kind The kind
 * @return The passes, from 1
 */
static inline int32_t box_gpu_tg_passes(
    const box_gpu_tg_kind_t *kind)
{
    return kind && kind->passes > 1 ? (int32_t)kind->passes : 1;
}

/** Generate the code of a stage: the shared code, the functions of its kinds, its bindings and
 * a main that loads its materialized inputs, calls the functions of its nodes in order and
 * stores the value of its root, or calls the function of its root for its pass and stores that.
 *
 * @param tg The graph state
 * @param graph The graph
 * @param stage The stage
 * @param owner The materialized node whose stage computes each node
 * @param first Slot of the first pass of each node of more than one pass, -1 for the others
 * @return The code, freed by the caller
 */
char* box_gpu_tg_code(
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    box_gpu_tg_stage_t *stage,
    const int32_t *owner,
    const int32_t *first);

/** Get the layouts of the stages with a number of inputs, of sampled textures and an output
 * format, creating them on first use.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param inputs Materialized inputs
 * @param textures Sampled textures
 * @param format Format of the storage texture
 * @return The layouts
 */
box_gpu_tg_layout_t* box_gpu_tg_layout(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    int32_t inputs,
    int32_t textures,
    WGPUTextureFormat format);

/** Compile the code of a stage under its handle through the shader path.
 *
 * @param gpu The backend
 * @param code The code
 * @param label Label of the module
 * @param handle Shader handle of the stage
 */
void box_gpu_tg_compile(
    box_gpu_t *gpu,
    const char *code,
    const char *label,
    uint32_t handle);

/** Generate the code of the crossfade of the two keys of an interpolated graph into its
 * image: the shared code, then a main that mixes box_texgraph_key0 and box_texgraph_key1 by
 * box_texgraph_stage.time, in linear light when box_texgraph_stage.encode is 1.
 *
 * @param tg The graph state, with its shared code
 * @param format Format of the image, RGBA8Unorm or RGBA16Float
 * @return The code, freed by the caller
 */
char* box_gpu_tg_blend_code(
    const box_gpu_texgraphs_t *tg,
    WGPUTextureFormat format);

/** Release the key images and their buffers of an interpolated graph.
 *
 * @param gpu The backend
 * @param graph The graph
 */
void box_gpu_tg_keyframes_free(
    box_gpu_t *gpu,
    box_gpu_tg_graph_t *graph);

/** Get a key image of an interpolated graph of the size and format its output stage writes,
 * creating it again when it has another.
 *
 * @param gpu The backend
 * @param graph The graph
 * @param slot The key, 0 or 1
 * @return The key image
 */
box_gpu_tg_pooled_t* box_gpu_tg_keyframe(
    box_gpu_t *gpu,
    box_gpu_tg_graph_t *graph,
    int32_t slot);

/** Release the crossfade shaders and pipelines.
 *
 * @param gpu The backend
 * @param tg The graph state
 */
void box_gpu_tg_blend_release(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg);

/** Check whether a graph is interpolated: animated, with a rate and TextureGraph.interpolate,
 * and an image a crossfade can store.
 *
 * @param graph The graph, its hashes computed
 * @return Whether it is
 */
bool box_gpu_tg_interpolated(
    const box_gpu_tg_graph_t *graph);

/** Plan the evaluations of an interpolated graph in a packet with key k of the clock: keys k
 * and k + 1 after a change, a readback, a changed source or a jump of the clock, else key
 * k + 1 into the older key once the clock reached the newer one; then the crossfade.
 *
 * The crossfade is skipped while the clock is paused. A graph no draw sampled does nothing
 * unless it changed, and nothing happens while the crossfade pipeline is created.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph, ready and interpolated
 * @param reads Readbacks of its nodes wait
 * @param evals Receives the box_gpu_tg_eval_t of the packet
 */
void box_gpu_tg_plan_keys(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    bool reads,
    ecs_vec_t *evals);

/** Record the crossfade of the two keys of an interpolated graph into the first level of its
 * image, by the fraction of the clock between them, and queue its mip levels.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param pass The compute pass
 * @param ev The evaluation of the crossfade
 */
void box_gpu_tg_blend(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    WGPUComputePassEncoder pass,
    box_gpu_tg_eval_t *ev);

/** Get the graph whose image a texture handle is.
 *
 * @param tg The graph state
 * @param handle The texture handle
 * @return The graph, NULL when the handle is no graph or a target
 */
box_gpu_tg_graph_t* box_gpu_tg_source_graph(
    box_gpu_texgraphs_t *tg,
    uint32_t handle);

/** Get the serial of the image a texture of a graph samples now: the serial of the image of its
 * handle, a new one in every packet for the image of a target, which renders every frame.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph
 * @param index Index of the texture in the textures of the graph
 * @return The serial, 0 for no image
 */
uint64_t box_gpu_tg_source_serial(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    int32_t index);

/** Put a graph in the evaluation order of the packet after the graphs whose images it samples;
 * a cycle, which the main thread fails, is cut where it closes.
 *
 * @param tg The graph state
 * @param graph The graph
 * @param order Receives box_gpu_tg_graph_t*
 */
void box_gpu_tg_order(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    ecs_vec_t *order);

/** Put a graph in a list of the graph state, taking it out of the one it is in.
 *
 * @param tg The graph state
 * @param graph The graph
 * @param track The list
 */
void box_gpu_tg_track(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    box_gpu_tg_track_t track);

/** Make every packet look at a graph until it is up to date again: it changed, its program
 * has to be made again or a readback asks for it.
 *
 * @param tg The graph state
 * @param graph The graph
 */
void box_gpu_tg_activate(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph);

/** Put the graphs a packet looks at in its evaluation order: the active ones, and the watchers
 * when an image was written since the last packet or an active graph is not steady, as its
 * evaluation writes an image they may sample.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param order Receives box_gpu_tg_graph_t*
 */
void box_gpu_tg_candidates(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    ecs_vec_t *order);

/** Put a graph a packet looked at in the list of its state after the packet: active while it
 * is not up to date, live or has readbacks waiting, else a watcher when it samples textures.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph
 * @param settled It is up to date: unchanged since its image was written, with the program
 * of its key, or it cannot be evaluated until it changes
 */
void box_gpu_tg_settle(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    bool settled);

/** Count a graph that a draw sampled as sampling the graphs whose images it samples, so an
 * animated graph that only another drawn graph samples keeps updating.
 *
 * @param tg The graph state
 * @param graph The graph
 */
void box_gpu_tg_propagate(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph);

/** Check whether an image a graph samples changed since its last evaluation: a graph it samples
 * evaluates in this packet, or the serial of an image differs from the one it read. The image
 * of a target changes every packet, which only counts while a draw samples the graph.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph, ready
 * @return Whether it evaluates again
 */
bool box_gpu_tg_sources_changed(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph);

/** Give the evaluations of a packet their compute passes: one that samples the single channel
 * image of a graph of the same pass starts the next one, after the blit of that image.
 *
 * @param tg The graph state
 * @param evals The evaluations, in order
 * @param count Number of evaluations
 * @return Number of passes
 */
int32_t box_gpu_tg_eval_passes(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_eval_t *evals,
    int32_t count);

/** Fail a readback and free it, removing it from the readbacks in flight.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param index Index of the readback in tg->reads
 */
void box_gpu_tg_read_fail(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    int32_t index);

/** Take a pooled intermediate of a size and format, creating it when none is free.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param width Width
 * @param height Height
 * @param format Format, R32Float or RGBA16Float
 * @return The intermediate, used until box_gpu_tg_pool_put
 */
box_gpu_tg_pooled_t* box_gpu_tg_pool_take(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format);

/** Return a pooled intermediate.
 *
 * @param p The intermediate, may be NULL
 */
void box_gpu_tg_pool_put(
    box_gpu_tg_pooled_t *p);

/** Release the pooled intermediates no graph used for BOX_GPU_TG_POOL_KEEP packets.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param all Release every free intermediate
 */
void box_gpu_tg_pool_sweep(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    bool all);

/** Poll the maps natively, then deliver the mapped readbacks unless delivery holds them and
 * add the mapped timestamps to the counters.
 *
 * @param gpu The backend
 * @param tg The graph state
 */
void box_gpu_tg_poll(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg);

/** Take the readbacks the main thread requested; a readback of a node the program of its graph
 * does not materialize adds the node to the forced boundaries of the graph.
 *
 * @param gpu The backend
 * @param tg The graph state
 */
void box_gpu_tg_requests(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg);

/** Check whether a graph has readbacks of a node waiting.
 *
 * @param tg The graph state
 * @param graph The graph
 * @return Whether one waits
 */
bool box_gpu_tg_node_reads(
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph);

/** Fail the readbacks of the nodes of a graph that wait, after the program that materializes
 * them failed.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param graph The graph
 */
void box_gpu_tg_node_reads_fail(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph);

/** Record the copies of the readbacks that can be made in this packet: the nodes and images of
 * the graphs it evaluated, the images of the other graphs whose image is up to date.
 *
 * @param gpu The backend
 * @param tg The graph state
 * @param encoder The encoder
 * @param evals The evaluations of the packet
 * @param eval_count Number of evaluations
 */
void box_gpu_tg_copies(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    WGPUCommandEncoder encoder,
    const box_gpu_tg_eval_t *evals,
    int32_t eval_count);

/** Create the timestamp queries of the evaluations when the device has the feature.
 *
 * @param gpu The backend
 * @param tg The graph state
 */
void box_gpu_tg_stamps_init(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg);

/** Record the resolve of the timestamps an earlier packet wrote: Metal writes them when the
 * pass completed, which a resolve in the same command buffer does not wait for.
 *
 * @param tg The graph state
 * @param encoder The encoder
 */
void box_gpu_tg_stamps_resolve(
    box_gpu_texgraphs_t *tg,
    WGPUCommandEncoder encoder);

/** Map the readbacks and timestamps whose copies were submitted.
 *
 * @param tg The graph state
 */
void box_gpu_tg_map(
    box_gpu_texgraphs_t *tg);

/** Check whether a packet has to record work for the graphs that are not evaluated: timestamps
 * to resolve or readbacks that wait.
 *
 * @param tg The graph state
 * @return Whether an encoder is needed
 */
bool box_gpu_tg_busy(
    const box_gpu_texgraphs_t *tg);

#endif

#endif
