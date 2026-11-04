/**
 * @file geometry/surface_math.c
 * @brief Evaluation and containment tests for parametric surfaces.
 *
 * Covers sweeps, lofts, lathes, ellipsoids, tori, prisms, and extrusions, described by
 * box_surface_data_t parameter words and packed asset vectors.
 */

#include "paths.h"
#include <math.h>

/** Small 3D vector passed by value. */
typedef struct box_sv {
    float x; /**< X coordinate */
    float y; /**< Y coordinate */
    float z; /**< Z coordinate */
} box_sv;

/** Evaluate a cubic Bezier of vectors with box_curve_cubic3, so that the surface code calls the
 * same name as its WGSL twin, curve_cubic3 in etc/shaders/include/curve.wgsl.
 *
 * @param start The start point
 * @param control1 The first control point
 * @param control2 The second control point
 * @param end The end point
 * @param t The curve parameter in [0, 1]
 * @return The point on the curve
 */
static box_sv curve_cubic3(
    box_sv start,
    box_sv control1,
    box_sv control2,
    box_sv end,
    float t)
{
    vec3 out;
    box_curve_cubic3((vec3){start.x, start.y, start.z},
        (vec3){control1.x, control1.y, control1.z}, (vec3){control2.x, control2.y, control2.z},
        (vec3){end.x, end.y, end.z}, t, out);

    return (box_sv){out[0], out[1], out[2]};
}

/** Construct a vector.
 *
 * @param x The x coordinate
 * @param y The y coordinate
 * @param z The z coordinate
 * @return The vector
 */
static box_sv sv(
    float x,
    float y,
    float z)
{
    return (box_sv){x, y, z};
}

/** Add two vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return a + b
 */
static box_sv sv_add(
    box_sv a,
    box_sv b)
{
    return sv(a.x + b.x, a.y + b.y, a.z + b.z);
}

/** Subtract two vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return a - b
 */
static box_sv sv_sub(
    box_sv a,
    box_sv b)
{
    return sv(a.x - b.x, a.y - b.y, a.z - b.z);
}

/** Scale a vector.
 *
 * @param a The vector
 * @param b The scale factor
 * @return a * b
 */
static box_sv sv_mul(
    box_sv a,
    float b)
{
    return sv(a.x * b, a.y * b, a.z * b);
}

/** Dot product of two vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return The dot product
 */
