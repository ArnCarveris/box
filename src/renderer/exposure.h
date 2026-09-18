/**
 * @file renderer/exposure.h
 * @brief Histogram mapping and the adaptation step of the automatic exposure.
 *
 * The bin mapping and the step are the CPU twin of etc/shaders/exposure.wgsl:
 * both sides must answer the same, so a change here changes that shader too.
 */

#ifndef BOX_RENDERER_EXPOSURE_H
#define BOX_RENDERER_EXPOSURE_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "packet.h"

/** Luminance below which a texel lands in the black bin */
#define BOX_EXPOSURE_BLACK (0.00001f)

/** Average luminance an exposure without a target adapts to */
#define BOX_EXPOSURE_TARGET (0.18f)

/** Hold a value inside a range.
 *
 * @param value The value
 * @param min Lowest value of the range
 * @param max Highest value of the range
 * @return The value, clamped
 */
static inline float box_exposure_clamp(
    float value,
    float min,
    float max)
{
    float result = value;
    if (result < min) {
        result = min;
    }

    if (result > max) {
        result = max;
    }

    return result;
}

/** Histogram bin a luminance lands in.
 *
 * Bin zero holds every texel below the range, the bins above it spread the
 * stops of the range evenly in log luminance.
 *
 * @param luma Linear luminance of the texel
 * @return The bin, 0 to BOX_EXPOSURE_BINS - 1
 */
static inline uint32_t box_exposure_bin(
    float luma)
{
    uint32_t result = 0;
    if (luma >= BOX_EXPOSURE_BLACK) {
        float t = (log2f(luma) - BOX_EXPOSURE_MIN_LOG) / BOX_EXPOSURE_LOG_RANGE;
        result = (uint32_t)(box_exposure_clamp(t, 0.0f, 1.0f) *
            (float)(BOX_EXPOSURE_BINS - 2) + 1.5f);
    }

    return result;
}

/** Luminance a histogram bin stands for, the inverse of the mapping.
 *
 * @param bin The bin, which may be the weighted average of several
 * @return The luminance
 */
static inline float box_exposure_luma(
    float bin)
{
    float t = (bin - 1.0f) / (float)(BOX_EXPOSURE_BINS - 2);

    return exp2f(t * BOX_EXPOSURE_LOG_RANGE + BOX_EXPOSURE_MIN_LOG);
}

/** Average luminance of a histogram, weighted by the bin of every texel.
 *
 * The black bin is left out, so a scene of a few lit objects on a black
 * background adapts to the objects.
 *
 * @param bins Counts of every bin
 * @param count Bins the histogram holds
 * @return The average luminance, 0 for an empty histogram
 */
static inline float box_exposure_average(
    const uint32_t *bins,
    int32_t count)
{
    double weighted = 0.0;
    double counted = 0.0;
    int32_t i;
    for (i = 1; i < count; i ++) {
        weighted += (double)bins[i] * (double)i;
        counted += (double)bins[i];
    }

    float result = 0.0f;
    if (counted > 0.0) {
        result = box_exposure_luma((float)(weighted / counted));
    }

    return result;
}

/** Exposure in stops the image adapts to over one frame.
 *
 * The measured exposure is the stops the average of the image is away from the
 * target; the result moves towards it by at most the speed of its direction and
 * never leaves the range of the component.
 *
 * @param current Exposure of the previous frame in stops
 * @param average Average luminance of the image
 * @param target Average luminance the adapted image holds, 0 takes the default
 * @param speed_up Stops per second the exposure may rise
 * @param speed_down Stops per second the exposure may fall
 * @param min_ev Lowest exposure in stops
 * @param max_ev Highest exposure in stops
 * @param pinned The exposure keeps its value instead of adapting
 * @param dt Seconds the frame advanced
 * @return The exposure of this frame in stops
 */
static inline float box_exposure_step(
    float current,
    float average,
    float target,
    float speed_up,
    float speed_down,
    float min_ev,
    float max_ev,
    bool pinned,
    float dt)
{
    float aim = target > 0.0f ? target : BOX_EXPOSURE_TARGET;
    float lit = average > BOX_EXPOSURE_BLACK ? average : BOX_EXPOSURE_BLACK;
    float measured = box_exposure_clamp(log2f(aim / lit), min_ev, max_ev);
    float result = box_exposure_clamp(current, min_ev, max_ev);
    if (!pinned) {
        float up = (speed_up > 0.0f ? speed_up : 0.0f) * dt;
        float down = (speed_down > 0.0f ? speed_down : 0.0f) * dt;
        result += box_exposure_clamp(measured - result, -down, up);
    }

    return result;
}

/** Multiplier an exposure in stops scales the image by.
 *
 * @param ev The exposure in stops
 * @param compensation Stops added to it
 * @return The multiplier
 */
static inline float box_exposure_scale(
    float ev,
    float compensation)
{
    return exp2f(ev + compensation);
}

#endif
