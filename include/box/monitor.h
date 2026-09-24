/**
 * @file box/monitor.h
 * @brief CPU, flecs statistics and per system frame timings published as components.
 *
 * A source is only sampled while its singleton exists, so a monitor that is
 * absent costs nothing. Every value is a reflected field, so the terminal, the
 * REST API and the status page read it without a dedicated endpoint.
 */

#ifndef BOX_MONITOR_H
#define BOX_MONITOR_H

#include "types.h"

#ifdef BOX_MONITOR_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Entries of the slowest lists of a monitor, which the ECS_STRUCTs spell out */
#define BOX_MONITOR_TOP (10)

/** Entries of the phase list of a system monitor, which the ECS_STRUCT spells out */
#define BOX_MONITOR_PHASES (8)

/** Render passes a GPU monitor reports, which the ECS_STRUCT spells out */
#define BOX_MONITOR_PASSES (16)

/** Seconds a CPU monitor averages its samples over by default */
#define BOX_MONITOR_WINDOW (1.0)

/** Seconds between two statistics collections by default */
#define BOX_MONITOR_PERIOD (1.0)

/** Frame times a statistics collection keeps by default */
#define BOX_MONITOR_HISTORY (60)

/** Frame times a statistics collection can keep */
#define BOX_MONITOR_HISTORY_MAX (240)

/** Process and machine CPU usage, sampled every frame while the singleton exists.
 *
 * The percentages are a moving average over window_s, where process_percent is a
 * share of one core and system_percent a share of every core.
 */
ECS_STRUCT(CpuMonitor, {
    double window_s;         /**< Seconds the moving average covers, default 1 */
    int32_t cores;           /**< Cores of the machine, written by the engine */
    double process_percent;  /**< Percent of one core this process used */
    double system_percent;   /**< Percent of every core the machine used */
    double core_percent_max; /**< Percent the busiest core of the machine used */
    double budget_sleep_ms;  /**< Milliseconds the CPU budget slept last frame */
});

/** A system and the time it spent per frame over the window of its list */
ECS_STRUCT(SystemTime, {
    ecs_entity_t system; /**< The system that ran */
    double time_ms;      /**< Milliseconds it spent per frame on average */
    double min_ms;       /**< Milliseconds of its cheapest frame in the window */
    double max_ms;       /**< Milliseconds of its most expensive frame in the window */
});

/** A pipeline phase and the time its systems spent in one frame */
ECS_STRUCT(PhaseTime, {
    ecs_entity_t phase; /**< The phase its systems depend on */
    double time_ms;     /**< Milliseconds those systems spent */
});

/** Flecs statistics mirrored into fields while the singleton exists.
 *
 * Setting the component imports the flecs stats addon and starts collection;
 * removing it stops collection and disables the monitors of the addon again
 * when this component imported it.
 *
 * Values are refreshed every period_s seconds, which marks the component
 * modified. The slowest list covers the frames of the last period, at most the
 * sixty samples the addon keeps, with the average, minimum and maximum per frame.
 */
ECS_STRUCT(EcsStats, {
    double period_s;          /**< Seconds between two collections, default 1 */
    int32_t history;          /**< Frame times kept, default 60, at most 240 */
    int32_t entity_count;     /**< Alive entities at the last collection */
    int32_t table_count;      /**< Tables at the last collection */
    int32_t systems_ran;      /**< Systems that ran in the last frame */
    int32_t collections;      /**< Collections since the component was set */
    double frame_time_ms;     /**< Milliseconds of the last frame */
    double frame_time_avg_ms; /**< Milliseconds of the frames in the window */
    double system_time_ms;    /**< Milliseconds the last frame spent in systems */
    double merge_time_ms;     /**< Milliseconds the last frame spent merging */
    double fps;               /**< Frames per second at the last collection */
    SystemTime slowest[10];   /**< Slowest systems of the period by average, slowest first */
});

/** Per system and per phase frame timings, collected while enabled.
 *
 * Setting enabled turns on the flecs system time measurement, which the monitor
 * reads per frame; the totals cover the frame before the one being collected.
 * The component is marked modified after every frame it collected.
 */
ECS_STRUCT(SystemMonitor, {
    bool enabled;          /**< Collect per system timings, default false */
    double frame_ms;       /**< Milliseconds every system spent in the frame */
    int32_t system_count;  /**< Systems that spent time in the frame */
    SystemTime slowest[10]; /**< Slowest systems of the frame, slowest first */
    PhaseTime phases[8];    /**< Milliseconds per phase, in pipeline order */
});

/** Name of a render pass and the time it spent on the GPU in one frame */
ECS_STRUCT(GpuPassTime, {
    char *name;     /**< Name of the pass, NULL when the entry is unused */
    double time_ms; /**< Milliseconds the GPU spent in the pass */
});

