/**
 * @file audio/core/clean.h
 * @brief Flushing of samples that are not finite, huge or denormal.
 */

#ifndef BOX_AUDIO_CORE_CLEAN_H
#define BOX_AUDIO_CORE_CLEAN_H

/** Smallest magnitude a sample keeps; smaller ones are flushed to 0, so no denormal forms */
#define BOX_AUDIO_TINY 1e-30f

/** Largest magnitude a sample keeps; larger ones, infinities and NaNs become 0 */
#define BOX_AUDIO_HUGE 1e30f

/** Flush a sample: NaN, infinities, huge values and denormals become 0.
 *
 * @param x The sample
 * @return The sample, or 0
 */
static inline float box_audio_clean(
    float x)
{
    float a = x < 0 ? -x : x;

    return a >= BOX_AUDIO_TINY && a <= BOX_AUDIO_HUGE ? x : 0.0f;
}

#endif
