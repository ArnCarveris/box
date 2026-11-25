/**
 * @file render/world_origin.c
 * @brief The absolute position of the local origin (WorldOrigin) as the world space patterns
 * of the shaders take it: split per use on the CPU in double precision and wrapped to the
 * period of each pattern, so the shaders add a small offset to the local position and the
 * patterns stay continuous across a shift and precise far from the absolute origin.
 */

#include "render.h"
#include <math.h>

void box_render_origin(
    const ecs_world_t *world,
    double out[3])
{
    const WorldOrigin *o = NULL;
    out[0] = out[1] = out[2] = 0.0;
    if (ecs_id(WorldOrigin) && ecs_is_alive(world, ecs_id(WorldOrigin))) {
        o = ecs_singleton_get(world, WorldOrigin);
    }

    if (o && isfinite(o->x) && isfinite(o->y) && isfinite(o->z)) {
        out[0] = o->x;
        out[1] = o->y;
        out[2] = o->z;
    }
}

double box_render_origin_wrap(
    double v,
    double period)
{
    double result = 0.0;
    if (isfinite(v) && isfinite(period) && period > 0.0) {
        result = fmod(v, period);
        if (result < 0.0) {
            result += period;
        }

        if (result >= period) {
            result = 0.0;
        }
    }

    return result;
}

float box_render_origin_wrap_float(
    double v,
    double period)
{
    float result = (float)box_render_origin_wrap(v, period);

    return result >= (float)period ? 0.0f : result;
}

void box_render_origin_mapping(
    const double origin[3],
    float size,
    float scale_u,
    float scale_v,
    float out[4])
{
    double s = isfinite(size) && size > 0.0f ? (double)size : 1.0;
    double u = isfinite(scale_u) ? (double)scale_u : 1.0;
    double v = isfinite(scale_v) ? (double)scale_v : 1.0;
    out[0] = box_render_origin_wrap_float(origin[0] / s * u, 1.0);
    out[1] = box_render_origin_wrap_float(origin[1] / s * v, 1.0);
    out[2] = box_render_origin_wrap_float(origin[2] / s * u, 1.0);
    out[3] = box_render_origin_wrap_float(origin[0] / s * v, 1.0);
}

uint32_t box_render_origin_repeats(
    const double origin[3],
    float size,
    float scale_u,
    float scale_v,
    int32_t period)
{
    double s = isfinite(size) && size > 0.0f ? (double)size : 1.0;
    double u = isfinite(scale_u) ? (double)scale_u : 1.0;
    double v = isfinite(scale_v) ? (double)scale_v : 1.0;
    double values[4] = { origin[0] / s * u, origin[1] / s * v, origin[2] / s * u,
        origin[0] / s * v };
    uint32_t result = 0;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        uint32_t whole = (uint32_t)floor(box_render_origin_wrap(values[i], (double)period));
        result |= (whole % (uint32_t)period & 0xffu) << (8 * i);
    }

    return result;
}
