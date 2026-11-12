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

/** Bytes after the parameters of a graph, so the four vec4 a node function gets stay in the
 * buffer for the last node */
#define BOX_GPU_TG_PARAM_PAD (64)

/** Bytes of BoxTexGraphStage */
#define BOX_GPU_TG_UNIFORM_SIZE (16)

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
    uint32_t flags;             /**< BOX_TEXTURE_NODE_NEIGHBOURHOOD, BOX_TEXTURE_NODE_ANIMATED */
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

/** Bind group and pipeline layout of the stages with a number of inputs and an output format */
typedef struct box_gpu_tg_layout_t {
    int32_t inputs;             /**< Materialized inputs */
    WGPUTextureFormat format;   /**< Format of the storage texture the stage writes */
    WGPUBindGroupLayout group;  /**< The bind group layout */
    WGPUPipelineLayout pipeline; /**< The pipeline layout */
} box_gpu_tg_layout_t;

/** A stage of a program: one compute shader that materializes one node */
typedef struct box_gpu_tg_stage_t {
    uint32_t shader;            /**< Shader handle of its module */
    int32_t root;               /**< Node whose value it stores */
    bool output;                /**< It stores the image */
    WGPUTextureFormat format;   /**< Format of the texture it writes */
    int32_t input_count;        /**< Materialized nodes it reads */
    int32_t inputs[BOX_GPU_TG_INPUTS]; /**< Those nodes, binding BOX_GPU_TG_FIRST_INPUT + i */
    box_gpu_tg_layout_t *layout; /**< Its layouts, owned by the graph state */
    WGPUComputePipeline pipeline; /**< Its pipeline once created */
    ecs_vec_t lines;            /**< box_gpu_tg_line_t of its code */
} box_gpu_tg_stage_t;

/** The stages of a structure, shared by the graphs whose key is the same */
typedef struct box_gpu_tg_program_t {
    uint64_t key;               /**< Key of the structure, see box_gpu_tg_key */
    int32_t refs;               /**< Graphs that use it as current or pending program */
    int32_t node_count;         /**< Nodes of the structure */
    int32_t stage_count;        /**< Stages in stages */
    box_gpu_tg_stage_t *stages; /**< The stages in order, the output stage last */
    bool failed;                /**< A stage did not compile, or the structure cannot be made */
    char *error;                /**< Why it failed */
    int32_t error_node;         /**< Node the error came from, -1 for none */
} box_gpu_tg_program_t;

/** A texture graph the main thread sent, under its texture handle */
typedef struct box_gpu_tg_graph_t {
    uint32_t handle;            /**< Texture handle */
    box_render_texture_graph_t desc; /**< Its description */
    box_render_texture_graph_node_t *nodes; /**< Its nodes */
    uint8_t *params;            /**< Its parameter bytes */
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
} box_gpu_tg_graph_t;

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
    box_gpu_tg_pooled_t **textures; /**< Intermediate of each materialized node, by node */
    box_gpu_tg_pooled_t *temp;  /**< Texture the output stage writes before a blit, or NULL */
    bool held;                  /**< The intermediates stay until the end of the packet */
    bool stored;                /**< The image of the handle was written */
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
} box_gpu_texgraphs_t;

/** Get the format of the texture of the image of a graph.
 *
 * @param format The TextureGraphFormat
 * @return RGBA8Unorm for color and linear, R8Unorm for single, RGBA16Float for HDR
 */
static inline WGPUTextureFormat box_gpu_tg_image_format(
    uint32_t format)
{
    return format == GraphSingle ? BOX_GPU_TEXTURE_SINGLE
        : format == GraphHdr ? WGPUTextureFormat_RGBA16Float : BOX_GPU_TEXTURE_FORMAT;
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
    return format == GraphColor || format == GraphLinear
        ? BOX_GPU_TEXTURE_FORMAT : WGPUTextureFormat_RGBA16Float;
}

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