/** Per pass GPU timings of the rendered frames, collected while the singleton exists.
 *
 * Timestamps are read back one or two frames later, the frame the numbers name.
 * A pass name recorded by several views or cascades sums their time. A device
 * without timestamp queries reports no passes.
 */
ECS_STRUCT(GpuTimings, {
    bool supported;         /**< The device can time its passes, written by the engine */
    int32_t frame;          /**< Frame of the renderer the passes were measured in */
    int32_t pass_count;     /**< Passes that frame recorded, at most sixteen */
    double frame_ms;        /**< Milliseconds those passes add up to */
    GpuPassTime passes[16]; /**< The passes, in the order the frame recorded them */
});

/** Counts of the last frame the renderer finished, collected while the singleton exists.
 *
 * Every value belongs to one frame, published once the frame is whole, which
 * includes the read back of the arguments of its indirect draws, so the numbers
 * trail the frame they name by one or two frames.
 *
 * Memory is what the buffers, textures and query sets the renderer created hold
 * at the end of that frame, by the size they were created with.
 */
ECS_STRUCT(RendererStats, {
    int32_t frame;               /**< Frame of the renderer the counts belong to */
    bool indirect_resolved;      /**< Instances and triangles include the indirect draws */
    int32_t draw_calls;          /**< Draw calls */
    int32_t indirect_draw_calls; /**< Draw calls that read their arguments from a buffer */
    int32_t render_passes;       /**< Render passes */
    int32_t compute_passes;      /**< Compute passes */
    int32_t pipeline_switches;   /**< Pipeline set calls that bound another pipeline */
    int32_t bind_group_changes;  /**< Bind group set calls that changed a slot */
    int32_t buffer_uploads;      /**< Queue writes into buffers */
    int64_t bytes_uploaded;      /**< Bytes the buffer and texture writes uploaded */
    int32_t texture_uploads;     /**< Queue writes into textures */
    int32_t compute_dispatches;  /**< Compute dispatches */
    int64_t instances;           /**< Instances the draws rendered */
    int64_t triangles;           /**< Triangles the draws rendered */
    int64_t gpu_memory;          /**< Bytes of every buffer, texture and query set */
    int64_t buffer_memory;       /**< Bytes of the buffers */
    int64_t texture_memory;      /**< Bytes of the textures */
    int32_t buffers;             /**< Live buffers */
    int32_t textures;            /**< Live textures */
});

/** Rows a detail statistics component holds, which the ECS_STRUCTs spell out */
#define BOX_MONITOR_STAT_ROWS (16)

/** Buffer usage classes a BufferStats component holds, which the ECS_STRUCT spells out */
#define BOX_MONITOR_USAGES (8)

/** What the passes of one name recorded in a frame */
ECS_STRUCT(PassStat, {
    char *name;                 /**< Name of the pass, NULL when the row is unused */
    bool compute;               /**< The pass is a compute pass */
    int32_t recorded;           /**< Passes of this name the frame began */
    int32_t draw_calls;         /**< Draw calls */
    int32_t dispatches;         /**< Compute dispatches */
    int64_t instances;          /**< Instances the draws rendered */
    int64_t triangles;          /**< Triangles the draws rendered */
    int32_t pipeline_switches;  /**< Pipeline set calls that bound another pipeline */
    int32_t bind_group_changes; /**< Bind group set calls that changed a slot */
});

/** Per pass counts of the last frame the renderer finished, while the singleton exists.
 *
 * Passes of one name, such as a pass per view or per cascade, share a row. The
 * rows are in the order the frame first recorded each name.
 */
ECS_STRUCT(PassStats, {
    int32_t frame;         /**< Frame of the renderer the counts belong to */
    int32_t total;         /**< Pass names the frame recorded */
    int32_t count;         /**< Rows in use, at most sixteen */
    PassStat passes[16];   /**< The passes, in record order */
});

/** What one pipeline recorded in a frame */
ECS_STRUCT(PipelineStat, {
    char *name;         /**< Label of the pipeline, NULL when the row is unused */
    bool compute;       /**< The pipeline is a compute pipeline */
    int32_t sets;       /**< Set calls that named it */
    int32_t switches;   /**< Set calls that bound it over another pipeline */
    int32_t draw_calls; /**< Draw calls made while it was bound */
    int32_t dispatches; /**< Dispatches made while it was bound */
    int64_t instances;  /**< Instances those draws rendered */
    int64_t triangles;  /**< Triangles those draws rendered */
});

/** Per pipeline counts of the last frame the renderer finished, while the singleton exists.
 *
 * A pipeline is named by the label it was created with. The rows hold the
 * pipelines with the most switches, most first.
 */
