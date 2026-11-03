/**
 * @file particle/timeline.c
 * @brief The rate and start speed curves of ParticleTimeline: the area of the rate curve above
 * 0 over a frame of the playback clock and the speed curve at a time of it.
 */

#include "timeline.h"
#include "curves.h"
#include "simulate.h"
#include <math.h>

/** Evaluate a curve of a ParticleTimeline with the curve module, exactly at its time; a curve
 * without keys or a bad one, which its validate hook logged, is 1.
 *
 * @param curve The curve, its times from 0 to 1 of the period
 * @param t The time over the period, 0 to 1
 * @return The value
 */
static float box_particle_timeline_curve(
    const ParticleCurve *curve,
    float t)
{
    Curve view = { .kind = curve->kind, .keys = curve->keys };
    float result = 1.0f;
    if (ecs_vec_count(&curve->keys) && box_particle_curve_valid(curve)) {
        result = box_curve_value(&view, t, 1.0f);
        result = isfinite(result) ? result : 1.0f;
    }

    return result;
}

/** Get the time over the period of a ParticleTimeline at a time of the playback clock: its
 * fraction of the period with loop, else up to 1 where it holds.
 *
 * @param timeline The timeline
 * @param period Its period in microseconds, above 0
 * @param time Microseconds of the playback clock
 * @return The time over the period, 0 to 1
 */
static float box_particle_timeline_t(
    const ParticleTimeline *timeline,
    uint64_t period,
    uint64_t time)
{
    float result;
    if (timeline->loop) {
        result = (float)((double)(time % period) / (double)period);
    } else {
        result = time >= period ? 1.0f : (float)((double)time / (double)period);
    }

    return result;
}

/** Most sign changes of the cubic of a span the area of a rate curve looks for */
#define BOX_PARTICLE_TIMELINE_STEPS (32)

/** Area under the part of a curve above 0 between two times, exact per piece of constant sign.
 *
 * A span with every control at or above 0 lies above 0 and has the area of box_curve_area. A span
 * with controls on both sides is cut where its cubic crosses 0, by bisection between fixed samples.
 * The cuts do not depend on from and to, so the areas of consecutive frames add up exactly.
 *
 * @param curve The curve, with keys and valid
 * @param from The start time
 * @param to The end time, at or after from
 * @return The area of max(curve, 0) from from to to
 */
static double box_particle_timeline_area(
    const Curve *curve,
    float from,
    float to)
{
    const CurveKey *keys = ecs_vec_first_t(&curve->keys, CurveKey);
    int32_t count = ecs_vec_count(&curve->keys), i, j;
    float first = keys[0].time, last = keys[count - 1].time;
    double result = 0;
    if (to <= from) {
        goto done;
    }

    if (from < first) {
        result += (double)glm_max(keys[0].value, 0.0f) * (double)(glm_min(to, first) - from);
    }

    if (to > last) {
        result += (double)glm_max(keys[count - 1].value, 0.0f) *
            (double)(to - glm_max(from, last));
    }

    for (i = 0; i + 1 < count; i ++) {
        float t0 = keys[i].time, t1 = keys[i + 1].time, c1, c2;
        float lo = glm_max(from, t0), hi = glm_min(to, t1);
        float cuts[BOX_PARTICLE_TIMELINE_STEPS + 2];
        int32_t cut_count = 0;
        if (hi <= lo) {
            continue;
        }

        box_curve_span_controls(curve, i, &c1, &c2);
        float p0 = keys[i].value, p3 = keys[i + 1].value;
        float least = glm_min(glm_min(p0, c1), glm_min(c2, p3));
        float most = glm_max(glm_max(p0, c1), glm_max(c2, p3));
        if (least >= 0) {
            result += (double)box_curve_area(curve, lo, hi, 1.0f);
            continue;
        }

        if (most <= 0) {
            continue;
        }

        cuts[cut_count ++] = 0;
        float before = p0;
        for (j = 1; j <= BOX_PARTICLE_TIMELINE_STEPS; j ++) {
            float u1 = (float)j / (float)BOX_PARTICLE_TIMELINE_STEPS;
            float after = box_curve_cubic(p0, c1, c2, p3, u1);
            if ((before > 0) != (after > 0)) {
                float a = (float)(j - 1) / (float)BOX_PARTICLE_TIMELINE_STEPS, b = u1;
                int32_t k;
                for (k = 0; k < 40; k ++) {
                    float m = 0.5f * (a + b);
                    if ((box_curve_cubic(p0, c1, c2, p3, m) > 0) == (before > 0)) {
                        a = m;
                    } else {
                        b = m;
                    }
                }

                cuts[cut_count ++] = 0.5f * (a + b);
            }

            before = after;
        }

        cuts[cut_count ++] = 1;
        for (j = 0; j + 1 < cut_count; j ++) {
            float u = 0.5f * (cuts[j] + cuts[j + 1]);
            float a = glm_max(lo, t0 + cuts[j] * (t1 - t0));
            float b = glm_min(hi, t0 + cuts[j + 1] * (t1 - t0));
            if (b > a && box_curve_cubic(p0, c1, c2, p3, u) > 0) {
                result += (double)box_curve_area(curve, a, b, 1.0f);
            }
        }
    }

done:
    return result;
}

double box_particle_timeline_seconds(
    const ParticleTimeline *timeline,
    uint64_t t0,
    uint64_t t1,
    double delta)
{
    uint64_t period = timeline ? box_particle_us(timeline->period) : 0;
    double result = delta, seconds;
    Curve view;
    if (!period || !ecs_vec_count(&timeline->rate.keys) ||
        !box_particle_curve_valid(&timeline->rate))
    {
        goto done;
    }

    view = (Curve){ .kind = timeline->rate.kind, .keys = timeline->rate.keys };
    seconds = (double)period * 1e-6;
    if (timeline->loop) {
        uint64_t n0 = t0 / period, n1 = t1 / period;
        float f0 = box_particle_timeline_t(timeline, period, t0);
        float f1 = box_particle_timeline_t(timeline, period, t1);
        if (n0 == n1) {
            result = box_particle_timeline_area(&view, f0, f1);
        } else {
            result = box_particle_timeline_area(&view, f0, 1.0f) +
                (double)(n1 - n0 - 1) * box_particle_timeline_area(&view, 0.0f, 1.0f) +
                box_particle_timeline_area(&view, 0.0f, f1);
        }

        result *= seconds;
    } else {
        float f0 = box_particle_timeline_t(timeline, period, t0);
        float f1 = box_particle_timeline_t(timeline, period, t1);
        uint64_t h0 = t0 > period ? t0 : period, h1 = t1 > period ? t1 : period;
        result = box_particle_timeline_area(&view, f0, f1) * seconds;
        result += (double)glm_max(box_curve_value(&view, 1.0f, 1.0f), 0.0f) *
            (double)(h1 - h0) * 1e-6;
    }

    result = isfinite(result) && result > 0 ? result : 0.0;
done:
    return result;
}

float box_particle_timeline_speed(
    const ParticleTimeline *timeline,
    uint64_t time)
{
    uint64_t period = timeline ? box_particle_us(timeline->period) : 0;
    float result = 1.0f;
    if (period) {
        result = box_particle_timeline_curve(&timeline->speed,
            box_particle_timeline_t(timeline, period, time));
    }

    return result;
}
