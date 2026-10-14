/**
 * @file app/app.h
 * @brief Process measurements and the temporary folder shared by the box.app sources.
 */

#ifndef BOX_APP_INTERNAL_H
#define BOX_APP_INTERNAL_H

#include <box.h>

/** Refresh rate estimate and leftover time of the delta smoothing, see smooth.c */
typedef struct box_app_smoother_t {
    int64_t vsync_delta;        /**< Microseconds of one refresh of a complete estimate */
    int64_t leftover;           /**< Measured microseconds not yet handed out as deltas */
    int32_t estimated_fps;      /**< Estimated refresh rate, 0 before the first reading */
    int32_t hits_at;            /**< Readings at the estimate */
    int32_t hits_above;         /**< Readings more than one Hz above the estimate */
    int32_t hits_below;         /**< Readings more than one Hz below the estimate */
    int32_t hits_one_above;     /**< Readings one Hz above the estimate */
    int32_t hits_one_below;     /**< Readings one Hz below the estimate */
    bool complete;              /**< The estimate is trusted and deltas are smoothed */
    bool locked;                /**< The estimate no longer changes */
    int64_t estimator_total;    /**< Microseconds of the readings being averaged */
    int32_t estimator_readings; /**< Frames in estimator_total */
    int64_t measurement_time;   /**< Microseconds of every frame measured */
    int64_t measurement_start;  /**< measurement_time at the start of the current window */
    int64_t measurement_frames; /**< Frames measured */
    int64_t measurement_end;    /**< Frame count at which the current window ends */
    bool allowed;               /**< The measured rate is within 5% of the estimate */
    bool smoothed;              /**< The last delta was snapped to whole refresh periods */
} box_app_smoother_t;

/** Reset a delta smoother to its state before the first frame.
 *
 * @param s The smoother
 */
void box_app_smoother_reset(
    box_app_smoother_t *s);

/** Smooth the measured delta of a frame that presents with vsync, as Godot's
 * MainTimerSync::DeltaSmoother::smooth_delta does: once the estimated refresh rate is stable,
 * the delta is the whole refresh periods that fit in the time measured so far.
 *
 * @param s The smoother
 * @param delta Measured microseconds since the previous frame
 * @return Microseconds the frame advances
 */
int64_t box_app_smooth_delta(
    box_app_smoother_t *s,
    int64_t delta);

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

/** Decide whether a machine is oversubscribed.
 *
 * @param load Load average over the last minute
 * @param cores Number of online cores
 * @return Whether the load exceeds the core count
 */
bool box_app_oversubscribed(
    double load,
    int32_t cores);

/** Refuse a headless start when the machine is oversubscribed.
 *
 * @return 0 when the process may start, nonzero when the machine is oversubscribed
 */
int box_app_guard(void);

/** Compute the sleep that brings a process back within its CPU budget.
 *
 * @param cpu_used CPU seconds the process used during the interval
 * @param wall_used Wall seconds the interval took
 * @param budget Percentage of one core the process may use
 * @return Seconds to sleep, 0 when the process is within its budget
 */
double box_app_pace(
    double cpu_used,
    double wall_used,
    double budget);

/** Create the temporary folder of the process and remove it when the world is deleted.
 *
 * @param world The world
 * @return The folder path, which the caller frees, or NULL when it could not be created
 */
char* box_app_tmp_create(
    ecs_world_t *world);

#endif
