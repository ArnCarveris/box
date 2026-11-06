/**
 * @file audio/meter_span.c
 * @brief The levels of a bus over a span: the peak and the RMS of its meters, and the
 * loudness of BS.1770 from the K-weighted squares the core sums per bus, in hops of 100 ms
 * that make the gated 400 ms blocks and the 3 s windows of the short-term loudness.
 */

#include "meter_span.h"
#include "analyze.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Absolute gate of the integrated loudness in LUFS */
#define BOX_AUDIO_SPAN_GATE (-70.0f)

/** Relative gate of the integrated loudness in LU under the loudness of the blocks */
#define BOX_AUDIO_SPAN_RELATIVE (10.0f)

/** Close the hop that fills: keep its mean power.
 *
 * @param l The levels
 */
static void box_audio_span_hop_push(
    box_audio_span_level_t *l)
{
    if (l->hop_count == l->hop_capacity) {
        int32_t capacity = l->hop_capacity ? l->hop_capacity * 2 : 64;
        double *hops = realloc(l->hops, (size_t)capacity * sizeof(double));
        if (!hops) {
            goto done;
        }

        l->hops = hops;
        l->hop_capacity = capacity;
    }

    l->hops[l->hop_count ++] = l->hop_frames > 0 ? l->hop_sum / l->hop_frames : 0;
done:
    l->hop_sum = 0;
    l->hop_frames = 0;
}

void box_audio_span_level_add(
    box_audio_span_level_t *l,
    const box_audio_meter_t *m,
    double sample_rate)
{
    double hop = BOX_AUDIO_SPAN_HOP * (sample_rate > 0 ? sample_rate : 48000.0);
    double left = (double)m->frames, loud = (double)m->loudness;
    if (!m->frames) {
        goto done;
    }

    l->peak = m->peak_left > l->peak ? m->peak_left : l->peak;
    l->peak = m->peak_right > l->peak ? m->peak_right : l->peak;
    l->square += m->square_left + m->square_right;
    l->frames += (double)m->frames;
    l->loudness = l->loudness || m->loudness > 0;
    while (left > 0) {
        double take = hop - l->hop_frames < left ? hop - l->hop_frames : left;
        l->hop_sum += loud * take / (double)m->frames;
        l->hop_frames += take;
        left -= take;
        if (l->hop_frames >= hop) {
            box_audio_span_hop_push(l);
        }
    }

done:
    return;
}

/** Get the mean power of the hops of a window.
 *
 * @param hops The hops
 * @param first First hop
 * @param count Hops of the window
 * @return The mean power
 */
static double box_audio_span_window(
    const double *hops,
    int32_t first,
    int32_t count)
{
    double sum = 0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        sum += hops[first + i];
    }

    return count > 0 ? sum / (double)count : 0;
}

/** Get the gated integrated loudness of BS.1770 over blocks of BOX_AUDIO_SPAN_BLOCK hops with
 * a step of one hop; a span shorter than a block is one block.
 *
 * @param hops The hops
 * @param count Number of hops
 * @return The loudness in LUFS
 */
static float box_audio_span_integrated(
    const double *hops,
    int32_t count)
{
    int32_t size = count < BOX_AUDIO_SPAN_BLOCK ? count : BOX_AUDIO_SPAN_BLOCK;
    int32_t blocks = count - size + 1, b, n = 0;
    double sum = 0, power;
    float relative, loudness, result = BOX_AUDIO_ANALYZE_FLOOR;
    for (b = 0; b < blocks && size > 0; b ++) {
        power = box_audio_span_window(hops, b, size);
        if (box_audio_loudness(power) > BOX_AUDIO_SPAN_GATE) {
            sum += power;
            n ++;
        }
    }

    if (!n) {
        goto done;
    }

    relative = box_audio_loudness(sum / n) - BOX_AUDIO_SPAN_RELATIVE;
    sum = 0;
    n = 0;
    for (b = 0; b < blocks; b ++) {
        power = box_audio_span_window(hops, b, size);
        loudness = box_audio_loudness(power);
        if (loudness > BOX_AUDIO_SPAN_GATE && loudness > relative) {
            sum += power;
            n ++;
        }
    }

    result = n ? box_audio_loudness(sum / n) : BOX_AUDIO_ANALYZE_FLOOR;
done:
    return result;
}

/** Get the largest short-term loudness over windows of BOX_AUDIO_SPAN_SHORT hops with a step
 * of one hop; a span shorter than a window is one window.
 *
 * @param hops The hops
 * @param count Number of hops
 * @return The loudness in LUFS
 */
static float box_audio_span_short_term(
    const double *hops,
    int32_t count)
{
    int32_t size = count < BOX_AUDIO_SPAN_SHORT ? count : BOX_AUDIO_SPAN_SHORT;
    int32_t windows = count - size + 1, w;
    float result = BOX_AUDIO_ANALYZE_FLOOR;
    for (w = 0; w < windows && size > 0; w ++) {
        float loudness = box_audio_loudness(box_audio_span_window(hops, w, size));
        result = loudness > result ? loudness : result;
    }

    return result;
}

void box_audio_span_level_get(
    const box_audio_span_level_t *l,
    box_audio_span_result_t *out)
{
    double mean = l->frames > 0 ? l->square / (2.0 * l->frames) : 0;
    memset(out, 0, sizeof(*out));
    out->peak = l->peak > 0 ? (float)(20.0 * log10((double)l->peak)) : BOX_AUDIO_ANALYZE_FLOOR;
    out->rms = mean > 0 ? (float)(10.0 * log10(mean)) : BOX_AUDIO_ANALYZE_FLOOR;
    out->peak = out->peak > BOX_AUDIO_ANALYZE_FLOOR ? out->peak : BOX_AUDIO_ANALYZE_FLOOR;
    out->rms = out->rms > BOX_AUDIO_ANALYZE_FLOOR ? out->rms : BOX_AUDIO_ANALYZE_FLOOR;
    out->loudness = l->loudness;
    out->integrated = box_audio_span_integrated(l->hops, l->hop_count);
    out->short_term_max = box_audio_span_short_term(l->hops, l->hop_count);
}

void box_audio_span_level_fini(
    box_audio_span_level_t *l)
{
    free(l->hops);
    memset(l, 0, sizeof(*l));
}

void box_audio_span_reduction_add(
    box_audio_span_reduction_t *r,
    float db,
    uint32_t frames)
{
    db = db > 0 && db < 1e30f ? db : 0;
    r->sum += (double)db * (double)frames;
    r->frames += (double)frames;
    r->max = db > r->max ? db : r->max;
}

float box_audio_span_reduction_mean(
    const box_audio_span_reduction_t *r)
{
    return r->frames > 0 ? (float)(r->sum / r->frames) : 0.0f;
}