ECS_STRUCT(PipelineStats, {
    int32_t frame;              /**< Frame of the renderer the counts belong to */
    int32_t total;              /**< Pipelines the frame bound */
    int32_t count;              /**< Rows in use, at most sixteen */
    int32_t sets;               /**< Pipeline set calls of the frame */
    int32_t switches;           /**< Of those, the calls that bound another pipeline */
    PipelineStat pipelines[16]; /**< The pipelines, most switches first */
});

/** Bind group set calls of one slot of the passes of one name */
ECS_STRUCT(BindGroupStat, {
    char *pass;      /**< Name of the pass, NULL when the row is unused */
    int32_t slot;    /**< Bind group slot */
    int32_t sets;    /**< Set calls */
    int32_t changes; /**< Set calls that bound another group or other dynamic offsets */
});

/** Per pass and slot bind group counts of the last finished frame, while the singleton exists.
 *
 * A set call that binds the group and offsets a slot already holds is a set
 * but no change. The rows hold the slots with the most changes, most first.
 */
ECS_STRUCT(BindGroupStats, {
    int32_t frame;           /**< Frame of the renderer the counts belong to */
    int32_t total;           /**< Pass and slot pairs the frame bound */
    int32_t count;           /**< Rows in use, at most sixteen */
    int32_t sets;            /**< Bind group set calls of the frame */
    int32_t changes;         /**< Of those, the calls that changed a slot */
    BindGroupStat slots[16]; /**< The slots, most changes first */
});

/** Buffers of one usage class and what the frame uploaded into them */
ECS_STRUCT(BufferStat, {
    char *usage;          /**< Name of the usage class */
    int32_t buffers;      /**< Live buffers */
    int64_t bytes;        /**< Bytes those buffers hold */
    int32_t uploads;      /**< Queue writes into them */
    int64_t upload_bytes; /**< Bytes those writes uploaded */
});

/** Buffers per usage class of the last frame the renderer finished, while the singleton exists.
 *
 * A buffer is counted under the first of indirect, vertex, index, uniform,
 * storage, readback and query its usage has, else under other.
 */
ECS_STRUCT(BufferStats, {
    int32_t frame;         /**< Frame of the renderer the counts belong to */
    int32_t buffers;       /**< Live buffers */
    int64_t bytes;         /**< Bytes they hold */
    int32_t uploads;       /**< Queue writes into buffers */
    int64_t upload_bytes;  /**< Bytes those writes uploaded */
    BufferStat usages[8];  /**< The usage classes, in the order above */
});

/** What the compute passes of one name recorded in a frame */
ECS_STRUCT(ComputeStat, {
    char *name;                 /**< Name of the pass, NULL when the row is unused */
    int32_t recorded;           /**< Passes of this name the frame began */
    int32_t dispatches;         /**< Dispatches */
    int64_t workgroups;         /**< Workgroups those dispatches launched */
    int32_t pipeline_switches;  /**< Pipeline set calls that bound another pipeline */
    int32_t bind_group_changes; /**< Bind group set calls that changed a slot */
});

/** Per compute pass counts of the last finished frame, while the singleton exists.
 *
 * Compute passes of one name share a row, in the order the frame first
 * recorded each name.
 */
ECS_STRUCT(ComputeStats, {
    int32_t frame;          /**< Frame of the renderer the counts belong to */
    int32_t total;          /**< Compute pass names the frame recorded */
    int32_t count;          /**< Rows in use, at most sixteen */
    int32_t dispatches;     /**< Dispatches of the frame */
    int64_t workgroups;     /**< Workgroups they launched */
    ComputeStat passes[16]; /**< The compute passes, in record order */
});

/** Script of the overlay, loaded at import into box.monitor.overlay_script.
 *
 * It includes etc/ui/overlay/stats.flecs, which lays the statistics panels out in
 * box.monitor.stats with an exported mut variable per panel, and the widget script
 * of every panel, whose templates live in box.monitor.panels.
 *
 * It also includes the terminal of the console, which exists while the world has
 * a box.ui.Console.
 */
#define BOX_MONITOR_OVERLAY "etc/ui/overlay/overlay.flecs"

/** Frame times the history of the Fps singleton keeps, which the ECS_STRUCT spells out */
#define BOX_MONITOR_FPS_HISTORY (120)

/** Frame rate of the last second and the most recent frame times.
 *
 * Filled and marked modified every frame. The panel shows while the variable
 * box.monitor.stats.fps is true. The history is oldest first, and slots before
 * the first recorded frame are zero.
 */
ECS_STRUCT(Fps, {
    double fps;          /**< Frames per second over the last second */
    double mean_ms;      /**< Mean frame time of the last second */
    double worst_ms;     /**< Worst frame time of the last second */
    double frame_ms;     /**< Milliseconds of the most recent frame */
    int32_t count;       /**< Frame times the history holds, at most its size */
    float history[120];  /**< Most recent frame times in milliseconds, newest last */
});

/** Register the box.monitor module.
 *
 * @param world The world
 */
void BoxMonitorImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
