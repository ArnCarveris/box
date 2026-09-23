/**
 * @file lod/reduce.c
 * @brief The level ladder of a parametric shape and the level its error picks.
 *
 * A segmented shape reduces by halving its segment count. The error of a level
 * is the chord error of its segment count at the instance radius, in world
 * units, so it projects and switches like any other node.
 */

#include <box.h>
#include <math.h>

/** Pi as a float constant, used instead of the possibly undefined M_PI */
#define BOX_LOD_REDUCE_PI (3.14159265358979323846f)

/** Segments a full circle needs before it has an inside */
#define BOX_LOD_REDUCE_MIN (3)

int32_t box_lod_ladder(
    int32_t authored,
    int32_t min_segments,
    box_lod_ladder_t *out)
{
    int32_t result = 0;
    if (!out) {
        goto done;
    }

    memset(out, 0, sizeof(box_lod_ladder_t));
    int32_t floor_count = min_segments > 1 ? min_segments : 1;
    if (authored < floor_count) {
        goto done;
    }

    int32_t segments = authored;
    out->segments[0] = segments;
    out->count = 1;
    while (out->count < BOX_LOD_LEVELS && segments / 2 >= floor_count) {
        segments /= 2;
        out->segments[out->count] = segments;
        out->count ++;
    }

    result = out->count;
done:
    return result;
}

float box_lod_chord_error(
    int32_t segments,
    float radius)
{
    float result = 0.0f;
    if (segments < BOX_LOD_REDUCE_MIN || radius <= 0.0f) {
        goto done;
    }

    result = radius * (1.0f - cosf(BOX_LOD_REDUCE_PI / (float)segments));
done:
    return result;
}

int32_t box_lod_reduce(
    const box_lod_reduce_desc_t *desc)
{
    int32_t result = 0;
    if (!desc || !desc->ladder || desc->ladder->count <= 0) {
        goto done;
    }

    float error_px[BOX_LOD_LEVELS];
    float scale = box_lod_error_scale(&desc->bounds, &desc->camera, desc->viewport_px);
    float span = desc->span > 0.0f && desc->span < 1.0f ? desc->span : 1.0f;
    int32_t count = desc->ladder->count;
    int32_t i;
    for (i = 0; i < count; i ++) {
        float circle = (float)desc->ladder->segments[i] / span;
        int32_t segments = (int32_t)(circle + 0.5f);

        error_px[i] = scale * box_lod_chord_error(segments, desc->radius);
    }

    box_lod_select_desc_t select = {
        .error_px = error_px, .count = count, .current = desc->current,
        .threshold = desc->threshold, .hysteresis = desc->hysteresis
    };

    result = box_lod_select(&select);
done:
    return result;
}
