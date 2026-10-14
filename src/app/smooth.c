/**
 * @file app/smooth.c
 * @brief Delta smoothing of a frame loop paced by vsync, a port of Godot's DeltaSmoother.
 *
 * Mirrors MainTimerSync::DeltaSmoother of Godot 4 (main/main_timer_sync.cpp): the refresh rate is
 * estimated from averaged frame times and, once stable, deltas are whole refresh periods.
 */

#include "app.h"
#include <math.h>

/** Frame times averaged into one refresh rate reading */
#define BOX_SMOOTH_READINGS (60)

/** Frames over which the measured rate is compared with the estimate */
#define BOX_SMOOTH_MEASURE_FRAMES (64)

/** Readings one Hz above the estimate needed before it is raised */
#define BOX_SMOOTH_SIGNIFICANCE_UP (1)

/** Readings one Hz below the estimate needed before it is lowered */
#define BOX_SMOOTH_SIGNIFICANCE_DOWN (2)

/** Lowest averaged rate the first estimate is taken from */
#define BOX_SMOOTH_MIN_FPS (50)

/** Readings at the estimate that complete it */
#define BOX_SMOOTH_COMPLETE_HITS (2)

/** Readings at a complete estimate that lock it */
#define BOX_SMOOTH_LOCK_HITS (20)

/** Readings at the estimate after which a large drop is taken as dropped frames */
#define BOX_SMOOTH_ESTABLISHED_HITS (10)

/** Deltas longer than this many microseconds pass unchanged, such as resuming from sleep */
#define BOX_SMOOTH_MAX_DELTA (1000000)

/** Microseconds of one refresh before the first estimate, 60 Hz */
#define BOX_SMOOTH_INITIAL_DELTA (16666)

/** Deltas shorter than this many microseconds pass unchanged, vsync is off or a hiccup */
#define BOX_SMOOTH_MIN_DELTA (1000)

/** Start a new estimate: forget the readings that confirmed or contradicted the old one.
 *
 * @param s The smoother
 */
static void box_smooth_new_estimate(
    box_app_smoother_t *s)
{
    s->hits_above = 0;
    s->hits_at = 0;
    s->hits_below = 0;
    s->hits_one_above = 0;
    s->hits_one_below = 0;
    s->complete = false;
}

/** Take one more reading of the frame time below the estimate.
 *
 * @param s The smoother
 * @param fps The averaged rate of the reading
 */
static void box_smooth_below(
    box_app_smoother_t *s,
    int32_t fps)
{
    bool established;
    if (fps == s->estimated_fps - 1) {
        s->hits_one_below ++;
        if (s->hits_one_below > s->hits_at &&
            s->hits_one_below > BOX_SMOOTH_SIGNIFICANCE_DOWN)
        {
            s->estimated_fps --;
            box_smooth_new_estimate(s);
        }

        goto done;
    }

    s->hits_below ++;
    established = s->complete && s->hits_at > BOX_SMOOTH_ESTABLISHED_HITS;
    if (!established && (s->hits_below / 8) > s->hits_at &&
        s->hits_below > BOX_SMOOTH_SIGNIFICANCE_DOWN)
    {
        s->estimated_fps --;
        box_smooth_new_estimate(s);
    }

done:
    return;
}

/** Take one more reading of the frame time above the estimate.
 *
 * @param s The smoother
 * @param fps The averaged rate of the reading
 */
static void box_smooth_above(
    box_app_smoother_t *s,
    int32_t fps)
{
    int32_t change;
    if (fps == s->estimated_fps + 1) {
        s->hits_one_above ++;
        if (s->hits_one_above > s->hits_at && s->hits_one_above > BOX_SMOOTH_SIGNIFICANCE_UP) {
            s->estimated_fps ++;
            box_smooth_new_estimate(s);
        }

        goto done;
    }

    s->hits_above ++;
    if (s->hits_above > s->hits_at && s->hits_above > BOX_SMOOTH_SIGNIFICANCE_UP) {
        change = (fps - s->estimated_fps) / 2;
        s->estimated_fps += change > 1 ? change : 1;
        box_smooth_new_estimate(s);
    }

done:
    return;
}

/** Update the refresh rate estimate with a frame time, as update_refresh_rate_estimator does.
 *
 * @param s The smoother
 * @param delta Microseconds of the frame
 */
static void box_smooth_estimate(
    box_app_smoother_t *s,
    int64_t delta)
{
    int32_t fps;
    if (s->locked) {
        goto done;
    }

    s->estimator_total += delta;
    s->estimator_readings ++;
    if (s->estimator_readings < BOX_SMOOTH_READINGS) {
        goto done;
    }

    delta = s->estimator_total / BOX_SMOOTH_READINGS;
    s->estimator_readings = 0;
    s->estimator_total = 0;
    fps = (int32_t)lround(1000000.0 / (double)delta);
    if (!s->estimated_fps) {
        if (fps < BOX_SMOOTH_MIN_FPS) {
            goto done;
        }

        s->estimated_fps = fps;
    }

    if (fps == s->estimated_fps) {
        s->hits_at ++;
        if (s->complete && s->hits_at == BOX_SMOOTH_LOCK_HITS) {
            s->locked = true;
        } else if (!s->complete && s->hits_at > BOX_SMOOTH_COMPLETE_HITS) {
            s->complete = true;
            s->vsync_delta = 1000000 / s->estimated_fps;
        }
    } else if (fps < s->estimated_fps) {
        box_smooth_below(s, fps);
    } else {
        box_smooth_above(s, fps);
    }

done:
    return;
}

/** Check that the measured rate stays within 5% of the estimate, as fps_allows_smoothing does.
 *
 * @param s The smoother
 * @param delta Microseconds of the frame
 * @return Whether the deltas may be smoothed
 */
static bool box_smooth_allowed(
    box_app_smoother_t *s,
    int64_t delta)
{
    int64_t passed;
    double ratio;
    s->measurement_time += delta;
    s->measurement_frames ++;
    if (s->measurement_frames != s->measurement_end) {
        goto done;
    }

    if (s->complete) {
        passed = (s->measurement_time - s->measurement_start) / BOX_SMOOTH_MEASURE_FRAMES;
        if (passed) {
            ratio = (1000000.0 / (double)passed) / (double)s->estimated_fps;
            s->allowed = ratio > 0.95 && ratio < 1.05;
        }
    }

    s->measurement_start = s->measurement_time;
    s->measurement_end += BOX_SMOOTH_MEASURE_FRAMES;
done:
    return s->allowed;
}

void box_app_smoother_reset(
    box_app_smoother_t *s)
{
    ecs_os_memset_t(s, 0, box_app_smoother_t);
    s->vsync_delta = BOX_SMOOTH_INITIAL_DELTA;
    s->measurement_end = BOX_SMOOTH_MEASURE_FRAMES;
    s->allowed = true;
}

int64_t box_app_smooth_delta(
    box_app_smoother_t *s,
    int64_t delta)
{
    int64_t result = delta;
    int64_t units;
    s->smoothed = false;
    if (delta > BOX_SMOOTH_MAX_DELTA || !box_smooth_allowed(s, delta) ||
        delta < BOX_SMOOTH_MIN_DELTA)
    {
        goto done;
    }

    box_smooth_estimate(s, delta);
    if (!s->complete) {
        goto done;
    }

    s->leftover += delta;
    units = s->leftover / s->vsync_delta;
    units = units > 1 ? units : 1;
    s->leftover -= units * s->vsync_delta;
    s->smoothed = true;
    result = units * s->vsync_delta;
done:
    return result;
}
