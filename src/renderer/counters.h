/**
 * @file renderer/counters.h
 * @brief Per frame counts of the work the renderer recorded, usable without WebGPU headers.
 *
 * A frame holds totals and, for the details the world asked for, tables per
 * pass, pipeline, bind group slot and buffer usage. The functions fill them.
 */

#ifndef BOX_RENDERER_COUNTERS_H
#define BOX_RENDERER_COUNTERS_H

#include <stdint.h>
#include <stdbool.h>

/** Count the totals of a frame */
#define BOX_COUNT_FRAME (1u << 0)

/** Count per pass, which the compute table reads too */
#define BOX_COUNT_PASSES (1u << 1)

/** Count per pipeline */
#define BOX_COUNT_PIPELINES (1u << 2)

/** Count per pass and bind group slot */
#define BOX_COUNT_BIND_GROUPS (1u << 3)

/** Count per buffer usage */
#define BOX_COUNT_BUFFERS (1u << 4)

/** Count per compute pass */
#define BOX_COUNT_COMPUTE (1u << 5)

/** Characters of a pass or pipeline name, its terminator included */
#define BOX_COUNT_NAME_MAX (24)

/** Pass names a frame can tell apart */
#define BOX_COUNT_PASS_MAX (48)

/** Pipelines a frame can tell apart */
#define BOX_COUNT_PIPELINE_MAX (96)

/** Pass and slot pairs a frame can tell apart */
#define BOX_COUNT_BIND_MAX (128)

/** Bind group slots a pass tracks */
#define BOX_COUNT_SLOT_MAX (8)

/** Primitive kinds a draw can assemble */
typedef enum box_count_topology_t {
    BOX_COUNT_TRIANGLES = 0, /**< A triangle list */
    BOX_COUNT_STRIP = 1,     /**< A triangle strip */
    BOX_COUNT_LINES = 2,     /**< Lines or points, no triangles */
} box_count_topology_t;

/** Usage classes a buffer is counted under, by the first usage it has in this order */
typedef enum box_count_usage_t {
    BOX_COUNT_USAGE_INDIRECT = 0, /**< Indirect draw or dispatch arguments */
    BOX_COUNT_USAGE_VERTEX = 1,   /**< Vertex or instance data */
    BOX_COUNT_USAGE_INDEX = 2,    /**< Indices */
    BOX_COUNT_USAGE_UNIFORM = 3,  /**< Uniforms */
    BOX_COUNT_USAGE_STORAGE = 4,  /**< Storage */
    BOX_COUNT_USAGE_READBACK = 5, /**< Mappable copies read by the CPU */
    BOX_COUNT_USAGE_QUERY = 6,    /**< Resolved queries */
    BOX_COUNT_USAGE_OTHER = 7,    /**< Anything else */
    BOX_COUNT_USAGE_COUNT = 8     /**< Number of classes */
} box_count_usage_t;

/** What the passes of one name recorded in a frame */
typedef struct box_count_pass_t {
    char name[BOX_COUNT_NAME_MAX]; /**< Name the pass was begun under */
    bool compute;                  /**< The pass is a compute pass */
    uint32_t recorded;             /**< Passes of this name the frame began */
    uint32_t draws;                /**< Draw calls */
    uint32_t dispatches;           /**< Compute dispatches */
    uint64_t workgroups;           /**< Workgroups those dispatches launched */
    uint64_t instances;            /**< Instances the draws rendered */
    uint64_t triangles;            /**< Triangles the draws rendered */
    uint32_t pipeline_sets;        /**< Pipeline set calls */
    uint32_t pipeline_switches;    /**< Pipeline set calls that bound another pipeline */
    uint32_t bind_sets;            /**< Bind group set calls */
    uint32_t bind_changes;         /**< Bind group set calls that changed a slot */
} box_count_pass_t;

/** What one pipeline recorded in a frame */
typedef struct box_count_pipeline_t {
    const void *handle;            /**< The pipeline */
    char name[BOX_COUNT_NAME_MAX]; /**< Label it was created with */
    bool compute;                  /**< The pipeline is a compute pipeline */
    uint32_t sets;                 /**< Set calls that named it */
    uint32_t switches;             /**< Set calls that bound it over another one */
    uint32_t draws;                /**< Draw calls made while it was bound */
    uint32_t dispatches;           /**< Dispatches made while it was bound */
    uint64_t instances;            /**< Instances those draws rendered */
    uint64_t triangles;            /**< Triangles those draws rendered */
} box_count_pipeline_t;

/** Bind group set calls of one slot of the passes of one name */
typedef struct box_count_bind_t {
    uint32_t pass;    /**< Index of the pass in the pass table */
    uint32_t slot;    /**< Bind group slot */
    uint32_t sets;    /**< Set calls */
    uint32_t changes; /**< Set calls that bound another group or other offsets */
} box_count_bind_t;

