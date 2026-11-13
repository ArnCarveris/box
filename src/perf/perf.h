/**
 * @file perf/perf.h
 * @brief State of a performance run shared by the box.perf sources.
 */

#ifndef BOX_PERF_INTERNAL_H
#define BOX_PERF_INTERNAL_H

#include <box.h>

/** Target frame rate of a constructed PerfRun */
#define BOX_PERF_FPS (60.0f)

/** State of the run in progress, shared by the observer and the systems of the module */
typedef struct box_perf_t {
    PerfState state;      /**< Progress of the run */
    float fps;            /**< Target frame rate */
    int32_t frames;       /**< Frames to measure */
    int32_t warmup;       /**< Frames before the measurement */
    int32_t run;          /**< Frames completed since the run started */
    double mark;          /**< Clock at the start of the previous frame of the run, 0 for none */
    double start;         /**< Wall seconds when the orbit started */
    double measure;       /**< Wall seconds when the measurement started */
    double slept;         /**< AppStatus.slept when the measurement started */
    int32_t stalls;       /**< RenderStatus.stalls when the measurement started */
    bool bounds;          /**< The bounds were measured */
    vec3 center;          /**< Center of the bounds */
    float radius;         /**< Radius of the sphere around the bounds */
    float distance;       /**< Distance of the camera from the center */
    ecs_vec_t intervals;  /**< Measured frame intervals in milliseconds, of double */
    int64_t cull_frame;   /**< Render frame of the last culling sample, -1 for none */
    int32_t cull_last[5]; /**< Visible, frustum culled, occlusion culled, batches skipped and
                           *   casters skipped counts of the last sample */
    double cull_sum[5];   /**< Sums of those counts over the samples */
    double begin;         /**< Perf clock at the start of the current frame, 0 for none */
    double os_begin;      /**< OS clock at the start of the previous frame, 0 for none */
    double cpu;           /**< Process CPU seconds when the measurement started */
    double wall;          /**< OS clock when the measurement started */
    int32_t presented;    /**< RenderStatus.presented when the measurement started */
    bool boost;           /**< A fly run holds the boost key */
    double boost_time;    /**< Perf clock of the last boost toggle of a fly run */
    vec3 last;            /**< Position of the camera in the previous measured frame */
    bool has_last;        /**< last holds a position */
    int32_t bake_left;    /**< Probes the bakes had left when they last progressed */
    int32_t bake_stalled; /**< Warmup frames since the bakes last progressed */
    bool graph_sampled;   /**< graph_last holds the counts of a measured frame */
    int64_t graph_last[4]; /**< Texture graph hits, misses, evictions and updates the backend
                           *   statistics counted until the last measured frame */
    double graph_sum[5];  /**< Sums of the samples of those per frame and the cached kilobytes */
} box_perf_t;

/** Distribution of a series of durations */
typedef struct box_perf_stats_t {
    double min;   /**< Shortest duration */
    double mean;  /**< Mean duration */
    double p50;   /**< Median duration */
    double p99;   /**< 99th percentile duration, nearest rank */
    double max;   /**< Longest duration */
} box_perf_stats_t;

/** Replacement for the wall clock, NULL for the real one, lets tests drive time. */
extern double (*box_perf_clock)(void);

/** Measure the world space bounds of every shape with a world transform in the scene of a
 * camera.
 *
 * @param world The world
 * @param camera The camera whose scene the shapes belong to, 0 for the default scene
 * @param min The minimum corner to fill
 * @param max The maximum corner to fill
 * @return The number of shapes, 0 when there are none and the corners are not written
 */
int32_t box_perf_bounds(
    ecs_world_t *world,
    ecs_entity_t camera,
    vec3 min,
    vec3 max);

/** Compute the distribution of a series of durations.
 *
 * @param values The durations, of double
 * @param stats The distribution to fill, zero when there are no durations
 */
void box_perf_stats(
    const ecs_vec_t *values,
    box_perf_stats_t *stats);

/** Write the result of a run as JSON to PerfRun.report.
 *
 * @param world The world
 * @param run The run settings
 * @param status The completed status
 * @param intervals The frame intervals in milliseconds, of double
 * @return 0 on success, nonzero when the file could not be written
 */
int box_perf_report(
    ecs_world_t *world,
    const PerfRun *run,
    const PerfStatus *status,
    const ecs_vec_t *intervals);

#endif
