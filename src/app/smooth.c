/**
 * @file app/smooth.c
 * @brief Delta smoothing of a frame loop paced by vsync, after Godot's DeltaSmoother.
 *
 * The refresh rate is estimated like MainTimerSync::DeltaSmoother of Godot 4 does, from frame
 * times averaged per refresh; once stable, a delta is the refreshes the previous frame was shown
 * for, measured as an exact period, and the time left over is handed back a little per frame.
 */

#include "app.h"
#include <math.h>

/** Frames averaged into one refresh rate reading at least */
#define BOX_SMOOTH_READINGS (60)

/** Frames of a reading whose present counts do not repeat within it */
#define BOX_SMOOTH_READINGS_MAX (240)

/** Frames over which the measured rate is compared with the estimate */
#define BOX_SMOOTH_MEASURE_FRAMES (64)

/** Readings one Hz above the estimate needed before it is raised */
#define BOX_SMOOTH_SIGNIFICANCE_UP (1)

/** Readings one Hz below the estimate needed before it is lowered */
#define BOX_SMOOTH_SIGNIFICANCE_DOWN (2)

/** Lowest averaged refresh rate the first estimate is taken from */
#define BOX_SMOOTH_MIN_FPS (50)

/** Readings at the estimate that complete it */
#define BOX_SMOOTH_COMPLETE_HITS (2)

/** Readings at a complete estimate that lock it */
#define BOX_SMOOTH_LOCK_HITS (20)

/** Readings at the estimate after which a large drop is taken as dropped frames */
#define BOX_SMOOTH_ESTABLISHED_HITS (10)

/** Deltas longer than this many nanoseconds pass unchanged, such as resuming from sleep */
#define BOX_SMOOTH_MAX_DELTA (1000000000)

/** Deltas shorter than this many nanoseconds pass unchanged, vsync is off or a hiccup */
#define BOX_SMOOTH_MIN_DELTA (1000000)

/** Frames over which the leftover time is handed back, so the deltas follow the wall clock */
#define BOX_SMOOTH_BLEED (32)

/** Refresh periods a frame may hand back at most: far below the tolerance of a delta, so the
 * time a hitch or a refilling pipeline leaves over returns over many frames, unnoticed */
#define BOX_SMOOTH_BLEED_MAX (0.02)

/** Leftover refreshes beyond the shown ones that count as a refresh the display repeated */
#define BOX_SMOOTH_DROP (1.0)

/** The same for a cadence: the main thread measures its frames shifted against the presents, so
 * the leftover swings by up to one refresh without a repeat */
#define BOX_SMOOTH_DROP_CADENCE (1.5)

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
    s->period_total = 0;
    s->period_refreshes = 0;
}

/** Take one more reading of the refresh period below the estimated rate.
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

/** Take one more reading of the refresh period above the estimated rate.
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

/** Count the frames of a reading: at least BOX_SMOOTH_READINGS and a whole number of cadence
 * cycles, so the shift between measured frames and presents cancels out.
 *
 * @param cycle Frames after which the present counts repeat, 0 when they do not repeat soon
 * @return Frames per reading
 */
static int32_t box_smooth_reading_frames(
    int32_t cycle)
{
    int32_t result = BOX_SMOOTH_READINGS_MAX;
    if (cycle > 0) {
        result = ((BOX_SMOOTH_READINGS + cycle - 1) / cycle) * cycle;
        result = result > BOX_SMOOTH_READINGS_MAX ? BOX_SMOOTH_READINGS_MAX : result;
    }

    return result;
}

/** Update the refresh rate estimate with a frame time, as update_refresh_rate_estimator does,
 * and keep the exact period: the mean of every reading that confirmed the estimate.
 *
 * @param s The smoother
 * @param delta Nanoseconds of the frame
 * @param presents Refreshes the frame measured spans
 * @param cycle Frames after which the present counts repeat, 0 when they do not repeat soon
 */
