/**
 * @file audio/diff.c
 * @brief Comparison of two recordings: bit identity, the offset of one against the other by
 * the normalized cross-correlation of their mono sums, and the residual of a null test.
 *
 * Plain C without Flecs or platform code. Nothing is allocated. The same input gives the
 * same result.
 */

#include "diff.h"
#include <math.h>
#include <string.h>

/** Smallest gain of the correlation over the best one so far that moves the offset away
 * from a smaller lag, so that a periodic sound keeps the nearest match */
#define BOX_AUDIO_DIFF_ALIGN_EPSILON (1e-6)

/** Get the sum of the channels of a frame.
 *
 * @param x Interleaved frames
 * @param channels Number of channels
 * @param i The frame
 * @return The sum of its samples
 */
static double box_audio_diff_mono(
    const float *x,
    int32_t channels,
    int64_t i)
{
    double result = 0;
    int32_t c;
    for (c = 0; c < channels; c ++) {
        result += x[i * channels + c];
    }

    return result;
}

int64_t box_audio_diff_samples(
    const float *a,
    const float *b,
    int64_t count,
    int64_t *first)
{
    int64_t i, result = 0;
    *first = -1;
    for (i = 0; i < count; i ++) {
        if (memcmp(&a[i], &b[i], sizeof(float))) {
            *first = *first < 0 ? i : *first;
            result ++;
        }
    }

    return result;
}

/** Find the loudest part of a recording of a length, by its mono sum, in steps of half the
 * length.
 *
 * @param x Interleaved frames
 * @param frames Number of frames
 * @param channels Number of channels
 * @param len Frames of the part
 * @param energy Receives the sum of the squares of the mono sum over the part
 * @return The first frame of the part
 */
static int32_t box_audio_diff_loudest(
    const float *x,
    int32_t frames,
    int32_t channels,
    int32_t len,
    double *energy)
{
    int32_t step = len / 2 > 0 ? len / 2 : 1, s, i, result = 0;
    *energy = -1;
    for (s = 0; s + len <= frames; s += step) {
        double e = 0;
        for (i = s; i < s + len; i ++) {
            double v = box_audio_diff_mono(x, channels, i);
            e += v * v;
        }

        if (e > *energy) {
            *energy = e;
            result = s;
        }
    }

    return result;
}

/** Get the normalized cross-correlation of a part of a with b at a lag.
 *
 * @param a Interleaved frames of the first recording
 * @param b Interleaved frames of the second recording
 * @param b_frames Frames of b
 * @param channels Channels of both
 * @param start First frame of the part of a
 * @param len Frames of the part
 * @param energy Sum of the squares of the mono sum of the part of a
 * @param lag Frame n of a goes with frame n + lag of b
 * @return The correlation, -1..1, 0 where b is silent
 */
static double box_audio_diff_correlate(
    const float *a,
    const float *b,
    int32_t b_frames,
    int32_t channels,
    int32_t start,
    int32_t len,
    double energy,
    int32_t lag)
{
    double ab = 0, bb = 0;
    int64_t i, j;
    for (i = start; i < (int64_t)start + len; i ++) {
        j = i + lag;
        if (j >= 0 && j < b_frames) {
            double y = box_audio_diff_mono(b, channels, j);
            ab += box_audio_diff_mono(a, channels, i) * y;
            bb += y * y;
        }
    }

    return bb > 0 ? ab / sqrt(energy * bb) : 0;
}

int32_t box_audio_diff_align(
    const float *a,
    int32_t a_frames,
    const float *b,
    int32_t b_frames,
    int32_t channels,
    int32_t max_lag)
{
    int32_t len = a_frames < BOX_AUDIO_DIFF_ALIGN_FRAMES ? a_frames :
        BOX_AUDIO_DIFF_ALIGN_FRAMES;
    int32_t start, k, s, result = 0;
    double energy = 0, best = 0;
    if (!a || !b || a_frames < 1 || b_frames < 1 || channels < 1 || max_lag < 0) {
        goto done;
    }

    start = box_audio_diff_loudest(a, a_frames, channels, len, &energy);
    if (!(energy > 0)) {
        goto done;
    }

    for (k = 0; k <= max_lag; k ++) {
        for (s = 0; s < (k ? 2 : 1); s ++) {
            int32_t lag = s ? -k : k;
            double c = box_audio_diff_correlate(a, b, b_frames, channels, start, len, energy,
                lag);
            if (c > best + BOX_AUDIO_DIFF_ALIGN_EPSILON) {
                best = c;
                result = lag;
            }
        }
    }

done:
    return result;
}

void box_audio_diff_residual(
    const float *a,
    int32_t a_frames,
    const float *b,
    int32_t b_frames,
    int32_t channels,
    int32_t offset,
    box_audio_diff_segment_t *segments,
    int32_t count)
{
    int32_t s, c;
    int64_t i, lo, hi;
    for (s = 0; s < count; s ++) {
        box_audio_diff_segment_t *seg = &segments[s];
        lo = seg->start > 0 ? seg->start : 0;
        lo = lo > -(int64_t)offset ? lo : -(int64_t)offset;
        hi = seg->end < a_frames ? seg->end : a_frames;
        hi = hi < (int64_t)b_frames - offset ? hi : (int64_t)b_frames - offset;
        seg->frames = 0;
        seg->power = 0;
        seg->reference = 0;
        for (i = lo; i < hi; i ++) {
            for (c = 0; c < channels; c ++) {
                double x = a[i * channels + c];
                double d = (double)b[(i + offset) * channels + c] - x;
                seg->power += d * d;
                seg->reference += x * x;
            }
        }

        seg->frames = hi > lo ? (int32_t)(hi - lo) : 0;
    }
}
