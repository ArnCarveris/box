/**
 * @file box/perf.h
 * @brief Performance runs: an orbiting camera fitted to the scene and frame time checks.
 *
 * Setting the PerfRun singleton starts a run that paces frames to a target rate, orbits a camera
 * around the bounds of every shape and counts each frame interval outside 10% of the target.
 */

#ifndef BOX_PERF_H
#define BOX_PERF_H

#include "types.h"

#ifdef BOX_PERF_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Fraction of the target frame interval a frame may deviate before it is an error */
#define BOX_PERF_TOLERANCE (0.1)

/** Seconds of frames measured by a run that does not set frames */
#define BOX_PERF_SECONDS (5)

/** Seconds per revolution of a run that does not set period */
#define BOX_PERF_PERIOD (10.0f)

/** Degrees above the horizon of a run that does not set elevation */
#define BOX_PERF_ELEVATION (30.0f)

/** Fewest warmup frames, the bounds are measured once the first frame placed the shapes */
#define BOX_PERF_MIN_WARMUP (2)

/** Progress of a performance run */
ECS_ENUM(PerfState, {
    PerfIdle = 0,    /**< No run was started */
    PerfWarmup = 1,  /**< Frames are paced and the camera orbits, nothing is measured */
    PerfMeasure = 2, /**< Every frame interval is measured */
    PerfDone = 3,    /**< The run completed, PerfStatus holds the result */
    PerfInvalid = 4  /**< The settings are invalid, the run did not start */
});

/** Settings of a performance run, setting the singleton (re)starts the run.
 *
 * The run sets App.fps to its target. Every frame sleeps until a full target interval passed
 * since the previous one, so only frames that take too long can miss the target.
 */
ECS_STRUCT(PerfRun, {
    float fps;            /**< Target frame rate: 30, 60, 90 or 120 */
    int32_t frames;       /**< Frames measured, 0 for five seconds of frames */
    int32_t warmup;       /**< Frames before the measurement, 0 for one second, at least 2 */
    ecs_entity_t camera;  /**< Camera that orbits, 0 for the first entity with Camera */
    float period;         /**< Seconds per revolution of the camera, 0 for 10 */
    float elevation;      /**< Degrees the camera looks down on the center, 0 for 30 */
    float stall;          /**< Milliseconds of busy work added to every frame, to test checks */
    bool quit;            /**< Quit the world when the run completes */
    char *report;         /**< JSON file the result is written to, NULL for none */
});

/** Result of a performance run, written by the engine and read only for applications */
ECS_STRUCT(PerfStatus, {
    PerfState state;      /**< Progress of the run */
    bool passed;          /**< The run completed every frame without an error */
    int32_t frame;        /**< Frames measured so far */
    int32_t errors;       /**< Frames outside the tolerance, slow plus fast */
    int32_t slow;         /**< Frames longer than 1.1 target intervals */
    int32_t fast;         /**< Frames shorter than 0.9 target intervals */
    double target_ms;     /**< Target frame interval in milliseconds */
    double min_ms;        /**< Shortest frame interval */
    double mean_ms;       /**< Mean frame interval */
    double p50_ms;        /**< Median frame interval */
    double p99_ms;        /**< 99th percentile frame interval */
    double max_ms;        /**< Longest frame interval */
    double work_mean_ms;  /**< Mean time a frame worked before the pacer waited */
    double work_p99_ms;   /**< 99th percentile time a frame worked before the pacer waited */
    double work_max_ms;   /**< Longest time a frame worked before the pacer waited */
    double slept_ms;      /**< Milliseconds the CPU budget of App slept during the measurement */
    int32_t stalls;       /**< Frames that waited for a free render packet during the measurement */
    int32_t shapes;       /**< Shapes the bounds were measured from */
    vec3 center;          /**< Center of the bounds the camera orbits */
    float radius;         /**< Radius of the sphere around the bounds */
    float distance;       /**< Distance of the camera from the center */
});

/** Register the box.perf module.
 *
 * @param world The world
 */
void BoxPerfImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
