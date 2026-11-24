/**
 * @file render/world_origin.c
 * @brief The absolute position of the local origin (WorldOrigin) as the world space patterns
 * of the shaders take it: split per use on the CPU in double precision and wrapped to the
 * period of each pattern, so the shaders add a small offset to the local position and the
 * patterns stay continuous across a shift and precise far from the absolute origin.
 */

#include "render.h"
#include "../world/world.h"
#include <math.h>

void box_render_frame_identity(
    box_render_frame_t *frame)
{
    ecs_os_memset_t(frame, 0, box_render_frame_t);
    frame->rotation[0][0] = 1.0;
    frame->rotation[1][1] = 1.0;
    frame->rotation[2][2] = 1.0;
    frame->scale = 1.0;
}

/** Get the entity the render origin follows: WorldOrigin.render_follow when it is alive, else
 * the camera of the first view of the packet.
 *
 * @param world The world
 * @param o The WorldOrigin
 * @param p The packet
 * @return The entity, 0 for none
 */
static ecs_entity_t box_render_origin_follow(
    const ecs_world_t *world,
    const WorldOrigin *o,
    const box_render_packet_t *p)
{
    ecs_entity_t result = 0;
    if (o->render_follow && ecs_is_alive(world, o->render_follow)) {
        result = o->render_follow;
    } else if (ecs_vec_count(&p->views)) {
        result = ecs_vec_first_t(&p->views, box_render_view_t)->camera;
    }

    return result;
}

/** Get the local position of the entity the render origin follows, in f64: the corner of the
 * cell of a WorldCell root, exact at any distance, else the translation of its world transform.
 *
 * @param world The world
 * @param e The entity
 * @param frame The frame of the local origin
 * @param out Receives the position
 * @return Whether the entity has a place in the world
 */
static bool box_render_origin_follow_at(
    const ecs_world_t *world,
    ecs_entity_t e,
    const box_world_frame_t *frame,
    double out[3])
{
    const WorldCell *cell = ecs_get(world, e, WorldCell);
    WorldTransform buf;
    const WorldTransform *t = NULL;
    bool result = true;
    if (cell) {
        double corner[3] = {
            (double)cell->x * BOX_WORLD_CELL_SIZE,
            (double)cell->y * BOX_WORLD_CELL_SIZE,
            (double)cell->z * BOX_WORLD_CELL_SIZE
        };
        box_world_frame_to_local(frame, corner, out);
        goto done;
    }

    t = box_transform_world(world, e, &buf);
    result = t != NULL;
    if (result) {
        out[0] = (double)t->m[3][0];
        out[1] = (double)t->m[3][1];
        out[2] = (double)t->m[3][2];
    }
done:
    return result;
}

void box_render_origin_frame(
    const ecs_world_t *world,
    box_render_packet_t *p)
{
    const WorldOrigin *o = NULL;
    box_world_frame_t frame;
    double at[3] = {0};
    int32_t r, c;
    if (ecs_id(WorldOrigin) && ecs_is_alive(world, ecs_id(WorldOrigin))) {
        o = ecs_singleton_get(world, WorldOrigin);
    }

    box_world_frame_get(world, &frame);
    box_render_frame_identity(&p->local);
    for (r = 0; r < 3; r ++) {
        p->local.origin[r] = isfinite(frame.t[r]) ? frame.t[r] : 0.0;
        for (c = 0; c < 3; c ++) {
            p->local.rotation[c][r] = frame.r[c][r];
        }
    }

    p->local.scale = frame.s;
    ecs_entity_t follow = o && o->render ? box_render_origin_follow(world, o, p) : 0;
    if (follow && box_render_origin_follow_at(world, follow, &frame, at) &&
        isfinite(at[0]) && isfinite(at[1]) && isfinite(at[2]))
    {
        p->local.delta[0] = at[0];
        p->local.delta[1] = at[1];
        p->local.delta[2] = at[2];
    }
}

void box_render_frame_map(
    const box_render_frame_t *from,
    const box_render_frame_t *to,
    mat4 out)
{
    double d[3], m[3][3], t[3];
    int32_t r, c, k;
    for (r = 0; r < 3; r ++) {
        d[r] = from->origin[r] - to->origin[r];
    }

    double s = from->scale / to->scale;
    for (r = 0; r < 3; r ++) {
        t[r] = 0.0;
        for (k = 0; k < 3; k ++) {
            t[r] += to->rotation[r][k] * d[k];
        }

        t[r] /= to->scale;
        for (c = 0; c < 3; c ++) {
            m[r][c] = 0.0;
            for (k = 0; k < 3; k ++) {
                m[r][c] += to->rotation[r][k] * from->rotation[c][k];
            }

            m[r][c] *= s;
        }
    }

    glm_mat4_identity(out);
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out[c][r] = (float)m[r][c];
        }

        out[3][r] = (float)t[r];
    }
}

bool box_render_frame_same(
    const box_render_frame_t *a,
    const box_render_frame_t *b)
{
    return !ecs_os_memcmp(a->origin, b->origin, sizeof(a->origin)) &&
        !ecs_os_memcmp(a->rotation, b->rotation, sizeof(a->rotation)) && a->scale == b->scale;
}

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
