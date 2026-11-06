/**
 * @file audio/diff.h
 * @brief Comparison of two recordings in plain C: bit identity, the offset of one against the
 * other by cross-correlation and the residual of a null test, per part of the recording. No
 * Flecs and no platform code; the per sample loops do not allocate.
 *
 * The audio diff command prints the result together with the analysis of analyze.h.
 */

#ifndef BOX_AUDIO_DIFF_H
#define BOX_AUDIO_DIFF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Frames of the part of a recording that the cross-correlation compares at most */
#define BOX_AUDIO_DIFF_ALIGN_FRAMES (16384)

/** A part of a recording with the power of its residual */
typedef struct box_audio_diff_segment_t {
    int32_t start;          /**< First frame of a */
    int32_t end;            /**< Frame of a after the last one */
    int32_t frames;         /**< Frames compared, where b has a frame at the offset */
    double power;           /**< Sum of the squares of b minus a over the channels */
    double reference;       /**< Sum of the squares of a over the same frames */
} box_audio_diff_segment_t;

/** Count the samples that differ in their bits.
 *
 * @param a The samples of the first recording
 * @param b The samples of the second recording
 * @param count Number of samples of each
 * @param first Receives the first sample that differs, -1 when none does
 * @return The number of samples that differ
 */
int64_t box_audio_diff_samples(
    const float *a,
    const float *b,
    int64_t count,
    int64_t *first);

/** Find the offset of b against a: the lag within -max_lag..max_lag at which the normalized
 * cross-correlation of the mono sums is highest, over the loudest part of a of at most
 * BOX_AUDIO_DIFF_ALIGN_FRAMES frames. Ties go to the smallest lag. Does not allocate.
 *
 * @param a Interleaved frames of the first recording
 * @param a_frames Frames of a
 * @param b Interleaved frames of the second recording
 * @param b_frames Frames of b
 * @param channels Channels of both
 * @param max_lag Largest offset tried in frames
 * @return The offset: frame n of a goes with frame n + offset of b, 0 when a or b is silent
 */
int32_t box_audio_diff_align(
    const float *a,
    int32_t a_frames,
    const float *b,
    int32_t b_frames,
    int32_t channels,
    int32_t max_lag);

/** Measure the residual of a null test, b minus a with b shifted by the offset, over parts
 * of a. Frames of a without a frame of b at the offset are left out. Does not allocate.
 *
 * @param a Interleaved frames of the first recording
 * @param a_frames Frames of a
 * @param b Interleaved frames of the second recording
 * @param b_frames Frames of b
 * @param channels Channels of both
 * @param offset Frame n of a goes with frame n + offset of b
 * @param segments The parts, start and end set by the caller, the rest receives the measures
 * @param count Number of parts
 */
void box_audio_diff_residual(
    const float *a,
    int32_t a_frames,
    const float *b,
    int32_t b_frames,
    int32_t channels,
    int32_t offset,
    box_audio_diff_segment_t *segments,
    int32_t count);

#ifdef __cplusplus
}
#endif

#endif
