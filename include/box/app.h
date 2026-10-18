/**
 * @file box/app.h
 * @brief Application settings, headless operation and the frame loop.
 *
 * The box.app module owns the App singleton that the command line fills and the
 * AppStatus singleton the engine writes; box_app_run runs the world until App stops it.
 */

#ifndef BOX_APP_H
#define BOX_APP_H

#include <flecs.h>

#ifdef BOX_APP_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Environment variable that, set to anything but 0, makes every new App unguarded and unpaced */
#define BOX_TEST_ENV "BOX_TEST"

/** Folder the temporary folders of headless processes are created in */
#define BOX_TMP_ROOT "/tmp/box"

/** Port of the command endpoints when --port is not given */
#define BOX_APP_PORT (27750)

/** Frames the ranges of FrameTiming cover, the most recent ones */
#define BOX_FRAME_WINDOW (240)

/** Milliseconds the delta_time of a frame may differ from the wall time since the previous one.
 *
 * flecs measures the delta at the start of the frame, so with App.delta_time 0 it only differs
 * from the interval the frame timing measures by the time between the two clock reads.
 */
#define BOX_FRAME_TOLERANCE_MS (1.0)

/** Intervals beyond this many target intervals count as a dropped frame */
#define BOX_FRAME_DROPPED (1.5)

/** Settings of the process, filled from the command line before the world runs.
 *
 * A headless run needs a frame limit or a timeout, is refused on an oversubscribed machine
 * unless unguarded, and sleeps when it uses more than its CPU budget unless unpaced.
 *
 * flecs paces the frames: fps is its target fps (ecs_set_target_fps), applied at the start and
 * whenever App is set, and every frame is ecs_progress(world, delta_time), read from App each
 * frame. In the browser the animation frames pace the loop and fps has no effect.
 */
ECS_STRUCT(App, {
    bool headless;      /**< Run without a window */
    int32_t frames;     /**< Stop after this many frames, 0 for no limit */
    double timeout;     /**< Stop after this many seconds, 0 for none, negative for infinite */
    float cpu_budget;   /**< Percent of one core a headless run may use, default 100, 0 no limit */
    float fps;          /**< Target fps flecs caps the frames at, default 0: no cap */
    float delta_time;   /**< Seconds every frame advances, default 0: flecs measures the delta */
    int32_t port;       /**< Port of the command endpoints, 0 disables them */
    int32_t surface_width;  /**< Width of the Surface created when there is no window, 0 none */
    int32_t surface_height; /**< Height of the Surface a run creates when it has none */
    bool unguarded;     /**< A headless run starts on an oversubscribed machine */
    bool unpaced;       /**< A headless run ignores its CPU budget */
    char *scene;        /**< Script file loaded before the first frame, NULL for none */
});

/** State of the process the engine writes, read only for applications */
ECS_STRUCT(AppStatus, {
    int32_t frame;      /**< Frames completed since the world was created */
    double elapsed;     /**< Wall seconds since the world was created */
    double slept;       /**< Wall seconds the CPU budget has slept in total */
    char *tmp_dir;      /**< Folder for temporary files of a headless process, else NULL */
    bool running;       /**< box_app_run is stepping the world */
});

/** Range of a frame time over the recent frames, in milliseconds */
ECS_STRUCT(FrameRange, {
    double min;   /**< Smallest value */
    double max;   /**< Largest value */
    double mean;  /**< Mean value */
});

/** Timing of the recent frames, written by the engine at the start of every frame.
 *
 * delta is the unscaled delta_time the world advanced with, interval the wall time between the
 * starts of two frames and error their difference.
 */
ECS_STRUCT(FrameTiming, {
    int32_t frames;         /**< Frames the ranges cover, at most BOX_FRAME_WINDOW */
    double target_ms;       /**< Interval of the App.fps cap, 0 when uncapped */
    bool vsync;             /**< Frames present with vsync */
    double delta_ms;        /**< Delta of the last frame */
    double interval_ms;     /**< Interval before the last frame */
    FrameRange delta;       /**< Deltas of the recent frames */
    FrameRange interval;    /**< Intervals of the recent frames */
    FrameRange error;       /**< Absolute difference of delta and interval of the recent frames */
    FrameRange present;     /**< Intervals between the presents of the recent frames */
    int32_t late;           /**< Recent frames whose error exceeds BOX_FRAME_TOLERANCE_MS */
    int32_t dropped;        /**< Recent frames longer than BOX_FRAME_DROPPED times target_ms */
    int64_t total;          /**< Frames since the world started */
    int64_t total_late;     /**< Frames since the world started with an error over tolerance */
    int64_t total_dropped;  /**< Dropped frames since the world started */
});

/** Register the box.app module.
 *
 * @param world The world
 */
void BoxAppImport(
    ecs_world_t *world);

/** Write command line arguments into the App singleton: --headless, --frames <n>,
 * --timeout <s|infinite>, --cpu-budget <pct>, --fps <n>, --delta-time <s>, --port <n>,
 * --surface <w>x<h>, a scene.
 *
 * --fps <n> sets the target fps of flecs in every mode, 0 or none for no cap. --delta-time <s>
 * makes every frame advance s seconds, 0 or none to measure the delta, headless too.
 *
 * @param world The world
 * @param argc Argument count, including the executable
 * @param argv Argument array, starting with the executable
 * @return 0 on success, nonzero when an argument is invalid
 */
int box_app_parse(
    ecs_world_t *world,
    int argc,
    char *argv[]);

/** Block the calling thread with an OS sleep until a time on the ecs_os_get_time clock.
 *
 * Sleeps 60% of the time left until less than 0.1 ms is left, then the rest, so timer leeway
 * cannot overshoot by more than a few dozen microseconds. It never busy waits.
 *
 * @param deadline Seconds on the clock of ecs_os_get_time
 * @return The clock after the wait
 */
double box_wait_until(
    double deadline);

/** Run the world with the settings of the App singleton until a stop condition is met: every
 * frame is ecs_progress(world, App.delta_time), and nothing else runs in the loop.
 *
 * @param world The world
 * @return 0 when the run completed, nonzero when it was refused or failed to start
 */
int box_app_run(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
