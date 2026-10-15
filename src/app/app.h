/**
 * @file app/app.h
 * @brief Process measurements and the temporary folder shared by the box.app sources.
 */

#ifndef BOX_APP_INTERNAL_H
#define BOX_APP_INTERNAL_H

#include <box.h>
#include <sys/types.h>

/** Refresh rate estimate and leftover time of the delta smoothing, see smooth.c */
typedef struct box_app_smoother_t {
    double period;              /**< Nanoseconds of one refresh of a complete estimate */
    double leftover;            /**< Measured nanoseconds not yet handed out as deltas */
    int32_t estimated_fps;      /**< Estimated refresh rate, 0 before the first reading */
    bool primed;                /**< The first reading, of frames that filled the pipeline,
                                 *   was dropped */
    int32_t hits_at;            /**< Readings at the estimate */
    int32_t hits_above;         /**< Readings more than one Hz above the estimate */
    int32_t hits_below;         /**< Readings more than one Hz below the estimate */
    int32_t hits_one_above;     /**< Readings one Hz above the estimate */
    int32_t hits_one_below;     /**< Readings one Hz below the estimate */
    bool complete;              /**< The estimate is trusted and deltas are smoothed */
    bool locked;                /**< The estimate no longer changes */
    int64_t period_total;       /**< Nanoseconds of the readings at the estimate */
    int64_t period_refreshes;   /**< Refreshes of the readings at the estimate */
    int64_t estimator_total;    /**< Nanoseconds of the frames being averaged */
    int32_t estimator_refreshes; /**< Refreshes the frames in estimator_total span */
    int32_t estimator_readings; /**< Frames in estimator_total */
    int64_t measurement_time;   /**< Nanoseconds of every frame measured */
    int64_t measurement_start;  /**< measurement_time at the start of the current window */
    int32_t measurement_refreshes; /**< Refreshes of the frames of the current window */
    int64_t measurement_frames; /**< Frames measured */
    int64_t measurement_end;    /**< Frame count at which the current window ends */
    bool allowed;               /**< The measured rate is within 5% of the estimate */
    bool measured;              /**< The first measurement window passed */
    bool smoothed;              /**< The last delta was snapped to whole refresh periods */
} box_app_smoother_t;

/** Reset a delta smoother to its state before the first frame.
 *
 * @param s The smoother
 */
void box_app_smoother_reset(
    box_app_smoother_t *s);

/** Smooth the measured delta of a frame that presents with vsync, after Godot's
 * MainTimerSync::DeltaSmoother::smooth_delta: once the refresh rate is stable, the delta is the
 * refreshes the previous frame is shown for in exact periods, plus repeated refreshes.
 *
 * A small share of the time left over is handed back each frame, so the deltas never drift.
 *
 * @param s The smoother
 * @param delta Measured nanoseconds since the previous frame
 * @param presents Refreshes the previous frame is shown for, 0 when presentation does not pace
 * @param cycle Frames after which the present counts repeat: 1 when every frame is shown for
 *        the same refreshes, 0 when they do not repeat within a few seconds
 * @param refresh Refresh rate the display reports, which times the refreshes until the
 *        estimate completes, 0 when unknown
 * @return Nanoseconds the frame advances
 */
int64_t box_app_smooth_delta(
    box_app_smoother_t *s,
    int64_t delta,
    int32_t presents,
    int32_t cycle,
    float refresh);

/** Fraction of the shortest frame interval of presentation pacing that the frame rate cap
 * still enforces: a guard that never binds while the presents pace the frames, and stops a
 * runaway frame rate when they return at once, like a minimized or occluded window. */
#define BOX_APP_PACED_GUARD (0.75)

/** Compute the interval the frame rate cap sleeps to, as an absolute deadline per frame.
 *
 * @param fps Frame rate cap, 0 for none
 * @param refresh Refresh rate of the display in Hz, 0 when unknown
 * @param presents Refreshes the last frame is shown for, 0 when presentation does not pace
 * @return Seconds per frame, 1 / fps when the cap paces, BOX_APP_PACED_GUARD times the
 *         shortest interval presentation paces otherwise, 0 without a cap
 */
double box_app_cap_interval(
    float fps,
    float refresh,
    int32_t presents);

/** Read the CPU time the process has used on all of its threads.
 *
 * @return User plus system seconds, 0 in the browser, which has no process CPU time
 */
double box_app_cpu_time(void);

/** Read the wall clock.
 *
 * @return Seconds since an arbitrary epoch
 */
double box_app_wall_time(void);

/** Decide whether a machine is oversubscribed for a process with a CPU budget.
 *
 * @param load Load average over the last minute
 * @param cores Number of online cores
 * @param budget Percentage of one core the process may use, 0 for no limit, which claims one
 *        core: the main thread when it runs flat out
 * @return Whether the load plus the cores the budget claims exceeds the core count
 */
bool box_app_oversubscribed(
    double load,
    int32_t cores,
    float budget);

/** Refuse a headless start when the machine is oversubscribed for the CPU budget.
 *
 * @param budget Percentage of one core the process may use, 0 for no limit
 * @return 0 when the process may start, nonzero when the machine is oversubscribed
 */
int box_app_guard(
    float budget);

/** Compute the sleep that brings a process back within its CPU budget.
 *
 * @param cpu_used CPU seconds the process used during the interval
 * @param wall_used Wall seconds since the interval started, which is the deadline of the
 *        previous sleep, so the time a late wake up overslept counts toward this one
 * @param budget Percentage of one core the process may use
 * @return Seconds to sleep, 0 when the process is within its budget
 */
double box_app_pace(
    double cpu_used,
    double wall_used,
    double budget);

/** Decide whether the root of the temporary folders keeps the folder of a process safe from
 * other users: it must be a real folder, owned by the user or by root, and sticky when others
 * can write it.
 *
 * @param mode The mode of the root, as lstat reports it
 * @param owner The user that owns the root
 * @param user The user of the process
 * @return NULL when the root is safe, otherwise why it is not
 */
const char* box_app_tmp_root_problem(
    mode_t mode,
    uid_t owner,
    uid_t user);

/** Create the private temporary folder of the process, or share the one it created, and
 * remove it when the last world that uses it is deleted.
 *
 * A folder of the current user that an earlier process with the same pid left at the path is
 * removed first; anything else at the path is refused and left alone.
 *
 * @param world The world
 * @return The folder path, which the caller frees, or NULL when it could not be created
 */
char* box_app_tmp_open(
    ecs_world_t *world);

/** Open the temporary folder of a headless process and stop the world when that fails.
 *
 * @param world The world
 * @return The folder path, which the caller frees, or NULL when it could not be created
 */
char* box_app_tmp_create(
    ecs_world_t *world);

/** Remove a folder of the current user and everything in it without following symbolic
 * links.
 *
 * @param path The folder
 */
void box_app_rmtree(
    const char *path);

#endif