static float sv_dot(
    box_sv a,
    box_sv b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/** Cross product of two vectors.
 *
 * @param a The first vector
 * @param b The second vector
 * @return a x b
 */
static box_sv sv_cross(
    box_sv a,
    box_sv b)
{
    return sv(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

/** Normalize a vector.
 *
 * @param a The vector
 * @return The unit vector, or (0, 1, 0) if a is (near) zero length
 */
static box_sv sv_unit(
    box_sv a)
{
    float length = sqrtf(sv_dot(a, a));
    box_sv result;
    if (length < 0.000000000001f) {
        result = sv(0.0f, 1.0f, 0.0f);
    } else {
        result = sv_mul(a, 1.0f / length);
    }

    return result;
}

/** Linearly interpolate between two vectors.
 *
 * @param a The start vector
 * @param b The end vector
 * @param t The interpolation factor
 * @return a * (1 - t) + b * t
 */
static box_sv sv_mix(
    box_sv a,
    box_sv b,
    float t)
{
    return sv_add(sv_mul(a, 1.0f - t), sv_mul(b, t));
}

/** Read a surface word, where indices from 24 address the packed asset vectors.
 *
 * @param d The surface data
 * @param i The vector index
 * @param j The component index
 * @return The value
 */
static float sf(
    const box_surface_data_t *d,
    int i,
    int j)
{
    float result;
    if (i >= 24) {
        result = d->assets[i - 24][j];
    } else {
        result = d->p[i][j];
    }

    return result;
}

/** Read the xyz components of a surface vector as a point.
 *
 * @param d The surface data
 * @param i The vector index, see sf()
 * @return The point
 */
static box_sv sp(
    const box_surface_data_t *d,
    int i)
{
    return sv(sf(d, i, 0), sf(d, i, 1), sf(d, i, 2));
}

/** Evaluate a packed path of cubic segments, each stored as four consecutive vectors.
 *
 * @param d The surface data
 * @param first Vector index of the first segment
 * @param count Number of segments
 * @param t The path parameter in [0, 1]
 * @return The point on the path
 */
static box_sv surface_path(
    const box_surface_data_t *d,
    int first,
    int count,
    float t)
{
    float parameter = t * (float)count;
    int index = (int)fminf(floorf(parameter), (float)(count - 1));
    float local = parameter - (float)index;
    int base = first + index * 4;

    return curve_cubic3(sp(d, base), sp(d, base + 1), sp(d, base + 2), sp(d, base + 3), local);
}

/** Evaluate the primary path of the surface (sweep path or lathe profile).
 *
 * @param d The surface data
 * @param t The path parameter in [0, 1]
 * @return The point on the path
 */
static box_sv surface_curve(
    const box_surface_data_t *d,
    float t)
{
    return surface_path(d, (int)sf(d, 4, 0), (int)sf(d, 4, 1), t);
}

/** Compute the unit tangent of the primary path.
 *
 * @param d The surface data
 * @param t The path parameter in [0, 1]
 * @return The unit tangent
 */
static box_sv surface_direction(
    const box_surface_data_t *d,
    float t)
{
    int count = (int)sf(d, 4, 1);
    float parameter = t * (float)count;
    int index = (int)fminf(floorf(parameter), (float)(count - 1));
    float local = parameter - (float)index;
    int base = (int)sf(d, 4, 0) + index * 4;
    box_sv a = sv_sub(sp(d, base + 1), sp(d, base));
    box_sv b = sv_sub(sp(d, base + 2), sp(d, base + 1));
    box_sv c = sv_sub(sp(d, base + 3), sp(d, base + 2));

    return sv_unit(sv_mix(sv_mix(a, b, local), sv_mix(b, c, local), local));
}

/** Compute an arbitrary unit vector perpendicular to a tangent.
 *
 * @param tangent The tangent
 * @return A unit vector perpendicular to tangent
 */
static box_sv surface_axis(
    box_sv tangent)
{
    box_sv axis = sv(0.0f, 1.0f, 0.0f);
    if (fabsf(tangent.y) > 0.9f) {
        axis = sv(1.0f, 0.0f, 0.0f);
    }

    return sv_unit(sv_cross(axis, tangent));
}

void box_surface_build_frames(
    const box_surface_data_t *d,
    vec4 *frames,
    int steps)
{
    box_sv previous = surface_direction(d, 0);
    box_sv axis = surface_axis(previous);
    glm_vec4_copy((vec4){axis.x, axis.y, axis.z, 0}, frames[0]);
    for (int i = 1; i <= steps; i ++) {
        box_sv tangent = surface_direction(d, (float)i / steps);
        box_sv turn = sv_cross(previous, tangent);
        float cosine = sv_dot(previous, tangent);
        if (cosine > -0.9999f) {
            axis = sv_add(axis, sv_add(sv_cross(turn, axis),
                sv_mul(sv_cross(turn, sv_cross(turn, axis)), 1.0f / (1.0f + cosine))));
        }

        axis = sv_unit(sv_sub(axis, sv_mul(tangent, sv_dot(axis, tangent))));
        glm_vec4_copy((vec4){axis.x, axis.y, axis.z, 0}, frames[i]);
        previous = tangent;
    }
}

/** Interpolate the precomputed sweep frame axis and orthogonalize it against the tangent.
 *
 * @param d The surface data
 * @param t The path parameter in [0, 1]
 * @param tangent The unit path tangent at t
 * @return The unit frame axis
 */
static box_sv surface_frame(
    const box_surface_data_t *d,
    float t,
    box_sv tangent)
{
    int steps = (int)sf(d, 8, 1);
    float parameter = t * (float)steps;
    int index = (int)fminf(floorf(parameter), (float)(steps - 1));
    int offset = (int)sf(d, 8, 0) + index;
    box_sv axis = sv_mix(sp(d, offset), sp(d, offset + 1), parameter - (float)index);

    return sv_unit(sv_sub(axis, sv_mul(tangent, sv_dot(axis, tangent))));
}

/** Evaluate the profile boundary, linearly interpolated between profile points.
 *
 * @param d The surface data
 * @param u The boundary parameter in [0, 1]
 * @param inner 1 to evaluate the hole rings, otherwise the outer ring
 * @return The 2D profile point in x and y
 */
static box_sv surface_boundary(
    const box_surface_data_t *d,
    float u,
    int inner)
{
    int offset = (int)sf(d, 5, 0);
    int count = (int)sf(d, 5, 2);
    int first = 0;
    if (inner == 1) {
        first = count;
        count = (int)sf(d, 5, 1) - count;
    }

    float t = u * (float)count;
    if (sf(d, 0, 0) == 1.0f && sf(d, 4, 3) == 0.0f) {
        t = u * (float)(count - 1);
    }

    int i = (int)fminf(floorf(t), (float)(count - 1));
    int next = (int)sf(d, offset + first + i, 3);

    return sv_mix(sp(d, offset + first + i), sp(d, offset + next), t - (float)i);
}

/** Analytic sample of a sweep side surface. */
typedef struct box_sweep_sample {
    box_sv position; /**< Surface position */
    box_sv normal; /**< Unnormalized surface normal */
    box_sv tangent; /**< Unnormalized derivative along u */
} box_sweep_sample;

/** Analytically evaluate position, normal, and u derivative of a sweep side surface.
 *
 * @param d The surface data
 * @param region The surface region, 1 for hole walls, otherwise the outer wall
 * @param u The profile parameter in [0, 1]
 * @param v The path parameter in [0, 1]
 * @return The sample
 */
static box_sweep_sample surface_sweep_sample(
    const box_surface_data_t *d,
    int region,
    float u,
    float v)
{
    int count = (int)sf(d, 4, 1);
    float parameter = v * (float)count;
    int index = (int)fminf(floorf(parameter), (float)(count - 1));
    float local = parameter - (float)index;
    int base = (int)sf(d, 4, 0) + index * 4;
    box_sv p0 = sp(d, base);
    box_sv p1 = sp(d, base + 1);
    box_sv p2 = sp(d, base + 2);
    box_sv p3 = sp(d, base + 3);
    box_sv a = sv_sub(p1, p0);
    box_sv b = sv_sub(p2, p1);
    box_sv c = sv_sub(p3, p2);
    box_sv direction = sv_mix(sv_mix(a, b, local), sv_mix(b, c, local), local);
    box_sv acceleration = sv_mul(sv_mix(sv_sub(b, a), sv_sub(c, b), local), 2.0f * (float)count);
    float speed = sqrtf(sv_dot(direction, direction));
    box_sv tangent = sv_unit(direction);
    box_sv tangent_delta = sv(0.0f, 0.0f, 0.0f);
    if (speed >= 0.000000000001f) {
        tangent_delta = sv_mul(sv_sub(acceleration,
            sv_mul(tangent, sv_dot(acceleration, tangent))), 1.0f / speed);
    }

    int steps = (int)sf(d, 8, 1);
    float frame_parameter = v * (float)steps;
    int frame_index = (int)fminf(floorf(frame_parameter), (float)(steps - 1));
    int offset = (int)sf(d, 8, 0) + frame_index;
    box_sv first = sp(d, offset);
    box_sv last = sp(d, offset + 1);
    box_sv reference = sv_mix(first, last, frame_parameter - (float)frame_index);
    box_sv reference_delta = sv_mul(sv_sub(last, first), (float)steps);
    float projection = sv_dot(reference, tangent);
    box_sv projected = sv_sub(reference, sv_mul(tangent, projection));
    box_sv axis = sv_unit(projected);
    box_sv projected_delta = sv_sub(sv_sub(reference_delta, sv_mul(tangent_delta, projection)),
        sv_mul(tangent, sv_dot(reference_delta, tangent) + sv_dot(reference, tangent_delta)));
    box_sv axis_delta = sv_mul(sv_sub(projected_delta,
        sv_mul(axis, sv_dot(projected_delta, axis))),
        1.0f / fmaxf(sqrtf(sv_dot(projected, projected)), 0.000000000001f));
    box_sv binormal = sv_cross(axis, tangent);
    box_sv binormal_delta = sv_add(sv_cross(axis_delta, tangent), sv_cross(axis, tangent_delta));
    box_sv point = surface_boundary(d, u, region);
    box_sv profile_delta = sv_sub(surface_boundary(d, fminf(u + 0.0001f, 1.0f), region),
        surface_boundary(d, fmaxf(u - 0.0001f, 0.0f), region));
    float taper_delta = sf(d, 1, 1) - 1.0f;
    float taper = 1.0f + taper_delta * v;
    float roll_delta = sf(d, 1, 2);
    float roll = roll_delta * v;
    float cr = cosf(roll);
    float sr = sinf(roll);
    float x = point.x * sf(d, 2, 0) * cr - point.y * sf(d, 2, 1) * sr;
    float z = point.x * sf(d, 2, 0) * sr + point.y * sf(d, 2, 1) * cr;
    float dx = profile_delta.x * sf(d, 2, 0) * cr - profile_delta.y * sf(d, 2, 1) * sr;
    float dz = profile_delta.x * sf(d, 2, 0) * sr + profile_delta.y * sf(d, 2, 1) * cr;
    box_sv du = sv_mul(sv_add(sv_mul(axis, dx), sv_mul(binormal, dz)), taper);
    box_sv dv = sv_add(sv_mul(direction, 3.0f * (float)count),
        sv_add(sv_mul(axis_delta, x * taper), sv_mul(binormal_delta, z * taper)));
    dv = sv_add(dv, sv_add(sv_mul(axis, taper_delta * x - roll_delta * taper * z),
        sv_mul(binormal, taper_delta * z + roll_delta * taper * x)));
    box_sv normal = sv_cross(dv, du);
    if (sv_dot(normal, sv_cross(tangent, du)) < 0.0f) {
        normal = sv_mul(normal, -1.0f);
    }

    box_sweep_sample result;
    result.position = sv_add(curve_cubic3(p0, p1, p2, p3, local),
        sv_mul(sv_add(sv_mul(axis, x), sv_mul(binormal, z)), taper));
    result.normal = normal;
    result.tangent = du;

    return result;
}

/** Place a 2D profile point on the sweep, applying scale, taper, and roll at v.
 *
 * @param d The surface data
 * @param point The profile point in x and y
 * @param v The path parameter in [0, 1]
 * @return The swept point
 */
static box_sv surface_sweep_point(
    const box_surface_data_t *d,
    box_sv point,
    float v)
{
    float taper = 1.0f + (sf(d, 1, 1) - 1.0f) * v;
    float a = point.x * sf(d, 2, 0) * taper;
    float b = point.y * sf(d, 2, 1) * taper;
    float roll = sf(d, 1, 2) * v;
    float x = a * cosf(roll) - b * sinf(roll);
    float z = a * sinf(roll) + b * cosf(roll);
    box_sv tangent = surface_direction(d, v);
    box_sv axis = surface_frame(d, v, tangent);

    return sv_add(surface_curve(d, v), sv_add(sv_mul(axis, x), sv_mul(sv_cross(axis, tangent), z)));
}

/** Interpolate a profile point between the two loft sections surrounding v.
 *
 * @param d The surface data
 * @param point The profile point index
 * @param v The loft parameter in [0, 1]
 * @return The lofted point
 */
static box_sv surface_loft_point(
    const box_surface_data_t *d,
    int point,
    float v)
{
    int count = (int)sf(d, 0, 2);
    int points = (int)sf(d, 5, 1);
    int offset = (int)sf(d, 4, 0);
    float t = v * (float)(count - 1);
    int section = (int)fminf(floorf(t), (float)(count - 2));

    return sv_mix(sp(d, offset + section * points + point),
        sp(d, offset + (section + 1) * points + point), t - (float)section);
}

/** Compute a prism footprint corner offset along its adjacent edge normals.
 *
 * @param d The surface data
 * @param i The corner index
 * @param inset The offset distance
 * @return The offset corner
 */
static box_sv surface_prism_corner(
    const box_surface_data_t *d,
    int i,
    float inset)
{
    int count = (int)sf(d, 0, 2);
    int previous = i - 1;
    int next = i + 1;
    if (previous < 0) {
        previous = count - 1;
    }

    if (next == count) {
        next = 0;
    }

    box_sv point = sp(d, 4 + i);
    box_sv incoming = sv_unit(sv_sub(point, sp(d, 4 + previous)));
    box_sv outgoing = sv_unit(sv_sub(sp(d, 4 + next), point));
    box_sv a = sv(-incoming.z, 0.0f, incoming.x);
    box_sv b = sv(-outgoing.z, 0.0f, outgoing.x);
    box_sv offset = sv_mul(sv_add(a, b), inset / fmaxf(1.0f + sv_dot(a, b), 0.000001f));

    return sv_add(point, offset);
}

/** Evaluate the side wall of a surface, dispatching on the surface kind.
 *
 * @param d The surface data
 * @param u The parameter around the wall in [0, 1]
 * @param v The parameter along the wall in [0, 1]
 * @param inner 1 for the inner wall (holes or thickness), 0 for the outer wall
 * @return The position, or the origin for unknown kinds
 */
static box_sv surface_outer(
    const box_surface_data_t *d,
    float u,
    float v,
    int inner)
{
    int kind = (int)sf(d, 0, 0);
    float thickness = sf(d, 1, 0) * (float)inner;
    box_sv result = sv(0.0f, 0.0f, 0.0f);
    if (kind == 1) {
        result = surface_sweep_point(d, surface_boundary(d, u, inner), v);
    } else if (kind == 2) {
        int count = (int)sf(d, 5, 2);
        int first = 0;
        if (inner == 1) {
            first = count;
            count = (int)sf(d, 5, 1) - count;
        }

        float t = u * (float)count;
        int i = (int)fminf(floorf(t), (float)(count - 1));
        int profile_offset = (int)sf(d, 5, 0);
        int next = (int)sf(d, profile_offset + first + i, 3);
        result = sv_mix(surface_loft_point(d, first + i, v), surface_loft_point(d, next, v),
            t - (float)i);
    } else if (kind == 3) {
        int first = (int)sf(d, 4, 0);
        int count = (int)sf(d, 4, 1);
        if (inner == 1) {
            first = (int)sf(d, 4, 2);
            count = (int)sf(d, 4, 3);
        }

        box_sv point = surface_path(d, first, count, v);
        result = sv(point.x * cosf(u * 6.28318530718f), point.y,
            point.x * sinf(u * 6.28318530718f));
    } else if (kind == 4 || kind == 5) {
        float a = sf(d, 2, 0) + u * (sf(d, 2, 1) - sf(d, 2, 0));
        float b = sf(d, 2, 2) + v * (sf(d, 2, 3) - sf(d, 2, 2));
        if (kind == 4) {
            box_sv radius = sp(d, 3);
            box_sv normal = sv_unit(sv(cosf(a) * cosf(b) / fmaxf(radius.x, 0.000001f),
                sinf(b) / fmaxf(radius.y, 0.000001f),
                sinf(a) * cosf(b) / fmaxf(radius.z, 0.000001f)));
            result = sv_sub(sv(radius.x * cosf(a) * cosf(b), radius.y * sinf(b),
                radius.z * sinf(a) * cosf(b)), sv_mul(normal, thickness));
        } else {
            float tube = sf(d, 3, 1) - thickness;
            float radial = sf(d, 3, 0) + tube * cosf(b);
            result = sv(radial * cosf(a), tube * sinf(b), radial * sinf(a));
        }
    } else if (kind == 6) {
        int count = (int)sf(d, 0, 2);
        float t = u * (float)count;
        int i = (int)fminf(floorf(t), (float)(count - 1));
        int next = i + 1;
        if (next == count) {
            next = 0;
        }

        float bevel = sf(d, 1, 0);
        float fraction = bevel / fmaxf(sf(d, 1, 1), 0.000001f);
        float height = fraction + (v - 0.25f) * 2.0f * (1.0f - 2.0f * fraction);
        if (v < 0.25f) {
            height = v * 4.0f * fraction;
        }

        if (v > 0.75f) {
            height = 1.0f - fraction + (v - 0.75f) * 4.0f * fraction;
        }

        float edge = fminf(height, 1.0f - height) * sf(d, 1, 1);
        float inset = sf(d, 1, 2) * height + fmaxf(bevel - edge, 0.0f);
        box_sv point = sv_mix(surface_prism_corner(d, i, inset),
            surface_prism_corner(d, next, inset), t - (float)i);
        result = sv(point.x + sf(d, 2, 0) * height, (height - 0.5f) * sf(d, 1, 1),
            point.z + sf(d, 2, 1) * height);
    } else if (kind == 7) {
        box_sv point = surface_boundary(d, u, inner);
        result = sv(point.x, (v - 0.5f) * sf(d, 1, 1), point.y);
    }

    return result;
}

/** Evaluate a point on a sweep or loft end cap over the profile bounds.
 *
 * @param d The surface data
 * @param u The parameter across the profile x range in [0, 1]
 * @param v The parameter across the profile y range in [0, 1]
 * @param side 0 for the start cap, 1 for the end cap
 * @return The cap position
 */
static box_sv surface_cap(
    const box_surface_data_t *d,
    float u,
    float v,
    float side)
{
    float x = sf(d, 6, 0) + (sf(d, 6, 1) - sf(d, 6, 0)) * u;
    float y = sf(d, 6, 2) + (sf(d, 6, 3) - sf(d, 6, 2)) * v;
    box_sv result = sv(0.0f, 0.0f, 0.0f);
    if (sf(d, 0, 0) == 1.0f) {
        result = surface_sweep_point(d, sv(x, y, 0.0f), side);
    } else {
        int offset = (int)sf(d, 7, 0);
        int count = (int)sf(d, 7, 1);
        int profile = (int)sf(d, 5, 0);
        bool found = false;
        for (int i = 0; !found && i < count; i ++) {
            int ia = (int)sf(d, offset + i, 0);
            int ib = (int)sf(d, offset + i, 1);
            int ic = (int)sf(d, offset + i, 2);
            box_sv a = sp(d, profile + ia);
            box_sv b = sp(d, profile + ib);
            box_sv c = sp(d, profile + ic);
            float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            float wb = ((x - a.x) * (c.y - a.y) - (y - a.y) * (c.x - a.x)) / area;
            float wc = ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x)) / area;
            if (wb >= -0.00001f && wc >= -0.00001f && wb + wc <= 1.00001f) {
                result = sv_add(sv_mul(surface_loft_point(d, ia, side), 1.0f - wb - wc),
                    sv_add(sv_mul(surface_loft_point(d, ib, side), wb),
                        sv_mul(surface_loft_point(d, ic, side), wc)));
                found = true;
            }
        }

        if (!found) {
            result = surface_loft_point(d, 0, side);
        }
    }

    return result;
}

/** Evaluate the position of a surface region.
 *
 * @param d The surface data
 * @param region The region: walls, caps, cap vertices, or wall edges
 * @param u The first surface parameter
 * @param v The second surface parameter
 * @return The position
 */
static box_sv surface_position(
    const box_surface_data_t *d,
    int region,
    float u,
    float v)
{
    box_sv result;
    if ((sf(d, 0, 0) == 1.0f || sf(d, 0, 0) == 2.0f) && (region == 2 || region == 3)) {
        result = surface_cap(d, u, v, (float)(region - 2));
    } else if (region == 8 || region == 9) {
        float side = (float)(region - 8);
        int point = (int)floorf(u + 0.5f);
        if (sf(d, 0, 0) == 1.0f) {
            int profile_offset = (int)sf(d, 5, 0);
            result = surface_sweep_point(d, sp(d, profile_offset + point), side);
        } else {
            result = surface_loft_point(d, point, side);
        }
    } else if (sf(d, 0, 0) == 7.0f && region >= 2) {
        float y = -0.5f;
        if (region == 3) {
            y = 0.5f;
        }

        result = sv(sf(d, 6, 0) + (sf(d, 6, 1) - sf(d, 6, 0)) * u,
            y * sf(d, 1, 1), sf(d, 6, 2) + (sf(d, 6, 3) - sf(d, 6, 2)) * v);
    } else if (region < 2) {
        result = surface_outer(d, u, v, region);
    } else {
        float a = u;
        float b = 0.0f;
        if (region == 3 || region == 7) {
            b = 1.0f;
        }

        if (region == 4 || region == 5) {
            a = 0.0f;
            if (region == 5) {
                a = 1.0f;
            }

            b = u;
        }

        box_sv outer = surface_outer(d, a, b, 0);
        box_sv inner = outer;
        int kind = (int)sf(d, 0, 0);
        if (sf(d, 1, 0) > 0.0f && kind != 6) {
            inner = surface_outer(d, a, b, 1);
        } else if (kind == 1) {
            inner = surface_curve(d, b);
        } else if (kind == 2) {
            inner = surface_loft_point(d, 0, b);
        } else if (kind == 3) {
            inner = sv(0.0f, outer.y, 0.0f);
        } else if (kind == 6) {
            inner = sp(d, 8);
            inner = sv(inner.x + sf(d, 2, 0) * b, outer.y, inner.z + sf(d, 2, 1) * b);
        }

        result = sv_mix(inner, outer, v);
    }

    return result;
}

/** Evaluate the unit normal of a surface region.
 *
 * Uses analytic normals for caps and sweep walls, finite differences otherwise.
 *
 * @param d The surface data
 * @param region The region, see surface_position()
 * @param u The first surface parameter
 * @param v The second surface parameter
 * @return The unit normal
 */
static box_sv surface_normal(
    const box_surface_data_t *d,
    int region,
    float u,
    float v)
{
    int cap_region = region;
    if ((sf(d, 0, 0) == 1.0f || sf(d, 0, 0) == 2.0f) && (region == 2 || region == 3)) {
        cap_region = region + 6;
    }

    box_sv result = sv(0.0f, 1.0f, 0.0f);
    bool found = false;
    if (cap_region == 8 || cap_region == 9) {
        float side = (float)(cap_region - 8);
        box_sv normal = sv(0.0f, 1.0f, 0.0f);
        if (sf(d, 0, 0) == 1.0f) {
            normal = surface_direction(d, side);
        }

        if (sf(d, 0, 0) == 2.0f) {
            int triangle = (int)sf(d, 7, 0);
            int ia = (int)sf(d, triangle, 0);
            int ib = (int)sf(d, triangle, 1);
            int ic = (int)sf(d, triangle, 2);
            box_sv a = surface_loft_point(d, ia, side);
            box_sv b = surface_loft_point(d, ib, side);
            box_sv c = surface_loft_point(d, ic, side);
            normal = sv_unit(sv_cross(sv_sub(c, a), sv_sub(b, a)));
        }

        if (cap_region == 8) {
            normal = sv_mul(normal, -1.0f);
        }

        result = normal;
        found = true;
    } else if (sf(d, 0, 0) == 7.0f && region >= 2) {
        if (region == 2) {
            result = sv(0.0f, -1.0f, 0.0f);
        } else {
            result = sv(0.0f, 1.0f, 0.0f);
        }

        found = true;
    } else if (sf(d, 0, 0) == 1.0f && region < 2) {
        box_sweep_sample sample = surface_sweep_sample(d, region, u, v);
        if (sv_dot(sample.normal, sample.normal) >= 0.000000000001f) {
            result = sv_unit(sample.normal);
            found = true;
        }
    }

    if (!found) {
        bool flip = (region == 1 && sf(d, 0, 0) == 3.0f) || region == 3 || region == 4 ||
            region == 7;
        float delta = 0.0001f;
        box_sv du = sv_sub(surface_position(d, region, fminf(u + delta, 1.0f), v),
            surface_position(d, region, fmaxf(u - delta, 0.0f), v));
        box_sv dv = sv_sub(surface_position(d, region, u, fminf(v + delta, 1.0f)),
            surface_position(d, region, u, fmaxf(v - delta, 0.0f)));
        box_sv normal = sv_cross(dv, du);
        if (flip) {
            normal = sv_mul(normal, -1.0f);
        }

        if (sv_dot(normal, normal) < 0.000000000001f) {
            float fallback = fminf(fmaxf(v, 0.001f), 0.999f);
            du = sv_sub(surface_position(d, region, fminf(u + delta, 1.0f), fallback),
                surface_position(d, region, fmaxf(u - delta, 0.0f), fallback));
            dv = sv_sub(surface_position(d, region, u, fallback + delta),
                surface_position(d, region, u, fallback - delta));
            normal = sv_cross(dv, du);
            if (flip) {
                normal = sv_mul(normal, -1.0f);
            }
        }

        if (sf(d, 0, 0) == 1.0f && region < 2) {
            box_sv outward = sv_cross(surface_direction(d, v), du);
            if (sv_dot(normal, outward) < 0.0f) {
                normal = sv_mul(normal, -1.0f);
            }
        }

        result = sv_unit(normal);
    }

    return result;
}

void box_surface_sample(const box_surface_data_t *data, int region,
    float u, float v, box_surface_sample_t *sample)
{
    if (sf(data, 0, 0) == 1.0f && region < 2) {
        box_sweep_sample value = surface_sweep_sample(data, region, u, v);
        box_sv normal = sv_dot(value.normal, value.normal) >= 0.000000000001f ?
            sv_unit(value.normal) : surface_normal(data, region, u, v);
        box_sv tangent = value.tangent;
        if (sv_dot(tangent, tangent) < 0.000000001f) {
            tangent = surface_axis(normal);
        }

        tangent = sv_unit(tangent);
        glm_vec3_copy((vec3){value.position.x, value.position.y, value.position.z},
            sample->position);
        glm_vec3_copy((vec3){normal.x, normal.y, normal.z}, sample->normal);
        glm_vec3_copy((vec3){tangent.x, tangent.y, tangent.z}, sample->tangent);
        return;
    }

    box_sv p = surface_position(data, region, u, v);
    box_sv n = surface_normal(data, region, u, v);
    box_sv tangent = sv_sub(surface_position(data, region, fminf(u + 0.0001f, 1.0f), v),
        surface_position(data, region, fmaxf(u - 0.0001f, 0.0f), v));
    if (sv_dot(tangent, tangent) < 0.000000001f) {
        tangent = surface_axis(n);
    }
    tangent = sv_unit(tangent);
    glm_vec3_copy((vec3){p.x, p.y, p.z}, sample->position);
    glm_vec3_copy((vec3){n.x, n.y, n.z}, sample->normal);
    glm_vec3_copy((vec3){tangent.x, tangent.y, tangent.z}, sample->tangent);
}

void box_surface_build_lengths(
    const box_surface_data_t *d,
    vec4 *frames,
    int steps,
    float *length)
{
    float *table = ecs_os_malloc_n(float, steps + 1);
    float total = 0.0f;
    if (table) {
        box_sv previous = surface_curve(d, 0.0f);
        table[0] = 0.0f;
        for (int i = 1; i <= steps; i ++) {
            box_sv point = surface_curve(d, (float)i / steps);
            box_sv delta = sv_sub(point, previous);
            total += sqrtf(sv_dot(delta, delta));
            table[i] = total;
            previous = point;
        }
    }

    int k = 0;
    for (int j = 0; j <= steps; j ++) {
        float parameter = (float)j / steps;
        if (table && total > 0.0f && isfinite(total)) {
            float target = total * (float)j / steps;
            while (k < steps - 1 && table[k + 1] < target) {
                k ++;
            }

            float span = table[k + 1] - table[k];
            float local = span > 0.0f ? (target - table[k]) / span : 0.0f;
            parameter = ((float)k + fminf(fmaxf(local, 0.0f), 1.0f)) / steps;
        }

        frames[j][3] = j == steps ? 1.0f : parameter;
    }

    ecs_os_free(table);
    *length = isfinite(total) ? total : 0.0f;
}

float box_surface_sweep_parameter(
    const box_surface_data_t *d,
    float s)
{
    int steps = (int)sf(d, 8, 1);
    float result = s;
    if (steps > 0) {
        float x = fminf(fmaxf(s, 0.0f), 1.0f) * (float)steps;
        int index = (int)fminf(floorf(x), (float)(steps - 1));
        int offset = (int)sf(d, 8, 0) + index;
        float a = sf(d, offset, 3), b = sf(d, offset + 1, 3);
        result = a + (b - a) * (x - (float)index);
    }

    return result;
}

/** Direction of a segment of a packed path at its start or its end, from the first control
 * point that differs from the end point.
 *
 * @param d The surface data
 * @param base Vector index of the first control point of the segment
 * @param end True for the direction at the end of the segment
 * @return The direction, zero for a segment of one point
 */
static box_sv surface_segment_direction(
    const box_surface_data_t *d,
    int base,
    bool end)
{
    box_sv result = sv(0.0f, 0.0f, 0.0f);
    for (int i = 1; i < 4; i ++) {
        box_sv delta = end ? sv_sub(sp(d, base + 3), sp(d, base + 3 - i)) :
            sv_sub(sp(d, base + i), sp(d, base));
        if (sv_dot(delta, delta) > 0.000000000001f) {
            result = sv_unit(delta);
            break;
        }
    }

    return result;
}

int box_surface_sweep_corners(
    const box_surface_data_t *d,
    float *parameters,
    float *fractions)
{
    int segments = (int)sf(d, 4, 1);
    int steps = (int)sf(d, 8, 1);
    int first = (int)sf(d, 4, 0);
    int count = 0;
    if (segments < 2 || steps <= 0 || steps % segments) {
        return 0;
    }

    /* The cumulative length is sampled as box_surface_build_lengths() samples it, so that a
     * corner is at the length fraction the table puts it */
    int per = steps / segments;
    box_sv previous = surface_curve(d, 0.0f);
    float total = 0.0f;
    for (int i = 1; i <= steps; i ++) {
        box_sv point = surface_curve(d, (float)i / steps);
        box_sv delta = sv_sub(point, previous);
        total += sqrtf(sv_dot(delta, delta));
        previous = point;
        int k = i / per;
        if (i % per || k >= segments) {
            continue;
        }

        box_sv a = surface_segment_direction(d, first + (k - 1) * 4, true);
        box_sv b = surface_segment_direction(d, first + k * 4, false);
        /* Joins within about half a degree, such as those of Catmull-Rom and smooth Bezier
         * paths, are not corners */
        if (sv_dot(a, a) > 0.0f && sv_dot(b, b) > 0.0f && sv_dot(a, b) < 0.99996f) {
            parameters[count] = (float)k / segments;
            fractions[count] = total;
            count ++;
        }
    }

    for (int i = 0; i < count; i ++) {
        fractions[i] = total > 0.0f && isfinite(total) ? fractions[i] / total : parameters[i];
    }

    return count;
}

/** Find the points and edges of a profile ring as surface_boundary() walks them.
 *
 * @param d The surface data
 * @param inner 1 for the hole rings, otherwise the outer ring
 * @param first Receives the index of the first point of the ring
 * @param edges Receives the number of edges u spans
 */
static void surface_ring(
    const box_surface_data_t *d,
    int inner,
    int *first,
    int *edges)
{
    int count = (int)sf(d, 5, 2);
    *first = 0;
    if (inner == 1) {
        *first = count;
        count = (int)sf(d, 5, 1) - count;
    }

    *edges = count;
    if (sf(d, 0, 0) == 1.0f && sf(d, 4, 3) == 0.0f) {
        *edges = count - 1;
    }
}

float box_surface_profile_edge(
    const box_surface_data_t *d,
    int point)
{
    int offset = (int)sf(d, 5, 0);
    box_sv a = sp(d, offset + point);
    box_sv b = sp(d, offset + (int)sf(d, offset + point, 3));

    return hypotf((b.x - a.x) * sf(d, 2, 0), (b.y - a.y) * sf(d, 2, 1));
}

float box_surface_profile_length(
    const box_surface_data_t *d,
    int inner)
{
    int first, edges;
    surface_ring(d, inner, &first, &edges);
    float result = 0.0f;
    for (int i = 0; i < edges; i ++) {
        result += box_surface_profile_edge(d, first + i);
    }

    return result;
}

float box_surface_profile_parameter(
    const box_surface_data_t *d,
    float s,
    int inner)
{
    int first, edges;
    surface_ring(d, inner, &first, &edges);
    float total = box_surface_profile_length(d, inner);
    float result = s;
    if (edges > 0 && total > 0.0f && isfinite(total)) {
        float target = fminf(fmaxf(s, 0.0f), 1.0f) * total, start = 0.0f;
        result = 1.0f;
        for (int i = 0; i < edges; i ++) {
            float length = box_surface_profile_edge(d, first + i);
            if (length > 0.0f && start + length >= target) {
                result = ((float)i + fminf(fmaxf((target - start) / length, 0.0f), 1.0f)) /
                    (float)edges;
                break;
            }

            start += length;
        }
    }

    return result;
}

void box_surface_vertex(
    const ShapeMesh *shape,
    const box_vertex_t *vertex,
    vec3 position)
{
    if (shape->surface.p[0][0] == 7) {
        glm_vec3_copy(ECS_CONST_CAST(float*, vertex->position), position);
        position[1] *= shape->surface.p[1][1];
    } else if (shape->surface.p[0][0] != 0) {
        box_sv p = surface_position(&shape->surface, (int)vertex->position[2],
            vertex->position[0], vertex->position[1]);
        glm_vec3_copy((vec3){p.x, p.y, p.z}, position);
    } else {
        glm_vec3_copy(ECS_CONST_CAST(float*, vertex->position), position);
    }
}

/** Test whether a point lies inside a closed sweep.
 *
 * @param d The surface data
 * @param position The point in shape space
 * @return True if the point is inside
 */
static bool box_surface_sweep_contains(
    const box_surface_data_t *d,
    const vec3 position)
{
    box_sv point = sv(position[0], position[1], position[2]);
    float best = INFINITY, parameter = 0;
    for (int i = 0; i <= 64; i ++) {
        float t = (float)i / 64;
        box_sv delta = sv_sub(point, surface_curve(d, t));
        float distance = sv_dot(delta, delta);
        if (distance < best) {
            best = distance;
            parameter = t;
        }
    }

    float lo = fmaxf(parameter - 1.0f / 64, 0);
    float hi = fminf(parameter + 1.0f / 64, 1);
    for (int i = 0; i < 24; i ++) {
        float a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
        box_sv da = sv_sub(point, surface_curve(d, a));
        box_sv db = sv_sub(point, surface_curve(d, b));
        if (sv_dot(da, da) < sv_dot(db, db)) {
            hi = b;
        } else {
            lo = a;
        }
    }

    float t = (lo + hi) * 0.5f;
    box_sv delta = sv_sub(point, surface_curve(d, t));
    box_sv tangent = surface_direction(d, t);
    float axial = sv_dot(delta, tangent);
    bool result;
    if ((t < 0.00001f && axial < 0) || (t > 0.99999f && axial > 0)) {
        result = false;
    } else {
        box_sv axis = surface_frame(d, t, tangent);
        box_sv binormal = sv_cross(axis, tangent);
        float x = sv_dot(delta, axis), z = sv_dot(delta, binormal);
        float roll = sf(d, 1, 2) * t;
        float rx = x * cosf(roll) + z * sinf(roll);
        float rz = -x * sinf(roll) + z * cosf(roll);
        float taper = 1 + (sf(d, 1, 1) - 1) * t;
        float sx = sf(d, 2, 0) * taper, sz = sf(d, 2, 1) * taper;
        if (sf(d, 4, 3) == 0 || sx == 0 || sz == 0) {
            result = false;
        } else {
            result = box_profile_contains(d, rx / sx, rz / sz);
        }
    }

    return result;
}

/** Test whether a point lies inside a lathe by testing its radius and height
 * against the revolved profile.
 *
 * @param d The surface data
 * @param position The point in shape space
 * @return True if the point is inside
 */
static bool box_surface_lathe_contains(
    const box_surface_data_t *d,
    const vec3 position)
{
    int outer = (int)sf(d, 4, 1) * 16 + 1;
    int inner = (int)sf(d, 4, 3) * 16 + 1;
    if (sf(d, 4, 3) == 0) {
        inner = 2;
    }

    int count = outer + inner;
    float radius = hypotf(position[0], position[2]), y = position[1];
    box_sv first = surface_curve(d, 0);
    box_sv previous = first;
    bool inside = false;
    for (int i = 1; i <= count; i ++) {
        box_sv point = first;
        if (i < outer) {
            point = surface_curve(d, (float)i / (outer - 1));
        } else if (i < count && sf(d, 4, 3) != 0) {
            point = surface_path(d, (int)sf(d, 4, 2), (int)sf(d, 4, 3),
                1 - (float)(i - outer) / (inner - 1));
        } else if (i < count) {
            point = surface_curve(d, i == outer ? 1 : 0);
            point.x = 0;
        }

        if ((previous.y > y) != (point.y > y) &&
            radius < (point.x - previous.x) * (y - previous.y) / (point.y - previous.y) +
                previous.x)
        {
            inside = !inside;
        }

        previous = point;
    }

    return inside;
}

/** Test whether an angle lies within an angular range.
 *
 * @param angle The angle in radians
 * @param start The start of the range
 * @param end The end of the range
 * @return True if the angle lies in [start, end) modulo a full turn
 */
static bool box_surface_angle_contains(
    float angle,
    float start,
    float end)
{
    float turn = 2.0f * GLM_PI;
    bool result;
    if (end - start >= turn - 0.000001f) {
        result = true;
    } else {
        float relative = fmodf(fmodf(angle - start, turn) + turn, turn);
        result = relative < end - start;
    }

    return result;
}

/** Test whether a point lies inside a (partial, possibly hollow) torus.
 *
 * @param d The surface data
 * @param position The point in shape space
 * @return True if the point is inside
 */
static bool box_surface_torus_contains(
    const box_surface_data_t *d,
    const vec3 position)
{
    float radial = hypotf(position[0], position[2]) - sf(d, 3, 0);
    float radius = hypotf(radial, position[1]);
    float tube = sf(d, 3, 1), thickness = sf(d, 1, 0);
    bool result;
    if (radius >= tube || (thickness > 0 && radius <= tube - thickness)) {
        result = false;
    } else {
        result = box_surface_angle_contains(
                atan2f(position[2], position[0]), sf(d, 2, 0), sf(d, 2, 1)) &&
            box_surface_angle_contains(atan2f(position[1], radial), sf(d, 2, 2), sf(d, 2, 3));
    }

    return result;
}

/** Test whether a point lies inside a beveled prism.
 *
 * @param d The surface data
 * @param position The point in shape space
 * @return True if the point is inside
 */
static bool box_surface_prism_contains(
    const box_surface_data_t *d,
    const vec3 position)
{
    float depth = sf(d, 1, 1);
    bool result = true;
    if (!depth || fabsf(position[1]) >= depth * 0.5f) {
        result = false;
    } else {
        float t = position[1] / depth + 0.5f;
        float inset = sf(d, 1, 2) * t + fmaxf(sf(d, 1, 0) - fminf(t, 1 - t) * depth, 0);
        float x = position[0] - sf(d, 2, 0) * t, z = position[2] - sf(d, 2, 1) * t;
        int count = (int)sf(d, 0, 2);
        for (int i = 0; result && i < count; i ++) {
            box_sv a = sp(d, 4 + i), b = sp(d, 4 + (i + 1) % count);
            float dx = b.x - a.x, dz = b.z - a.z;
            if ((dx * (z - a.z) - dz * (x - a.x)) / hypotf(dx, dz) <= inset) {
                result = false;
            }
        }
    }

    return result;
}

bool box_surface_enclosed(
    const ShapeMesh *shape,
    const box_mesh_t *mesh,
    const vec3 point)
{
    bool result = true;
    for (int j = 0; result && j < 3; j ++) {
        if (point[j] <= shape->min[j] || point[j] >= shape->max[j]) {
            result = false;
        }
    }

    if (result) {
        const box_surface_data_t *d = &shape->surface;
        int kind = (int)d->p[0][0];
        if (kind == 1) {
            result = box_surface_sweep_contains(d, point);
        } else if (kind == 3) {
            result = box_surface_lathe_contains(d, point);
        } else if (kind == 5) {
            result = box_surface_torus_contains(d, point);
        } else if (kind == 6) {
            result = box_surface_prism_contains(d, point);
        } else if (kind == 7) {
            result = box_profile_contains(&shape->surface, point[0], point[2]);
        } else {
            double angle = 0;
            for (int i = 0; i < mesh->index_count; i += 3) {
                vec3 a, b, c, cross;
                box_surface_vertex(shape, &mesh->vertices[mesh->indices[i]], a);
                box_surface_vertex(shape, &mesh->vertices[mesh->indices[i + 1]], b);
                box_surface_vertex(shape, &mesh->vertices[mesh->indices[i + 2]], c);
                glm_vec3_sub(a, ECS_CONST_CAST(float*, point), a);
                glm_vec3_sub(b, ECS_CONST_CAST(float*, point), b);
                glm_vec3_sub(c, ECS_CONST_CAST(float*, point), c);
                glm_vec3_cross(b, c, cross);
                double la = glm_vec3_norm(a), lb = glm_vec3_norm(b), lc = glm_vec3_norm(c);
                double numerator = glm_vec3_dot(a, cross);
                double denominator = la * lb * lc + glm_vec3_dot(a, b) * lc +
                    glm_vec3_dot(b, c) * la + glm_vec3_dot(c, a) * lb;
                angle += 2.0 * atan2(numerator, denominator);
            }

            result = fabs(angle) > 2.0 * GLM_PI;
        }
    }

    return result;
}
