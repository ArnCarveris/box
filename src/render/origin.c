/**
 * @file render/origin.c
 * @brief The origin the renderer draws relative to: a point on a grid of cells near the first
 * focus of the world, which moves by whole cells when the focus is more than a cell away.
 *
 * The GPU works in f32 relative to it. An instance keeps the cell it is in with its transform,
 * so a move of the origin uploads no instance again.
 *
 * The world space patterns of the shaders take the origin split per use in double precision and
 * wrapped to the period of each pattern, so they stay continuous across a move of the origin.
 */

#include "render.h"
#include <math.h>

/** The origin the renderer draws relative to, a singleton of the world */
typedef struct RenderOrigin {
    dvec3 origin;            /**< World position of the origin */
    int64_t frame;           /**< Frame the origin was last updated in, -1 before the first */
} RenderOrigin;

static ECS_COMPONENT_DECLARE(RenderOrigin);

/** Move one axis of the origin to the cell nearest to the focus when the focus is more than a
 * cell away from it.
 *
 * @param origin The axis of the origin
 * @param focus The axis of the focus
 * @return The axis of the origin
 */
static double box_render_origin_follow(
    double origin,
    double focus)
{
    double result = origin;
    if (isfinite(focus) && fabs(focus - origin) > BOX_RENDER_ORIGIN_CELL) {
        result = round(focus / BOX_RENDER_ORIGIN_CELL) * BOX_RENDER_ORIGIN_CELL;
    }

    return result;
}

void box_render_origin(
    const ecs_world_t *world,
    double out[3])
{
    const ecs_world_t *real = ecs_get_world(world);
    RenderOrigin *o = ecs_id(RenderOrigin)
        ? ECS_CONST_CAST(RenderOrigin*, ecs_singleton_get(real, RenderOrigin))
        : NULL;
    int64_t frame = ecs_get_world_info(real)->frame_count_total;
    box_world_focus_t focus;
    int32_t i;
    if (o && o->frame != frame) {
        o->frame = frame;
        if (box_world_focus(real, &focus, 1)) {
            for (i = 0; i < 3; i ++) {
                o->origin[i] = box_render_origin_follow(o->origin[i], focus.position[i]);
            }
        }
    }

    out[0] = o ? o->origin[0] : 0.0;
    out[1] = o ? o->origin[1] : 0.0;
    out[2] = o ? o->origin[2] : 0.0;
}

void box_render_transform_place(
    box_render_transform_t *out,
    const dvec3 position)
{
    int32_t r;
    for (r = 0; r < 3; r ++) {
        double cell = floor(position[r] / BOX_RENDER_ORIGIN_CELL + 0.5);
        cell = isfinite(cell) && fabs(cell) < (double)INT32_MAX ? cell : 0.0;
        out->cell[r] = (int32_t)cell;
        out->rows[r][3] = (float)(position[r] - cell * BOX_RENDER_ORIGIN_CELL);
    }

    out->cell[3] = 1;
}

void box_render_transform_world(
    box_render_transform_t *out,
    const WorldTransform *t,
    const vec3 scale)
{
    int32_t r, c;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out->rows[r][c] = t->basis[c][r] * (scale ? scale[c] : 1.0f);
        }
    }

    box_render_transform_place(out, t->position);
}

void box_render_transform_position(
    const box_render_transform_t *t,
    dvec3 out)
{
    int32_t r;
    for (r = 0; r < 3; r ++) {
        out[r] = (double)t->rows[r][3];
        if (t->cell[3]) {
            out[r] += (double)t->cell[r] * BOX_RENDER_ORIGIN_CELL;
        }
    }
}

void box_render_transform_offset(
    const box_render_transform_t *t,
    const double *origin,
    vec3 out)
{
    dvec3 position;
    int32_t r;
    box_render_transform_position(t, position);
    for (r = 0; r < 3; r ++) {
        out[r] = t->cell[3] && origin
            ? (float)(position[r] - origin[r])
            : (float)position[r];
    }
}

void box_render_transform_mat4(
    const box_render_transform_t *t,
    const double *origin,
    mat4 out)
{
    vec3 offset;
    int32_t r, c;
    box_render_transform_offset(t, origin, offset);
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out[c][r] = t->rows[r][c];
        }

        out[r][3] = 0.0f;
        out[3][r] = offset[r];
    }

    out[3][3] = 1.0f;
}

void box_render_transform_from_mat4(
    box_render_transform_t *out,
    const mat4 m,
    const double *origin)
{
    dvec3 position;
    int32_t r, c;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out->rows[r][c] = m[c][r];
        }

        position[r] = (double)m[3][r] + (origin ? origin[r] : 0.0);
    }

    box_render_transform_place(out, position);
}

void box_render_origin_import(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, RenderOrigin);
    ecs_singleton_set(world, RenderOrigin, { .frame = -1 });
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
