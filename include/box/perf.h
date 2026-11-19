/**
 * @file box/perf.h
 * @brief Performance runs: an orbiting camera fitted to the scene and frame time checks.
 *
 * Setting the PerfRun singleton starts a run that caps the frames at a target rate with App.fps,
 * orbits a camera around the bounds of every shape (or flies it with its controller) and counts
 * frame intervals outside 10% of the target and delta_times that differ from the wall time.
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

/** Seconds of frames a constructed run measures, at its frame rate */
#define BOX_PERF_SECONDS (5)

/** Seconds per revolution of a constructed run */
#define BOX_PERF_PERIOD (10.0f)

/** Degrees above the horizon of a constructed run */
#define BOX_PERF_ELEVATION (30.0f)

/** Fewest warmup frames, the bounds are measured once the first frame placed the shapes */
#define BOX_PERF_MIN_WARMUP (2)

/** Progress of a performance run */
ECS_ENUM(PerfState, {
    PerfIdle = 0,    /**< No run was started */
    PerfWarmup = 1,  /**< Frames are capped and the camera orbits, nothing is measured */
    PerfMeasure = 2, /**< Every frame interval is measured */
    PerfDone = 3,    /**< The run completed, PerfStatus holds the result */
    PerfInvalid = 4  /**< The settings are invalid, the run did not start */
});

/** Seconds between the boost toggles of a fly run */
#define BOX_PERF_BOOST_PERIOD (2.0)

/** Settings of a performance run, setting the singleton (re)starts the run.
 *
 * The run sets App.fps to its target, so flecs caps the frames, and measures the intervals
 * between the starts of the frames.
 */
ECS_STRUCT(PerfRun, {
    float fps;            /**< Target frame rate: 30, 60, 90 or 120, default 60 */
    int32_t frames;       /**< Frames measured, default 300 */
    int32_t warmup;       /**< Frames before the measurement, default 60, at least 2 */
    ecs_entity_t camera;  /**< Camera that orbits, 0 for the first entity with Camera */
    float period;         /**< Seconds per revolution of the camera, default 10, 0 stands still */
    float elevation;      /**< Degrees the camera looks down on the center, default 30 */
    float stall;          /**< Milliseconds of busy work added to every frame, to test checks */
    bool fly;             /**< The camera controller flies the camera, forward and turning left */
    bool quit;            /**< Quit the world when the run completes */
    char *report;         /**< JSON file the result is written to, NULL for none */
});

/** Range of a count sampled over the measurement of a run */
ECS_STRUCT(PerfRange, {
    int32_t min;  /**< Smallest sample */
    int32_t max;  /**< Largest sample */
    double mean;  /**< Mean of the samples */
});

/** Checks of a run beyond the frame intervals: delta time, presentation, CPU use and flight */
ECS_STRUCT(PerfChecks, {
    int32_t delta_errors; /**< Frames whose delta_time differs over tolerance from wall time */
    double delta_tolerance_ms; /**< Tolerance of the delta, BOX_FRAME_TOLERANCE_MS */
    double delta_max_ms;  /**< Largest difference of delta_time and the wall time */
    double present_mean_ms; /**< Mean interval between presents during the measurement */
    double cpu_percent;   /**< Process CPU time over wall time during the measurement */
    double cpu_ms;        /**< Process CPU milliseconds per measured frame */
    bool fly;             /**< The controller flew the camera */
    float travelled;      /**< Distance the camera moved during the measurement */
});

/** Texture graph cache counts per frame of the measurement of a run, from the backend
 * statistics (zero without the GPU backend) */
ECS_STRUCT(PerfTextureGraphs, {
    int32_t samples;     /**< Frames sampled */
    PerfRange hits;      /**< Stages whose value the cache had, per frame */
    PerfRange misses;    /**< Stages computed into a new cached value, per frame */
    PerfRange evictions; /**< Cached values evicted to keep the budget, per frame */
    PerfRange updates;   /**< Updates of animated graphs, per frame */
    PerfRange cached_kb; /**< Kilobytes of the cached values at each frame */
});

/** Result of a performance run, written by the engine and read only for applications */
ECS_STRUCT(PerfStatus, {
    PerfState state;      /**< Progress of the run */
    bool passed;          /**< The run completed every frame without an error */
    int32_t frame;        /**< Frames measured so far */
    int32_t errors;       /**< Sum of slow, fast and delta errors */
    int32_t slow;         /**< Frames longer than 1.1 target intervals */
    int32_t fast;         /**< Frames shorter than 0.9 target intervals */
    PerfChecks checks;    /**< Delta, presentation, CPU and flight checks of the measurement */
    double target_ms;     /**< Target frame interval in milliseconds */
    int32_t warmup;       /**< Capped frames before the measurement, the settle period */
    float period;         /**< Seconds per revolution of the orbit */
    double min_ms;        /**< Shortest frame interval */
    double mean_ms;       /**< Mean frame interval */
    double p50_ms;        /**< Median frame interval */
    double p99_ms;        /**< 99th percentile frame interval */
    double max_ms;        /**< Longest frame interval */
    double slept_ms;      /**< Milliseconds the CPU budget of App slept during the measurement */
    int32_t stalls;       /**< Frames that waited for a free render packet during the measurement */
    int32_t shapes;       /**< Shapes the bounds were measured from */
    vec3 center;          /**< Center of the bounds the camera orbits */
    float radius;         /**< Radius of the sphere around the bounds */
    float distance;       /**< Distance of the camera from the center */
    float revolutions;    /**< Turns the orbit made during the measured frames, 0 standing still */
    int32_t views;        /**< Views that render the orbiting camera or a camera under it */
    int32_t cull_samples; /**< Culling results of those views read during the measurement */
    int32_t cull_changes; /**< Samples whose counts differ from the sample before */
    PerfRange visible;    /**< Instances those views drew, summed per sample */
    PerfRange frustum_culled;   /**< Instances outside the frustum or too small, per sample */
    PerfRange occlusion_culled; /**< Instances hidden behind closer geometry, per sample */
    PerfRange range_culled;     /**< Instances outside their DrawRange, per sample */
    PerfRange batches_skipped;  /**< Batches the views skipped by their bounds, per sample */
    PerfRange casters_skipped;  /**< Batches their shadow cascades skipped by their bounds, per
                                 *   sample */
    float audio_load;     /**< Largest AudioStatus.load of the measured frames, the share of
                           *   real time the audio.render took */
    PerfTextureGraphs texture_graphs; /**< Texture graph cache counts per measured frame */
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
