/**
 * @file box/math.h
 * @brief Inline vector and matrix math on the reflected box types.
 *
 * Conventions: left handed, X right, Y up, Z forward, column-major matrices,
 * angles in radians, reverse Z projection with an infinite far plane.
 */

#ifndef BOX_MATH_H
#define BOX_MATH_H

#include "types.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Build a vec3.
 *
 * @param x X component
 * @param y Y component
 * @param z Z component
 * @return The vector
 */
static inline vec3 vec3_new(
    float x,
    float y,
    float z)
{
    return (vec3){ x, y, z };
}

/** Add two vectors.
 *
 * @param a First vector
 * @param b Second vector
 * @return a + b
 */
static inline vec3 vec3_add(
    vec3 a,
    vec3 b)
{
    return (vec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

/** Subtract two vectors.
 *
 * @param a First vector
 * @param b Second vector
 * @return a - b
 */
static inline vec3 vec3_sub(
    vec3 a,
    vec3 b)
{
    return (vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

/** Scale a vector.
 *
 * @param a The vector
 * @param s The scalar
 * @return a * s
 */
static inline vec3 vec3_scale(
    vec3 a,
    float s)
{
    return (vec3){ a.x * s, a.y * s, a.z * s };
}

/** Multiply two vectors component wise.
 *
 * @param a First vector
 * @param b Second vector
 * @return a * b
 */
static inline vec3 vec3_mul(
    vec3 a,
    vec3 b)
{
    return (vec3){ a.x * b.x, a.y * b.y, a.z * b.z };
}

/** Dot product.
 *
 * @param a First vector
 * @param b Second vector
 * @return a . b
 */
static inline float vec3_dot(
    vec3 a,
    vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/** Cross product.
 *
 * @param a First vector
 * @param b Second vector
 * @return a x b
 */
static inline vec3 vec3_cross(
    vec3 a,
    vec3 b)
{
    return (vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

/** Vector length.
 *
 * @param a The vector
 * @return |a|
 */
static inline float vec3_length(
    vec3 a)
{
    return sqrtf(vec3_dot(a, a));
}

/** Normalize a vector, returns zero for a zero vector.
 *
 * @param a The vector
 * @return a / |a|
 */
static inline vec3 vec3_normalize(
    vec3 a)
{
    float len = vec3_length(a);

    return len > 0 ? vec3_scale(a, 1.0f / len) : a;
}

/** Component wise minimum.
 *
 * @param a First vector
 * @param b Second vector
 * @return min(a, b)
 */
static inline vec3 vec3_min(
    vec3 a,
    vec3 b)
{
    return (vec3){ fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z) };
}

/** Component wise maximum.
 *
 * @param a First vector
 * @param b Second vector
 * @return max(a, b)
 */
static inline vec3 vec3_max(
    vec3 a,
    vec3 b)
{
    return (vec3){ fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z) };
}

/** Identity matrix.
 *
 * @return The identity
 */
static inline mat4 mat4_identity(void)
{
    mat4 m = {{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }};

    return m;
}

/** Matrix element accessor.
 *
 * @param m The matrix
 * @param row The row
 * @param col The column
 * @return The element
 */
static inline float mat4_at(
    const mat4 *m,
    int row,
    int col)
{
    return m->v[col * 4 + row];
}

/** Multiply two matrices.
 *
 * @param a Left matrix
 * @param b Right matrix
 * @return a * b
 */
static inline mat4 mat4_mul(
    const mat4 *a,
    const mat4 *b)
{
    mat4 r;
    int col, row;
    for (col = 0; col < 4; col ++) {
        for (row = 0; row < 4; row ++) {
            r.v[col * 4 + row] =
                a->v[0 * 4 + row] * b->v[col * 4 + 0] +
                a->v[1 * 4 + row] * b->v[col * 4 + 1] +
                a->v[2 * 4 + row] * b->v[col * 4 + 2] +
                a->v[3 * 4 + row] * b->v[col * 4 + 3];
        }
    }

    return r;
}

/** Translation matrix.
 *
 * @param t The translation
 * @return The matrix
 */
static inline mat4 mat4_translation(
    vec3 t)
{
    mat4 m = mat4_identity();
    m.v[12] = t.x;
    m.v[13] = t.y;
    m.v[14] = t.z;

    return m;
}

/** Scale matrix.
 *
 * @param s The scale
 * @return The matrix
 */
static inline mat4 mat4_scaling(
    vec3 s)
{
    mat4 m = mat4_identity();
    m.v[0] = s.x;
    m.v[5] = s.y;
    m.v[10] = s.z;

    return m;
}

/** Rotation about the X axis.
 *
 * @param a Angle in radians
 * @return The matrix
 */
static inline mat4 mat4_rotation_x(
    float a)
{
    mat4 m = mat4_identity();
    float c = cosf(a), s = sinf(a);
    m.v[5] = c;  m.v[6] = s;
    m.v[9] = -s; m.v[10] = c;

    return m;
}

/** Rotation about the Y axis.
 *
 * @param a Angle in radians
 * @return The matrix
 */
static inline mat4 mat4_rotation_y(
    float a)
{
    mat4 m = mat4_identity();
    float c = cosf(a), s = sinf(a);
    m.v[0] = c;  m.v[2] = -s;
    m.v[8] = s;  m.v[10] = c;

    return m;
}

/** Rotation about the Z axis.
 *
 * @param a Angle in radians
 * @return The matrix
 */
static inline mat4 mat4_rotation_z(
    float a)
{
    mat4 m = mat4_identity();
    float c = cosf(a), s = sinf(a);
    m.v[0] = c;  m.v[1] = s;
    m.v[4] = -s; m.v[5] = c;

    return m;
}

/** Euler rotation composed as Rx * Ry * Rz, the Rotation component convention.
 *
 * @param e Angles about X, Y and Z
 * @return The matrix
 */
static inline mat4 mat4_rotation_xyz(
    vec3 e)
{
    mat4 rx = mat4_rotation_x(e.x), ry = mat4_rotation_y(e.y), rz = mat4_rotation_z(e.z);
    mat4 ryz = mat4_mul(&ry, &rz);

    return mat4_mul(&rx, &ryz);
}

/** Euler rotation composed as Ry * Rx * Rz, the Orientation component convention.
 *
 * @param pitch Angle about X
 * @param yaw Angle about Y
 * @param roll Angle about Z
 * @return The matrix
 */
static inline mat4 mat4_rotation_yxz(
    float pitch,
    float yaw,
    float roll)
{
    mat4 rx = mat4_rotation_x(pitch), ry = mat4_rotation_y(yaw), rz = mat4_rotation_z(roll);
    mat4 rxz = mat4_mul(&rx, &rz);

    return mat4_mul(&ry, &rxz);
}

/** Compose translation, rotation and scale into T * R * S.
 *
 * @param t Translation
 * @param r Rotation matrix
 * @param s Scale
 * @return The matrix
 */
static inline mat4 mat4_trs(
    vec3 t,
    const mat4 *r,
    vec3 s)
{
    mat4 m = *r;
    m.v[0] *= s.x; m.v[1] *= s.x; m.v[2] *= s.x;
    m.v[4] *= s.y; m.v[5] *= s.y; m.v[6] *= s.y;
    m.v[8] *= s.z; m.v[9] *= s.z; m.v[10] *= s.z;
    m.v[12] = t.x; m.v[13] = t.y; m.v[14] = t.z;

    return m;
}

/** Transform a point, applying translation.
 *
 * @param m The matrix
 * @param p The point
 * @return m * (p, 1)
 */
static inline vec3 mat4_transform_point(
    const mat4 *m,
    vec3 p)
{
    return (vec3){
        m->v[0] * p.x + m->v[4] * p.y + m->v[8] * p.z + m->v[12],
        m->v[1] * p.x + m->v[5] * p.y + m->v[9] * p.z + m->v[13],
        m->v[2] * p.x + m->v[6] * p.y + m->v[10] * p.z + m->v[14]
    };
}

/** Transform a direction, ignoring translation.
 *
 * @param m The matrix
 * @param d The direction
 * @return m * (d, 0)
 */
static inline vec3 mat4_transform_dir(
    const mat4 *m,
    vec3 d)
{
    return (vec3){
        m->v[0] * d.x + m->v[4] * d.y + m->v[8] * d.z,
        m->v[1] * d.x + m->v[5] * d.y + m->v[9] * d.z,
        m->v[2] * d.x + m->v[6] * d.y + m->v[10] * d.z
    };
}

/** Translation part of a matrix.
 *
 * @param m The matrix
 * @return The translation
 */
static inline vec3 mat4_get_translation(
    const mat4 *m)
{
    return (vec3){ m->v[12], m->v[13], m->v[14] };
}

/** General 4x4 inverse.
 *
 * @param m The matrix
 * @return The inverse, or identity when singular
 */
static inline mat4 mat4_inverse(
    const mat4 *m)
{
    const float *a = m->v;
    float inv[16];
    inv[0] =
        a[5]*a[10]*a[15]
        - a[5]*a[11]*a[14]
        - a[9]*a[6]*a[15]
        + a[9]*a[7]*a[14]
        + a[13]*a[6]*a[11]
        - a[13]*a[7]*a[10];
    inv[4] =
        - a[4]*a[10]*a[15]
        + a[4]*a[11]*a[14]
        + a[8]*a[6]*a[15]
        - a[8]*a[7]*a[14]
        - a[12]*a[6]*a[11]
        + a[12]*a[7]*a[10];
    inv[8] =
        a[4]*a[9]*a[15]
        - a[4]*a[11]*a[13]
        - a[8]*a[5]*a[15]
        + a[8]*a[7]*a[13]
        + a[12]*a[5]*a[11]
        - a[12]*a[7]*a[9];
    inv[12] =
        - a[4]*a[9]*a[14]
        + a[4]*a[10]*a[13]
        + a[8]*a[5]*a[14]
        - a[8]*a[6]*a[13]
        - a[12]*a[5]*a[10]
        + a[12]*a[6]*a[9];
    inv[1] =
        - a[1]*a[10]*a[15]
        + a[1]*a[11]*a[14]
        + a[9]*a[2]*a[15]
        - a[9]*a[3]*a[14]
        - a[13]*a[2]*a[11]
        + a[13]*a[3]*a[10];
    inv[5] =
        a[0]*a[10]*a[15]
        - a[0]*a[11]*a[14]
        - a[8]*a[2]*a[15]
        + a[8]*a[3]*a[14]
        + a[12]*a[2]*a[11]
        - a[12]*a[3]*a[10];
    inv[9] =
        - a[0]*a[9]*a[15]
        + a[0]*a[11]*a[13]
        + a[8]*a[1]*a[15]
        - a[8]*a[3]*a[13]
        - a[12]*a[1]*a[11]
        + a[12]*a[3]*a[9];
    inv[13] =
        a[0]*a[9]*a[14]
        - a[0]*a[10]*a[13]
        - a[8]*a[1]*a[14]
        + a[8]*a[2]*a[13]
        + a[12]*a[1]*a[10]
        - a[12]*a[2]*a[9];
    inv[2] =
        a[1]*a[6]*a[15]
        - a[1]*a[7]*a[14]
        - a[5]*a[2]*a[15]
        + a[5]*a[3]*a[14]
        + a[13]*a[2]*a[7]
        - a[13]*a[3]*a[6];
    inv[6] =
        - a[0]*a[6]*a[15]
        + a[0]*a[7]*a[14]
        + a[4]*a[2]*a[15]
        - a[4]*a[3]*a[14]
        - a[12]*a[2]*a[7]
        + a[12]*a[3]*a[6];
    inv[10] =
        a[0]*a[5]*a[15]
        - a[0]*a[7]*a[13]
        - a[4]*a[1]*a[15]
        + a[4]*a[3]*a[13]
        + a[12]*a[1]*a[7]
        - a[12]*a[3]*a[5];
    inv[14] =
        - a[0]*a[5]*a[14]
        + a[0]*a[6]*a[13]
        + a[4]*a[1]*a[14]
        - a[4]*a[2]*a[13]
        - a[12]*a[1]*a[6]
        + a[12]*a[2]*a[5];
    inv[3] =
        - a[1]*a[6]*a[11]
        + a[1]*a[7]*a[10]
        + a[5]*a[2]*a[11]
        - a[5]*a[3]*a[10]
        - a[9]*a[2]*a[7]
        + a[9]*a[3]*a[6];
    inv[7] =
        a[0]*a[6]*a[11]
        - a[0]*a[7]*a[10]
        - a[4]*a[2]*a[11]
        + a[4]*a[3]*a[10]
        + a[8]*a[2]*a[7]
        - a[8]*a[3]*a[6];
    inv[11] =
        - a[0]*a[5]*a[11]
        + a[0]*a[7]*a[9]
        + a[4]*a[1]*a[11]
        - a[4]*a[3]*a[9]
        - a[8]*a[1]*a[7]
        + a[8]*a[3]*a[5];
    inv[15] =
        a[0]*a[5]*a[10]
        - a[0]*a[6]*a[9]
        - a[4]*a[1]*a[10]
        + a[4]*a[2]*a[9]
        + a[8]*a[1]*a[6]
        - a[8]*a[2]*a[5];
    float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    mat4 r;
    if (det == 0) {
        r = mat4_identity();
    } else {
        int i;
        for (i = 0; i < 16; i ++) {
            r.v[i] = inv[i] / det;
        }
    }

    return r;
}

/** View matrix looking from eye towards target with local +Z pointing at it.
 *
 * @param eye Camera position
 * @param target Point to look at
 * @param up Up hint
 * @return The view matrix
 */
static inline mat4 mat4_look_at(
    vec3 eye,
    vec3 target,
    vec3 up)
{
    vec3 f = vec3_normalize(vec3_sub(target, eye));
    vec3 r = vec3_normalize(vec3_cross(up, f));
    vec3 u = vec3_cross(f, r);
    mat4 m = mat4_identity();
    m.v[0] = r.x; m.v[4] = r.y; m.v[8] = r.z;
    m.v[1] = u.x; m.v[5] = u.y; m.v[9] = u.z;
    m.v[2] = f.x; m.v[6] = f.y; m.v[10] = f.z;
    m.v[12] = -vec3_dot(r, eye);
    m.v[13] = -vec3_dot(u, eye);
    m.v[14] = -vec3_dot(f, eye);

    return m;
}

/** Rotation matrix whose local +Z points along a direction.
 *
 * @param dir Direction to face
 * @param up Up hint
 * @return The rotation
 */
static inline mat4 mat4_facing(
    vec3 dir,
    vec3 up)
{
    vec3 f = vec3_normalize(dir);
    vec3 r = vec3_normalize(vec3_cross(up, f));
    vec3 u = vec3_cross(f, r);
    mat4 m = mat4_identity();
    m.v[0] = r.x; m.v[1] = r.y; m.v[2] = r.z;
    m.v[4] = u.x; m.v[5] = u.y; m.v[6] = u.z;
    m.v[8] = f.x; m.v[9] = f.y; m.v[10] = f.z;

    return m;
}

/** Reverse Z perspective projection with an infinite far plane.
 *
 * @param fov_y Vertical field of view in radians
 * @param aspect Width over height
 * @param near Near plane distance, maps to depth 1
 * @return The projection
 */
static inline mat4 mat4_perspective(
    float fov_y,
    float aspect,
    float near)
{
    float f = 1.0f / tanf(fov_y * 0.5f);
    mat4 m = {{ 0 }};
    m.v[0] = f / aspect;
    m.v[5] = f;
    m.v[10] = 0;
    m.v[11] = 1;
    m.v[14] = near;

    return m;
}

/** Reverse Z orthographic projection.
 *
 * @param height Visible height in world units
 * @param aspect Width over height
 * @param near Near plane, maps to depth 1
 * @param far Far plane, maps to depth 0
 * @return The projection
 */
static inline mat4 mat4_orthographic(
    float height,
    float aspect,
    float near,
    float far)
{
    mat4 m = mat4_identity();
    m.v[0] = 2.0f / (height * aspect);
    m.v[5] = 2.0f / height;
    m.v[10] = -1.0f / (far - near);
    m.v[14] = far / (far - near);

    return m;
}

#ifdef __cplusplus
}
#endif

#endif
