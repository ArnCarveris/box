/**
 * @file Diff.c
 * @brief The comparison of recordings of audio/diff.c: bit identity, the offset found by
 * cross-correlation and the residual of a null test.
 */

#include "world.h"
#include <diff.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Fill interleaved frames with white noise of a linear congruential generator.
 *
 * @param x The frames
 * @param samples Number of samples
 * @param seed The seed
 */
static void diff_noise(
    float *x,
    int32_t samples,
    uint32_t seed)
{
    int32_t i;
    for (i = 0; i < samples; i ++) {
        seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
        x[i] = 0.5f * ((float)seed / (float)0x3FFFFFFF - 1.0f);
    }
}

/** Copy frames later by an offset, zeros before them, of the same length.
 *
 * @param out The copy
 * @param x The frames
 * @param frames Number of frames
 * @param channels Number of channels
 * @param delay Frames of the delay, negative to make the copy earlier
 */
static void diff_shift(
    float *out,
    const float *x,
    int32_t frames,
    int32_t channels,
    int32_t delay)
{
    int32_t i, c;
    for (i = 0; i < frames; i ++) {
        for (c = 0; c < channels; c ++) {
            int32_t j = i - delay;
            out[i * channels + c] = j >= 0 && j < frames ? x[j * channels + c] : 0;
        }
    }
}

void Diff_samples(void) {
    float a[8] = {0, 1, 2, 3, 4, 5, 6, 7}, b[8];
    int64_t first = 0;
    memcpy(b, a, sizeof(a));
    test_int(box_audio_diff_samples(a, b, 8, &first), 0);
    test_int(first, -1);
    b[3] = 3.5f;
    b[6] = -6.0f;
    test_int(box_audio_diff_samples(a, b, 8, &first), 2);
    test_int(first, 3);
    a[0] = 0.0f;
    b[0] = -0.0f;
    test_int(box_audio_diff_samples(a, b, 1, &first), 1);
    test_int(first, 0);
}

void Diff_align_delay(void) {
    int32_t frames = 48000;
    float *a = malloc(sizeof(float) * (size_t)frames);
    float *b = malloc(sizeof(float) * (size_t)frames);
    diff_noise(a, frames, 7);
    diff_shift(b, a, frames, 1, 37);
    test_int(box_audio_diff_align(a, frames, b, frames, 1, 960), 37);
    diff_shift(b, a, frames, 1, -25);
    test_int(box_audio_diff_align(a, frames, b, frames, 1, 960), -25);
    diff_shift(b, a, frames, 1, 0);
    test_int(box_audio_diff_align(a, frames, b, frames, 1, 960), 0);
    free(a);
    free(b);
}

void Diff_align_stereo(void) {
    int32_t frames = 20000;
    float *a = malloc(sizeof(float) * (size_t)frames * 2);
    float *b = malloc(sizeof(float) * (size_t)frames * 2);
    diff_noise(a, frames * 2, 11);
    diff_shift(b, a, frames, 2, 10);
    test_int(box_audio_diff_align(a, frames, b, frames, 2, 480), 10);
    free(a);
    free(b);
}

void Diff_align_out_of_window(void) {
    int32_t frames = 20000;
    float *a = malloc(sizeof(float) * (size_t)frames);
    float *b = malloc(sizeof(float) * (size_t)frames);
    diff_noise(a, frames, 3);
    diff_shift(b, a, frames, 1, 600);
    test_assert(box_audio_diff_align(a, frames, b, frames, 1, 100) != 600);
    test_int(box_audio_diff_align(a, frames, b, frames, 1, 700), 600);
    free(a);
    free(b);
}

void Diff_align_silent(void) {
    float a[256], b[256];
    memset(a, 0, sizeof(a));
    diff_noise(b, 256, 5);
    test_int(box_audio_diff_align(a, 256, b, 256, 1, 32), 0);
    test_int(box_audio_diff_align(b, 256, a, 256, 1, 32), 0);
}

void Diff_residual_gain(void) {
    int32_t i, frames = 4800;
    float *a = malloc(sizeof(float) * (size_t)frames);
    float *b = malloc(sizeof(float) * (size_t)frames);
    diff_noise(a, frames, 9);
    for (i = 0; i < frames; i ++) {
        b[i] = a[i] * 0.5f;
    }

    box_audio_diff_segment_t seg[2] = {
        { .start = 0, .end = frames },
        { .start = 1000, .end = 2000 }
    };
    box_audio_diff_residual(a, frames, b, frames, 1, 0, seg, 2);
    test_int(seg[0].frames, frames);
    test_int(seg[1].frames, 1000);
    test_assert(fabs(seg[0].power / seg[0].reference - 0.25) < 1e-9);
    test_assert(fabs(seg[1].power / seg[1].reference - 0.25) < 1e-9);

    box_audio_diff_residual(a, frames, a, frames, 1, 0, seg, 1);
    test_assert(seg[0].power == 0);
    test_assert(seg[0].reference > 0);
    free(a);
    free(b);
}

void Diff_residual_offset(void) {
    int32_t frames = 4800;
    float *a = malloc(sizeof(float) * (size_t)frames * 2);
    float *b = malloc(sizeof(float) * (size_t)frames * 2);
    diff_noise(a, frames * 2, 13);
    diff_shift(b, a, frames, 2, 10);
    box_audio_diff_segment_t seg[3] = {
        { .start = 0, .end = frames },
        { .start = frames - 5, .end = frames },
        { .start = 100, .end = 200 }
    };
    box_audio_diff_residual(a, frames, b, frames, 2, 10, seg, 3);
    test_int(seg[0].frames, frames - 10);
    test_assert(seg[0].power == 0);
    test_assert(seg[0].reference > 0);
    test_int(seg[1].frames, 0);
    test_assert(seg[1].reference == 0);
    test_int(seg[2].frames, 100);

    box_audio_diff_residual(a, frames, b, frames, 2, 0, seg, 1);
    test_int(seg[0].frames, frames);
    test_assert(seg[0].power > seg[0].reference);
    free(a);
    free(b);
}
