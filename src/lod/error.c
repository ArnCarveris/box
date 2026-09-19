/**
 * @file lod/error.c
 * @brief The screen space error metric and the level switch its band guards.
 *
 * The metric projects an error in world units at the nearest point of the
 * bounds of a node, for a perspective and for an orthographic camera alike, and
 * the switch keeps a node at its level until the projected error leaves the
 * band around the threshold of the view.
 */

#include <box.h>
#include <math.h>

/** Degrees to radians, used instead of the possibly undefined M_PI */
#define BOX_LOD_RADIANS (3.14159265358979323846f / 180.0f)

/** Widest field of view the metric projects with, in degrees */
#define BOX_LOD_FOV_MAX (179.0f)

/** Distance from a value to the nearest point of an interval.
 *
 * @param min One end of the interval
 * @param max The other end of the interval
 * @param value The value
 * @return The distance, zero when the value lies inside the interval
 */
static float box_lod_axis(
    float min,
    float max,
    float value)
{
    float lo = fminf(min, max);
    float hi = fmaxf(min, max);
    float result = 0.0f;
    if (value < lo) {
        result = lo - value;
    } else if (value > hi) {
        result = value - hi;
    }

    return result;
}

/** Coarsest level whose projected error stays under a threshold.
 *
 * @param error_px Projected error in pixels of every level
 * @param count Number of levels
 * @param threshold Error in pixels a level may not exceed
 * @return The level, zero when no level fits the threshold
 */
static int32_t box_lod_coarsest(
    const float *error_px,
    int32_t count,
    float threshold)
{
    int32_t result = 0;
    int32_t i;
    for (i = count - 1; i > 0; i --) {
        if (error_px[i] <= threshold) {
            result = i;
            break;
        }
    }

    return result;
}

float box_lod_error_scale(
    const box_lod_bounds_t *bounds,
    const box_lod_camera_t *camera,
    float viewport_px)
{
    float result = 0.0f;
    if (!bounds || !camera || viewport_px <= 0.0f) {
        goto done;
    }

    if (camera->orthographic) {
        float height = camera->ortho_height > 0.0f ?
            camera->ortho_height : BOX_LOD_ORTHO_HEIGHT;
        result = viewport_px / height;
        goto done;
    }

    float fov = camera->fov > 0.0f ? camera->fov : BOX_LOD_FOV;
    if (fov > BOX_LOD_FOV_MAX) {
        fov = BOX_LOD_FOV_MAX;
    }

    float x = box_lod_axis(bounds->min.x, bounds->max.x, camera->position.x);
    float y = box_lod_axis(bounds->min.y, bounds->max.y, camera->position.y);
    float z = box_lod_axis(bounds->min.z, bounds->max.z, camera->position.z);
    float distance = sqrtf(x * x + y * y + z * z);
    if (distance < BOX_LOD_MIN_DISTANCE) {
        distance = BOX_LOD_MIN_DISTANCE;
    }

    result = viewport_px / (2.0f * tanf(0.5f * fov * BOX_LOD_RADIANS) * distance);
done:
    return result;
}

float box_lod_error_px(
    const box_lod_bounds_t *bounds,
    float error,
    const box_lod_camera_t *camera,
    float viewport_px)
{
    float result = 0.0f;
    if (error <= 0.0f) {
        goto done;
    }

    result = error * box_lod_error_scale(bounds, camera, viewport_px);
done:
    return result;
}

int32_t box_lod_select(
    const box_lod_select_desc_t *desc)
{
    int32_t result = 0;
    if (!desc || !desc->error_px || desc->count <= 0) {
        goto done;
    }

    float band = desc->hysteresis > 0.0f ? desc->hysteresis : 0.0f;
    if (band > 1.0f) {
        band = 1.0f;
    }

    int32_t current = desc->current;
    if (current < 0 || current >= desc->count) {
        result = box_lod_coarsest(desc->error_px, desc->count, desc->threshold);
        goto done;
    }

    int32_t coarser = box_lod_coarsest(desc->error_px, desc->count,
        desc->threshold * (1.0f - band));
    int32_t finer = box_lod_coarsest(desc->error_px, desc->count,
        desc->threshold * (1.0f + band));
    result = current;
    if (coarser > current) {
        result = coarser;
    } else if (finer < current) {
        result = finer;
    }
done:
    return result;
}
