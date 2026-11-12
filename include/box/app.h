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
 * frame. In the browser the loop skips animation frames to match fps, without a flecs sleep.
 */
ECS_STRUCT(App, {
    bool headless;      /**< Run without a window */
    int32_t frames;     /**< Stop after this many frames, 0 for no limit */
    double timeout;     /**< Stop after this many seconds, 0 for none, negative for infinite */
    float cpu_budget;   /**< Percent of one core a headless run may use, default 100, 0 no limit */
    float fps;          /**< Target fps flecs caps the frames at, default 0: FpsTarget */
    float delta_time;   /**< Seconds every frame advances, default 0: flecs measures the delta */
    int32_t port;       /**< Port of the command endpoints, 0 disables them */
    int32_t surface_width;  /**< Width of the window Surface, set or created by the run, 0 none */
    int32_t surface_height; /**< Height of the window Surface, set or created by the run */
    bool unguarded;     /**< A headless run starts on an oversubscribed machine, by --unguarded */
    bool unpaced;       /**< A headless run ignores its CPU budget */
    char *scene;        /**< Script file loaded before the first frame, NULL for none */
    bool design;        /**< Render the gallery of design cameras, set by --design */
    char *camera;       /**< Name of the Camera the surface shows, set by --camera, NULL for none */
    char *out;          /**< Image file the surface is written to, set by --out, NULL for none */
    bool no_audio;      /**< Open no audio output, set by --no-audio */
});

/** Frame rate cap of a scene, a singleton a scene script sets, such as
 * `$ { box.app.FpsTarget: {30} }`.
 *
 * It paces the frames like App.fps, which takes precedence when it is above 0, so --fps and the
 * fps command override the scene. Removing it, or a value of 0, leaves the frames uncapped.
 */
ECS_STRUCT(FpsTarget, {
    float value;        /**< Frames per second, 0 for no cap */
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
    double target_ms;       /**< Interval of the box_app_fps cap, 0 when uncapped */
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

/** Wall time between the starts of the recent frames, one sample per frame: the intervals of
 * FrameTiming, written by the engine at the start of every frame while hold is false.
 *
 * The engine notifies each sample it adds with ecs_singleton_modified, like FrameTiming, so OnSet
 * observers and change detection see it; while hold is set it adds none and does not notify.
 *
 * Read the samples with box_frame_sample. Tests and captures that show fixed samples write
 * interval, count and next and set hold. The size of interval is BOX_FRAME_WINDOW.
 */
ECS_STRUCT(FrameSamples, {
    double interval[240];   /**< Intervals in milliseconds, a ring of BOX_FRAME_WINDOW */
    int32_t count;          /**< Samples stored, at most BOX_FRAME_WINDOW */
    int32_t next;           /**< Slot the next sample goes to */
    bool hold;              /**< The engine adds no samples, so fixed samples stay */
});

/** Seconds between two passes over the watched files when FileWatch is not set */
#define BOX_FILE_WATCH_INTERVAL (0.25f)

/** How the engine watches the files it loaded, set to BOX_FILE_WATCH_INTERVAL by box.app.
 *
 * Watched are the files a Shader read or looked for and the file of a TexturePath, Font and
 * EnvironmentImage. A changed, removed or returned file loads again; the old content stays until
 * the new load is ready. A new path or removing the component stops watching the old file.
 *
 * Every interval (unscaled) a watch thread stats each distinct file once; a poll in EcsOnStore
 * applies the pass (a headless one waits) and visits only the owners of changed files. At interval
 * 0, without threads (the browser) or when the thread cannot start (one warning) it runs inline.
 */
ECS_STRUCT(FileWatch, {
    float interval;     /**< Seconds between passes, 0 a pass every frame, < 0 off */
});

/** What the file watch did, written by the engine when a pass was applied or the number of
 * watched files changed.
 */
ECS_STRUCT(FileWatchStatus, {
    int32_t files;      /**< Distinct files watched */
    int64_t polls;      /**< Passes over all files applied since the world started */
    int64_t changes;    /**< Changed files found since the world started */
    char *last;         /**< The file that changed last, NULL before the first change */
});

/** Sets the other components of its entity on entity at the start of the frame after
 * AppStatus.frame reached frame, then deletes its entity. SetAt adds Disabled to its entity.
 *
 * Copied: every component written to it (ecs_set, a script assignment), whatever its value. Not
 * copied: pairs, tags, SetAt and components only added, such as a Position added with With.
 * Writes are recorded once the entity has SetAt, so write SetAt first in a script body.
 *
 * A component added before SetAt, or merged by a stage without OnSet, is copied only when it
 * differs from its constructed value; SetAt warns when such a component holds that value, except
 * for components the engine computes, such as MaterialId.
 */
ECS_STRUCT(SetAt, {
    int32_t frame;          /**< Frames completed (AppStatus.frame) before the values are set */
    ecs_entity_t entity;    /**< Entity that receives the values */
});

/** Register the box.app module.
 *
 * @param world The world
 */
void BoxAppImport(
    ecs_world_t *world);

/** Write command line arguments into the App singleton: --headless, --unguarded, --frames <n>,
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

/** The frame rate cap the engine paces the frames at: App.fps when above 0, else FpsTarget.
 *
 * @param world The world
 * @return Frames per second, 0 for no cap
 */
float box_app_fps(
    const ecs_world_t *world);

/** Find a file: the path as given, or a relative path in the working directory or one of its
 * ancestors, so a run from inside the repository finds `etc/...`.
 *
 * @param path The path of the file
 * @return The path to load, the path as given when it is not found, owned by the caller
 */
char* box_app_locate(
    const char *path);

/** Add a sample to FrameSamples, over the oldest one when the ring is full.
 *
 * @param samples The samples
 * @param interval_ms The interval of the frame in milliseconds
 */
void box_frame_samples_add(
    FrameSamples *samples,
    double interval_ms);

/** Get a sample of FrameSamples by its age.
 *
 * @param samples The samples
 * @param age 0 for the newest sample, 1 for the one before it
 * @return The interval in milliseconds, 0 when there is no sample of that age
 */
double box_frame_sample(
    const FrameSamples *samples,
    int32_t age);

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
