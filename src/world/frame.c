/**
 * @file world/frame.c
 * @brief The frame of the local origin in f64: its absolute position, rotation and scale, and
 * the conversions between local and absolute positions.
 */

#include "world.h"
#include <math.h>

void box_world_frame_set(
    box_world_frame_t *frame,
    const double t[3],
    const double q[4],
    double s)
{
    double len = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    double x = 0, y = 0, z = 0, w = 1;
    int32_t i;

    if (len > 0 && isfinite(len)) {
        x = q[0] / len;
        y = q[1] / len;
        z = q[2] / len;
        w = q[3] / len;
    }

    for (i = 0; i < 3; i ++) {
        frame->t[i] = t[i];
    }

    frame->q[0] = x;
    frame->q[1] = y;
    frame->q[2] = z;
    frame->q[3] = w;
    frame->s = s > 0 && isfinite(s) ? s : 1.0;

    frame->r[0][0] = 1 - 2 * (y * y + z * z);
    frame->r[0][1] = 2 * (x * y + w * z);
    frame->r[0][2] = 2 * (x * z - w * y);
    frame->r[1][0] = 2 * (x * y - w * z);
    frame->r[1][1] = 1 - 2 * (x * x + z * z);
    frame->r[1][2] = 2 * (y * z + w * x);
    frame->r[2][0] = 2 * (x * z + w * y);
    frame->r[2][1] = 2 * (y * z - w * x);
    frame->r[2][2] = 1 - 2 * (x * x + y * y);
}

void box_world_frame_get(
    const ecs_world_t *world,
    box_world_frame_t *out)
{
    const WorldOrigin *o = NULL;
    double t[3] = {0, 0, 0};
    double q[4] = {0, 0, 0, 1};
    double s = 1.0;

    if (ecs_id(WorldOrigin) && ecs_is_alive(world, ecs_id(WorldOrigin))) {
        o = ecs_singleton_get(world, WorldOrigin);
    }

    if (o) {
        t[0] = o->x;
        t[1] = o->y;
        t[2] = o->z;
        q[0] = o->q[0];
        q[1] = o->q[1];
        q[2] = o->q[2];
        q[3] = o->q[3];
        s = o->meters_per_unit;
    }

    box_world_frame_set(out, t, q, s);
}

bool box_world_frame_is_translation(
    const box_world_frame_t *frame)
{
    return frame->q[0] == 0 && frame->q[1] == 0 && frame->q[2] == 0 && frame->s == 1.0;
}

void box_world_frame_to_absolute(
    const box_world_frame_t *frame,
    const double local[3],
    double out[3])
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        double turned = frame->r[0][i] * local[0] + frame->r[1][i] * local[1] +
            frame->r[2][i] * local[2];
        out[i] = frame->t[i] + frame->s * turned;
    }
}

void box_world_frame_to_local(
    const box_world_frame_t *frame,
    const double absolute[3],
    double out[3])
{
    double d[3] = {
        absolute[0] - frame->t[0],
        absolute[1] - frame->t[1],
        absolute[2] - frame->t[2]
    };
    int32_t c;

    for (c = 0; c < 3; c ++) {
        double along = frame->r[c][0] * d[0] + frame->r[c][1] * d[1] + frame->r[c][2] * d[2];
        out[c] = along / frame->s;
    }
}

void box_world_quat_from_to(
    const double from[3],
    const double to[3],
    double out[4])
{
    double d = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
    double q[4];

    if (d < -0.999999) {
        double axis[3];
        if (fabs(from[0]) < 0.9) {
            axis[0] = 0;
            axis[1] = -from[2];
            axis[2] = from[1];
        } else {
            axis[0] = from[2];
            axis[1] = 0;
            axis[2] = -from[0];
        }

        q[0] = axis[0];
        q[1] = axis[1];
        q[2] = axis[2];
        q[3] = 0;
    } else {
        q[0] = from[1] * to[2] - from[2] * to[1];
        q[1] = from[2] * to[0] - from[0] * to[2];
        q[2] = from[0] * to[1] - from[1] * to[0];
        q[3] = 1 + d;
    }

    double len = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    out[0] = q[0] / len;
    out[1] = q[1] / len;
    out[2] = q[2] / len;
    out[3] = q[3] / len;
}

void box_world_quat_mul(
    const double a[4],
    const double b[4],
    double out[4])
{
    double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    double y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    double z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];

    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

void box_world_rotation_of_matrix(
    const double m[3][3],
    Rotation *out)
{
    double sx = -m[1][2];
    if (sx > 1.0) {
        sx = 1.0;
    } else if (sx < -1.0) {
        sx = -1.0;
    }

    out->x = (float)asin(sx);
    if (fabs(sx) < 0.9999999) {
        out->y = (float)atan2(m[0][2], m[2][2]);
        out->z = (float)atan2(m[1][0], m[1][1]);
    } else {
        out->y = (float)atan2(-m[2][0], m[0][0]);
        out->z = 0.0f;
    }
}
