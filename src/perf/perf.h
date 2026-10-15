/**
 * @file perf/perf.h
 * @brief State of a performance run shared by the box.perf sources.
 */

#ifndef BOX_PERF_INTERNAL_H
#define BOX_PERF_INTERNAL_H

#include <box.h>

/** Target frame rate of a constructed PerfRun */
#define BOX_PERF_FPS (60.0f)

/** Seconds of CPU time per frame the real-time schedule of the pacing thread asks for at least */
#define BOX_PERF_MIN_COMPUTATION (0.0005)

/** Fraction of the frame interval the real-time schedule asks for at most */
#define BOX_PERF_MAX_COMPUTATION (0.75)

/** Real-time schedule of the thread that paces a run */
typedef struct box_perf_priority_t {
    bool active;      /**< The thread runs with a real-time schedule that must be reset */
    bool refused;     /**< The platform refused the schedule, the run keeps the standard one */
    uint64_t thread;  /**< Platform handle of the thread */
} box_perf_priority_t;

/** State of the run in progress, shared by the observer and the systems of the module */
typedef struct box_perf_t {
    PerfState state;      /**< Progress of the run */
    float fps;            /**< Target frame rate */
    int32_t frames;       /**< Frames to measure */
    int32_t warmup;       /**< Frames before the measurement */
    int32_t run;          /**< Frames completed since the run started */
    double mark;          /**< Wall seconds at the end of the previous paced frame, 0 for none */
    double start;         /**< Wall seconds when the orbit started */
    double measure;       /**< Wall seconds when the measurement started */
    double slept;         /**< AppStatus.slept when the measurement started */
    int32_t stalls;       /**< RenderStatus.stalls when the measurement started */
    bool bounds;          /**< The bounds were measured */
    vec3 center;          /**< Center of the bounds */
    float radius;         /**< Radius of the sphere around the bounds */
    float distance;       /**< Distance of the camera from the center */
    ecs_vec_t intervals;  /**< Measured frame intervals in milliseconds, of double */
    ecs_vec_t work;       /**< Milliseconds before the wait of the paced frames of the phase */
    int64_t cull_frame;   /**< Render frame of the last culling sample, -1 for none */
    int32_t cull_last[3]; /**< Visible, frustum and occlusion culled counts of the last sample */
    double cull_sum[3];   /**< Sums of the visible, frustum and occlusion culled samples */
    box_perf_priority_t priority; /**< Schedule of the pacing thread during the run */
    double begin;         /**< Perf clock at the start of the current frame, 0 for none */
    double os_begin;      /**< OS clock at the start of the previous frame, 0 for none */
    double cpu;           /**< Process CPU seconds when the measurement started */
    double wall;          /**< OS clock when the measurement started */
    int32_t presented;    /**< RenderStatus.presented when the measurement started */
    int32_t presents_bad; /**< Uneven presents of RenderStatus when the measurement started */
    int32_t mismatches;   /**< RenderStatus.delta_mismatches when the measurement started */
    bool boost;           /**< A fly run holds the boost key */
    double boost_time;    /**< Perf clock of the last boost toggle of a fly run */
    vec3 last;            /**< Position of the camera in the previous measured frame */
    bool has_last;        /**< last holds a position */
    double wake;          /**< Seconds the pacing thread is learned to wake after a sleep ends */
    double wake_jitter;   /**< Mean deviation of the wake up latency from wake */
    int32_t wake_samples; /**< Latency samples taken during the run */
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

/** Replacement for sleeping, NULL for the real sleep. Receives the seconds to sleep. */
extern void (*box_perf_sleep)(double seconds);

/** Give the calling thread a real-time schedule of one frame per interval: on macOS a
 * time-constraint policy, on Linux SCHED_FIFO where the user may (CAP_SYS_NICE or an RLIMIT_RTPRIO
 * above 0). Other platforms keep their schedule and rely on the learned wake up latency.
 *
 * @param priority The schedule to update, a schedule of another thread is reset first
 * @param interval Seconds per frame, the period and the deadline of every frame
 * @param computation Seconds of CPU time a frame needs, clamped to a sane range
 * @return Whether the thread runs with the real-time schedule
 */
bool box_perf_priority_set(
    box_perf_priority_t *priority,
    double interval,
    double computation);

/** Give the thread of a real-time schedule its standard schedule back.
 *
 * @param priority The schedule, nothing happens when it is not active
 */
void box_perf_priority_reset(
    box_perf_priority_t *priority);

/** Measure the world space bounds of every shape with a world transform.
 *
 * @param world The world
 * @param min The minimum corner to fill
 * @param max The maximum corner to fill
 * @return The number of shapes, 0 when there are none and the corners are not written
 */
int32_t box_perf_bounds(
    ecs_world_t *world,
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
