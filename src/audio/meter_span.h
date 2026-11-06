/**
 * @file audio/meter_span.h
 * @brief The levels of a bus over a span, from the meters the core reports once per frame:
 * the peak, the RMS, the integrated and the largest short-term loudness (EBU R128, with the
 * loudness of analyze.h), and the gain reduction of an effect. Plain C, no Flecs; a frame
 * adds one meter, and only a hop of the loudness that fills up grows its array.
 */

#ifndef BOX_AUDIO_METER_SPAN_H
#define BOX_AUDIO_METER_SPAN_H

#include "core/core.h"

/** Seconds of a hop of the loudness: the step of its 400 ms blocks and of its 3 s windows */
#define BOX_AUDIO_SPAN_HOP (0.1)

/** Hops of a block of the momentary loudness, which the integrated loudness gates */
#define BOX_AUDIO_SPAN_BLOCK (4)

/** Hops of a window of the short-term loudness */
#define BOX_AUDIO_SPAN_SHORT (30)

/** A reduction under this many dB counts as none: the effect never engages */
#define BOX_AUDIO_SPAN_ENGAGED (0.1f)

/** The levels of a bus over a span */
typedef struct box_audio_span_level_t {
    float peak;             /**< Largest absolute sample of both channels, linear */
    double square;          /**< Sum of the squares of both channels */
    double frames;          /**< Frames measured */
    bool loudness;          /**< Some frames came with their K-weighted squares */
    double hop_sum;         /**< K-weighted squares of the hop that fills */
    double hop_frames;      /**< Frames of that hop */
    double *hops;           /**< Mean K-weighted power of each full hop, summed over the
                             *   channels */
    int32_t hop_count;      /**< Full hops */
    int32_t hop_capacity;   /**< Room in hops */
} box_audio_span_level_t;

/** The levels of a span in dB */
typedef struct box_audio_span_result_t {
    float peak;             /**< Sample peak in dBFS, BOX_AUDIO_ANALYZE_FLOOR for silence */
    float rms;              /**< RMS of both channels in dB */
    float integrated;       /**< Gated integrated loudness in LUFS */
    float short_term_max;   /**< Largest short-term loudness in LUFS, over the whole span when
                             *   it is shorter than a window */
    bool loudness;          /**< The loudness was measured */
} box_audio_span_result_t;

/** The gain reduction of an effect over a span */
typedef struct box_audio_span_reduction_t {
    double sum;             /**< Reduction of each meter in dB times its frames */
    double frames;          /**< Frames measured */
    float max;              /**< Largest reduction in dB */
} box_audio_span_reduction_t;

/** Add the meter of a bus over one read to its levels.
 *
 * @param l The levels
 * @param m The meter of the read, BOX_AUDIO_PARAM_LOUDNESS on for the loudness
 * @param sample_rate Frames per second
 */
void box_audio_span_level_add(
    box_audio_span_level_t *l,
    const box_audio_meter_t *m,
    double sample_rate);

/** Get the levels of a span in dB.
 *
 * @param l The levels
 * @param out Receives the result
 */
void box_audio_span_level_get(
    const box_audio_span_level_t *l,
    box_audio_span_result_t *out);

/** Free the hops of the levels and start them over.
 *
 * @param l The levels
 */
void box_audio_span_level_fini(
    box_audio_span_level_t *l);

/** Add the largest reduction of an effect over one read.
 *
 * @param r The reduction
 * @param db Largest reduction of the read in dB
 * @param frames Frames of the read
 */
void box_audio_span_reduction_add(
    box_audio_span_reduction_t *r,
    float db,
    uint32_t frames);

/** Get the reduction of an effect averaged over the frames of a span.
 *
 * @param r The reduction
 * @return The average in dB, 0 when nothing was measured
 */
float box_audio_span_reduction_mean(
    const box_audio_span_reduction_t *r);

#endif