static void box_smooth_estimate(
    box_app_smoother_t *s,
    int64_t delta,
    int32_t presents,
    int32_t cycle)
{
    int64_t total;
    int32_t refreshes, fps;
    double period;
    if (s->locked) {
        goto done;
    }

    s->estimator_total += delta;
    s->estimator_refreshes += presents;
    s->estimator_readings ++;
    if (s->estimator_readings < box_smooth_reading_frames(cycle)) {
        goto done;
    }

    total = s->estimator_total;
    refreshes = s->estimator_refreshes;
    period = (double)total / (double)refreshes;
    s->estimator_readings = 0;
    s->estimator_refreshes = 0;
    s->estimator_total = 0;
    fps = (int32_t)lround(1e9 / period);
    if (!s->primed) {
        s->primed = true;
        goto done;
    }

    if (!s->estimated_fps) {
        if (fps < BOX_SMOOTH_MIN_FPS) {
            goto done;
        }

        s->estimated_fps = fps;
    }

    if (fps == s->estimated_fps) {
        s->hits_at ++;
        s->period_total += total;
        s->period_refreshes += refreshes;
        if (s->complete && s->hits_at == BOX_SMOOTH_LOCK_HITS) {
            s->locked = true;
        } else if (!s->complete && s->hits_at > BOX_SMOOTH_COMPLETE_HITS) {
            s->complete = true;
        }

        s->period = (double)s->period_total / (double)s->period_refreshes;
    } else if (fps < s->estimated_fps) {
        box_smooth_below(s, fps);
    } else {
        box_smooth_above(s, fps);
    }

done:
    return;
}

/** Check that the measured refresh rate stays within 5% of the estimate, or before it of the
 * refresh rate the display reports, as fps_allows_smoothing does.
 *
 * @param s The smoother
 * @param delta Nanoseconds of the frame
 * @param presents Refreshes the frame measured spans
 * @param refresh Refresh rate the display reports, 0 when unknown
 * @return Whether the deltas may be smoothed
 */
static bool box_smooth_allowed(
    box_app_smoother_t *s,
    int64_t delta,
    int32_t presents,
    float refresh)
{
    double rate = s->complete ? (double)s->estimated_fps : (double)refresh;
    int64_t passed;
    double ratio;
    s->measurement_time += delta;
    s->measurement_refreshes += presents;
    s->measurement_frames ++;
    if (s->measurement_frames != s->measurement_end) {
        goto done;
    }

    passed = s->measurement_time - s->measurement_start;
    if (rate > 0 && passed > 0) {
        ratio = (1e9 * (double)s->measurement_refreshes / (double)passed) / rate;
        s->allowed = ratio > 0.95 && ratio < 1.05;
    }

    s->measured = true;
    s->measurement_start = s->measurement_time;
    s->measurement_refreshes = 0;
    s->measurement_end += BOX_SMOOTH_MEASURE_FRAMES;
done:
    return s->allowed;
}

void box_app_smoother_reset(
    box_app_smoother_t *s)
{
    ecs_os_memset_t(s, 0, box_app_smoother_t);
    s->measurement_end = BOX_SMOOTH_MEASURE_FRAMES;
    s->allowed = true;
}

int64_t box_app_smooth_delta(
    box_app_smoother_t *s,
    int64_t delta,
    int32_t presents,
    int32_t cycle,
    float refresh)
{
    int64_t result = delta;
    int32_t shown = presents > 0 ? presents : 1;
    double band, left, bleed, units, period;
    s->smoothed = false;
    if (delta > BOX_SMOOTH_MAX_DELTA || !box_smooth_allowed(s, delta, shown, refresh) ||
        delta < BOX_SMOOTH_MIN_DELTA)
    {
        goto done;
    }

    box_smooth_estimate(s, delta, shown, cycle);
    period = s->complete ? s->period : 0;
    if (!period && presents > 0 && refresh > 0 && s->measured) {
        period = 1e9 / (double)refresh;
    }

    if (!period) {
        goto done;
    }

    band = cycle == 1 ? BOX_SMOOTH_DROP : BOX_SMOOTH_DROP_CADENCE;
    s->leftover += (double)delta;
    left = s->leftover - (double)shown * period;
    units = (double)shown;
    if (left >= band * period) {
        units += 1.0 + floor((left - band * period) / period);
    }

    s->leftover -= units * period;
    bleed = s->leftover / BOX_SMOOTH_BLEED;
    bleed = bleed > period * BOX_SMOOTH_BLEED_MAX ? period * BOX_SMOOTH_BLEED_MAX : bleed;
    bleed = bleed < -period * BOX_SMOOTH_BLEED_MAX ? -period * BOX_SMOOTH_BLEED_MAX : bleed;
    s->leftover -= bleed;
    s->smoothed = true;
    result = (int64_t)llround(units * period + bleed);
done:
    return result;
}