/** Buffers of one usage class and what the frame uploaded into them */
typedef struct box_count_buffer_t {
    uint32_t buffers;      /**< Live buffers */
    uint64_t bytes;        /**< Bytes those buffers hold */
    uint32_t uploads;      /**< Queue writes into them */
    uint64_t upload_bytes; /**< Bytes those writes uploaded */
} box_count_buffer_t;

/** Everything the renderer counted in one frame.
 *
 * The totals are always filled; a table is filled when its flag was asked for.
 * The instances and triangles of indirect draws come from a read back of their
 * arguments and are part of the frame once indirect_resolved is set.
 */
typedef struct box_counters_t {
    uint32_t frame;              /**< Frame of the renderer the counts belong to */
    uint32_t flags;              /**< BOX_COUNT_ flags the frame was counted with */
    bool indirect_resolved;      /**< The indirect draws are part of the instances */
    uint32_t draws;              /**< Draw calls */
    uint32_t indirect_draws;     /**< Draw calls that took their arguments from a buffer */
    uint32_t render_passes;      /**< Render passes */
    uint32_t compute_passes;     /**< Compute passes */
    uint32_t pipeline_sets;      /**< Pipeline set calls */
    uint32_t pipeline_switches;  /**< Pipeline set calls that bound another pipeline */
    uint32_t bind_sets;          /**< Bind group set calls */
    uint32_t bind_changes;       /**< Bind group set calls that changed a slot */
    uint32_t buffer_uploads;     /**< Queue writes into buffers */
    uint64_t buffer_upload_bytes; /**< Bytes those writes uploaded */
    uint32_t texture_uploads;    /**< Queue writes into textures */
    uint64_t texture_upload_bytes; /**< Bytes those writes uploaded */
    uint32_t dispatches;         /**< Compute dispatches */
    uint64_t workgroups;         /**< Workgroups those dispatches launched */
    uint64_t instances;          /**< Instances the draws rendered */
    uint64_t triangles;          /**< Triangles the draws rendered */
    uint32_t buffers;            /**< Live buffers at the end of the frame */
    uint64_t buffer_memory;      /**< Bytes those buffers hold */
    uint32_t textures;           /**< Live textures at the end of the frame */
    uint64_t texture_memory;     /**< Bytes those textures hold */
    uint64_t query_memory;       /**< Bytes the query sets hold */
    uint32_t pass_count;         /**< Entries of the pass table */
    uint32_t pipeline_count;     /**< Entries of the pipeline table */
    uint32_t bind_count;         /**< Entries of the bind group table */
    box_count_pass_t passes[BOX_COUNT_PASS_MAX];             /**< Passes, in record order */
    box_count_pipeline_t pipelines[BOX_COUNT_PIPELINE_MAX];  /**< Pipelines, in bind order */
    box_count_bind_t binds[BOX_COUNT_BIND_MAX];              /**< Slots, in bind order */
    box_count_buffer_t usages[BOX_COUNT_USAGE_COUNT];        /**< Buffers per usage class */
} box_counters_t;

/** Triangles a draw assembles.
 *
 * @param topology The primitive kind of the pipeline
 * @param vertices Vertices or indices of one instance
 * @param instances Instances the draw renders
 * @return The triangles
 */
uint64_t box_count_triangles(
    box_count_topology_t topology,
    uint32_t vertices,
    uint32_t instances);

/** Entry of a pass name in the pass table, added when the name is new.
 *
 * @param c The counts
 * @param name Name of the pass, truncated to BOX_COUNT_NAME_MAX
 * @param compute The pass is a compute pass
 * @return Index of the entry, -1 when the table is full
 */
int32_t box_count_pass(
    box_counters_t *c,
    const char *name,
    bool compute);

/** Entry of a pipeline in the pipeline table, added when the pipeline is new.
 *
 * @param c The counts
 * @param handle The pipeline
 * @param name Label of the pipeline, truncated to BOX_COUNT_NAME_MAX
 * @param compute The pipeline is a compute pipeline
 * @return Index of the entry, -1 when the table is full
 */
int32_t box_count_pipeline(
    box_counters_t *c,
    const void *handle,
    const char *name,
    bool compute);

/** Entry of a pass and slot pair in the bind group table, added when it is new.
 *
 * @param c The counts
 * @param pass Index of the pass in the pass table
 * @param slot Bind group slot
 * @return Index of the entry, -1 when the table is full
 */
int32_t box_count_bind(
    box_counters_t *c,
    uint32_t pass,
    uint32_t slot);

/** Name of a buffer usage class.
 *
 * @param usage The class
 * @return The name, "other" for a value out of range
 */
const char* box_count_usage_name(
    uint32_t usage);

/** Copy a name into a fixed buffer, truncating what does not fit.
 *
 * @param dst The buffer, BOX_COUNT_NAME_MAX characters
 * @param name The name, NULL for an empty one
 * @param length Characters of the name, SIZE_MAX when it is terminated
 */
void box_count_name(
    char *dst,
    const char *name,
    uint64_t length);

#endif
