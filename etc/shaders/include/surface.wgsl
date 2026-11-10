#pragma once

#include "curve.wgsl"

struct SurfaceData {
    p: array<vec4f, 24>,
};

var<private> surface_data: SurfaceData;

fn sv(x: f32, y: f32, z: f32) -> vec3f {
    return vec3f(x, y, z);
}

fn sv_add(a: vec3f, b: vec3f) -> vec3f {
    return sv(a.x + b.x, a.y + b.y, a.z + b.z);
}

fn sv_sub(a: vec3f, b: vec3f) -> vec3f {
    return sv(a.x - b.x, a.y - b.y, a.z - b.z);
}

fn sv_mul(a: vec3f, b: f32) -> vec3f {
    return sv(a.x * b, a.y * b, a.z * b);
}

fn sv_dot(a: vec3f, b: vec3f) -> f32 {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

fn sv_cross(a: vec3f, b: vec3f) -> vec3f {
    return sv(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

fn sv_unit(a: vec3f) -> vec3f {
    var length = sqrt(sv_dot(a, a));
    if (length < 0.000000000001) {
        return sv(0.0, 1.0, 0.0);
    }

    return sv_mul(a, 1.0 / length);
}

fn sv_mix(a: vec3f, b: vec3f, t: f32) -> vec3f {
    return sv_add(sv_mul(a, 1.0 - t), sv_mul(b, t));
}

fn sf(i: i32, j: i32) -> f32 {
    if (i >= 24) {
        return surface_assets[i - 24][j];
    }

    return surface_data.p[i][j];
}

fn sp(i: i32) -> vec3f {
    return sv(sf(i, 0), sf(i, 1), sf(i, 2));
}

fn surface_path(first: i32, count: i32, t: f32) -> vec3f {
    var parameter = t * f32(count);
    var index = i32(min(floor(parameter), f32(count - 1)));
    var local = parameter - f32(index);
    var base = first + index * 4;

    return curve_cubic3(sp(base), sp(base + 1), sp(base + 2), sp(base + 3), local);
}

fn surface_curve(t: f32) -> vec3f {
    return surface_path(i32(sf(4, 0)), i32(sf(4, 1)), t);
}

fn surface_direction(t: f32) -> vec3f {
    var count = i32(sf(4, 1));
    var parameter = t * f32(count);
    var index = i32(min(floor(parameter), f32(count - 1)));
    var local = parameter - f32(index);
    var base = i32(sf(4, 0)) + index * 4;
    var a = sv_sub(sp(base + 1), sp(base));
    var b = sv_sub(sp(base + 2), sp(base + 1));
    var c = sv_sub(sp(base + 3), sp(base + 2));

    return sv_unit(sv_mix(sv_mix(a, b, local), sv_mix(b, c, local), local));
}

fn surface_axis(tangent: vec3f) -> vec3f {
    var axis = sv(0.0, 1.0, 0.0);
    if (abs(tangent.y) > 0.9) {
        axis = sv(1.0, 0.0, 0.0);
    }

    return sv_unit(sv_cross(axis, tangent));
}

fn surface_frame(t: f32, tangent: vec3f) -> vec3f {
    var steps = i32(sf(8, 1));
    var parameter = t * f32(steps);
    var index = i32(min(floor(parameter), f32(steps - 1)));
    var offset = i32(sf(8, 0)) + index;
    var axis = sv_mix(sp(offset), sp(offset + 1), parameter - f32(index));

    return sv_unit(sv_sub(axis, sv_mul(tangent, sv_dot(axis, tangent))));
}

fn surface_boundary(u: f32, inner: i32) -> vec3f {
    var offset = i32(sf(5, 0));
    var count = i32(sf(5, 2));
    var first = 0;
    if (inner == 1) {
        first = count;
        count = i32(sf(5, 1)) - count;
    }

    var t = u * f32(count);
    if (sf(0, 0) == 1.0 && sf(4, 3) == 0.0) {
        t = u * f32(count - 1);
    }

    var i = i32(min(floor(t), f32(count - 1)));
    var next = i32(sf(offset + first + i, 3));

    return sv_mix(sp(offset + first + i), sp(offset + next), t - f32(i));
}

fn surface_profile_normal(u: f32, inner: i32) -> vec3f {
    var offset = i32(sf(5, 0));
    var count = i32(sf(5, 2));
    var first = 0;
    if (inner == 1) {
        first = count;
        count = i32(sf(5, 1)) - count;
    }

    var t = u * f32(count);
    var i = i32(min(floor(t), f32(count - 1)));
    var next = i32(sf(offset + first + i, 3));
    var a = sp(offset + first + i);
    var b = sp(offset + next);
    var normal = sv(b.y - a.y, 0.0, a.x - b.x);
    if (sf(8, 0) > 0.0) {
        var normals = i32(sf(8, 0));
        var from_normal = sv(sf(normals + first + i, 2), 0.0,
            sf(normals + first + i, 3));
        var to_normal = sv(sf(normals + next, 0), 0.0, sf(normals + next, 1));
        normal = sv_mix(from_normal, to_normal, t - f32(i));
    }

    if (sv_dot(normal, normal) < 0.000000000001) {
        return normal;
    }

    return sv_unit(normal);
}

struct SurfaceSweepSample {
    position: vec3f,
    normal: vec3f,
    tangent: vec3f,
};

fn surface_sweep_sample(region: i32, u: f32, v: f32) -> SurfaceSweepSample {
    var count = i32(sf(4, 1));
    var parameter = v * f32(count);
    var index = i32(min(floor(parameter), f32(count - 1)));
    var local = parameter - f32(index);
    var base = i32(sf(4, 0)) + index * 4;
    var p0 = sp(base);
    var p1 = sp(base + 1);
    var p2 = sp(base + 2);
    var p3 = sp(base + 3);
    var a = sv_sub(p1, p0);
    var b = sv_sub(p2, p1);
    var c = sv_sub(p3, p2);
    var direction = sv_mix(sv_mix(a, b, local), sv_mix(b, c, local), local);
    var acceleration = sv_mul(sv_mix(sv_sub(b, a), sv_sub(c, b), local), 2.0 * f32(count));
    var speed = sqrt(sv_dot(direction, direction));
    var tangent = sv_unit(direction);
    var tangent_delta = sv(0.0, 0.0, 0.0);
    if (speed >= 0.000000000001) {
        tangent_delta = sv_mul(sv_sub(acceleration,
            sv_mul(tangent, sv_dot(acceleration, tangent))), 1.0 / speed);
    }

    var steps = i32(sf(8, 1));
    var frame_parameter = v * f32(steps);
    var frame_index = i32(min(floor(frame_parameter), f32(steps - 1)));
    var offset = i32(sf(8, 0)) + frame_index;
    var first = sp(offset);
    var last = sp(offset + 1);
    var reference = sv_mix(first, last, frame_parameter - f32(frame_index));
    var reference_delta = sv_mul(sv_sub(last, first), f32(steps));
    var projection = sv_dot(reference, tangent);
    var projected = sv_sub(reference, sv_mul(tangent, projection));
    var axis = sv_unit(projected);
    var projected_delta = sv_sub(sv_sub(reference_delta, sv_mul(tangent_delta, projection)),
        sv_mul(tangent, sv_dot(reference_delta, tangent) + sv_dot(reference, tangent_delta)));
    var axis_delta = sv_mul(sv_sub(projected_delta,
        sv_mul(axis, sv_dot(projected_delta, axis))),
        1.0 / max(sqrt(sv_dot(projected, projected)), 0.000000000001));
    var binormal = sv_cross(axis, tangent);
    var binormal_delta = sv_add(sv_cross(axis_delta, tangent), sv_cross(axis, tangent_delta));
    var point = surface_boundary(u, region);
    var profile_delta = sv_sub(surface_boundary(min(u + 0.0001, 1.0), region),
        surface_boundary(max(u - 0.0001, 0.0), region));
    var taper_delta = sf(1, 1) - 1.0;
    var taper = 1.0 + taper_delta * v;
    var roll_delta = sf(1, 2);
    var roll = roll_delta * v;
    var cr = cos(roll);
    var sr = sin(roll);
    var x = point.x * sf(2, 0) * cr - point.y * sf(2, 1) * sr;
    var z = point.x * sf(2, 0) * sr + point.y * sf(2, 1) * cr;
    var dx = profile_delta.x * sf(2, 0) * cr - profile_delta.y * sf(2, 1) * sr;
    var dz = profile_delta.x * sf(2, 0) * sr + profile_delta.y * sf(2, 1) * cr;
    var du = sv_mul(sv_add(sv_mul(axis, dx), sv_mul(binormal, dz)), taper);
    var dv = sv_add(sv_mul(direction, 3.0 * f32(count)),
        sv_add(sv_mul(axis_delta, x * taper), sv_mul(binormal_delta, z * taper)));
    dv = sv_add(dv, sv_add(sv_mul(axis, taper_delta * x - roll_delta * taper * z),
        sv_mul(binormal, taper_delta * z + roll_delta * taper * x)));
    var normal = sv_cross(dv, du);
    if (sv_dot(normal, sv_cross(tangent, du)) < 0.0) {
        normal = sv_mul(normal, -1.0);
    }

    var result: SurfaceSweepSample;
    result.position = sv_add(curve_cubic3(p0, p1, p2, p3, local),
        sv_mul(sv_add(sv_mul(axis, x), sv_mul(binormal, z)), taper));
    result.normal = normal;
    result.tangent = du;

    return result;
}

fn surface_sweep_point(point: vec3f, v: f32) -> vec3f {
    var taper = 1.0 + (sf(1, 1) - 1.0) * v;
    var a = point.x * sf(2, 0) * taper;
    var b = point.y * sf(2, 1) * taper;
    var roll = sf(1, 2) * v;
    var x = a * cos(roll) - b * sin(roll);
    var z = a * sin(roll) + b * cos(roll);
    var tangent = surface_direction(v);
    var axis = surface_frame(v, tangent);

    return sv_add(surface_curve(v), sv_add(sv_mul(axis, x), sv_mul(sv_cross(axis, tangent), z)));
}

fn surface_loft_point(point: i32, v: f32) -> vec3f {
    var count = i32(sf(0, 2));
    var points = i32(sf(5, 1));
    var offset = i32(sf(4, 0));
    var t = v * f32(count - 1);
    var section = i32(min(floor(t), f32(count - 2)));

    return sv_mix(sp(offset + section * points + point),
        sp(offset + (section + 1) * points + point), t - f32(section));
}

fn surface_prism_corner(i: i32, inset: f32) -> vec3f {
    var count = i32(sf(0, 2));
    var previous = i - 1;
    var next = i + 1;
    if (previous < 0) {
        previous = count - 1;
    }

    if (next == count) {
        next = 0;
    }

    var point = sp(4 + i);
    var incoming = sv_unit(sv_sub(point, sp(4 + previous)));
    var outgoing = sv_unit(sv_sub(sp(4 + next), point));
    var a = sv(-incoming.z, 0.0, incoming.x);
    var b = sv(-outgoing.z, 0.0, outgoing.x);
    var offset = sv_mul(sv_add(a, b), inset / max(1.0 + sv_dot(a, b), 0.000001));

    return sv_add(point, offset);
}

fn surface_outer(u: f32, v: f32, inner: i32) -> vec3f {
    var kind = i32(sf(0, 0));
    var thickness = sf(1, 0) * f32(inner);
    if (kind == 1) {
        return surface_sweep_point(surface_boundary(u, inner), v);
    }

    if (kind == 2) {
        var count = i32(sf(5, 2));
        var first = 0;
        if (inner == 1) {
            first = count;
            count = i32(sf(5, 1)) - count;
        }

        var t = u * f32(count);
        var i = i32(min(floor(t), f32(count - 1)));
        var profile_offset = i32(sf(5, 0));
        var next = i32(sf(profile_offset + first + i, 3));
        return sv_mix(surface_loft_point(first + i, v), surface_loft_point(next, v),
            t - f32(i));
    }

    if (kind == 3) {
        var first = i32(sf(4, 0));
        var count = i32(sf(4, 1));
        if (inner == 1) {
            first = i32(sf(4, 2));
            count = i32(sf(4, 3));
        }

        var point = surface_path(first, count, v);
        return sv(point.x * cos(u * 6.28318530718), point.y,
            point.x * sin(u * 6.28318530718));
    }

    if (kind == 4 || kind == 5) {
        var a = sf(2, 0) + u * (sf(2, 1) - sf(2, 0));
        var b = sf(2, 2) + v * (sf(2, 3) - sf(2, 2));
        if (kind == 4) {
            var radius = sp(3);
            var normal = sv_unit(sv(cos(a) * cos(b) / max(radius.x, 0.000001),
                sin(b) / max(radius.y, 0.000001),
                sin(a) * cos(b) / max(radius.z, 0.000001)));
            return sv_sub(sv(radius.x * cos(a) * cos(b), radius.y * sin(b),
                radius.z * sin(a) * cos(b)), sv_mul(normal, thickness));
        }

        var tube = sf(3, 1) - thickness;
        var radial = sf(3, 0) + tube * cos(b);
        return sv(radial * cos(a), tube * sin(b), radial * sin(a));
    }

    if (kind == 6) {
        var count = i32(sf(0, 2));
        var t = u * f32(count);
        var i = i32(min(floor(t), f32(count - 1)));
        var next = i + 1;
        if (next == count) {
            next = 0;
        }

        var bevel = sf(1, 0);
        var fraction = bevel / max(sf(1, 1), 0.000001);
        var height = fraction + (v - 0.25) * 2.0 * (1.0 - 2.0 * fraction);
        if (v < 0.25) {
            height = v * 4.0 * fraction;
        }

        if (v > 0.75) {
            height = 1.0 - fraction + (v - 0.75) * 4.0 * fraction;
        }

        var edge = min(height, 1.0 - height) * sf(1, 1);
        var inset = sf(1, 2) * height + max(bevel - edge, 0.0);
        var point = sv_mix(surface_prism_corner(i, inset),
            surface_prism_corner(next, inset), t - f32(i));
        return sv(point.x + sf(2, 0) * height, (height - 0.5) * sf(1, 1),
            point.z + sf(2, 1) * height);
    }

    if (kind == 7) {
        var point = surface_boundary(u, inner);
        return sv(point.x, (v - 0.5) * sf(1, 1), point.y);
    }

    return sv(0.0, 0.0, 0.0);
}

fn surface_cap(u: f32, v: f32, side: f32) -> vec3f {
    var x = sf(6, 0) + (sf(6, 1) - sf(6, 0)) * u;
    var y = sf(6, 2) + (sf(6, 3) - sf(6, 2)) * v;
    if (sf(0, 0) == 1.0) {
        return surface_sweep_point(sv(x, y, 0.0), side);
    }

    var offset = i32(sf(7, 0));
    var count = i32(sf(7, 1));
    var profile = i32(sf(5, 0));
    for (var i = 0; i < count; i ++) {
        var ia = i32(sf(offset + i, 0));
        var ib = i32(sf(offset + i, 1));
        var ic = i32(sf(offset + i, 2));
        var a = sp(profile + ia);
        var b = sp(profile + ib);
        var c = sp(profile + ic);
        var area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        var wb = ((x - a.x) * (c.y - a.y) - (y - a.y) * (c.x - a.x)) / area;
        var wc = ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x)) / area;
        if (wb >= -0.00001 && wc >= -0.00001 && wb + wc <= 1.00001) {
            return sv_add(sv_mul(surface_loft_point(ia, side), 1.0 - wb - wc),
                sv_add(sv_mul(surface_loft_point(ib, side), wb),
                    sv_mul(surface_loft_point(ic, side), wc)));
        }
    }

    return surface_loft_point(0, side);
}

fn surface_position(region: i32, u: f32, v: f32) -> vec3f {
    if ((sf(0, 0) == 1.0 || sf(0, 0) == 2.0) && (region == 2 || region == 3)) {
        return surface_cap(u, v, f32(region - 2));
    }

    if (region == 8 || region == 9) {
        var side = f32(region - 8);
        var point = i32(floor(u + 0.5));
        if (sf(0, 0) == 1.0) {
            var profile_offset = i32(sf(5, 0));
            return surface_sweep_point(sp(profile_offset + point), side);
        }

        return surface_loft_point(point, side);
    }

    if (sf(0, 0) == 7.0 && region >= 2) {
        var y = -0.5;
        if (region == 3) {
            y = 0.5;
        }

        return sv(sf(6, 0) + (sf(6, 1) - sf(6, 0)) * u,
            y * sf(1, 1), sf(6, 2) + (sf(6, 3) - sf(6, 2)) * v);
    }

    var a = u;
    var b = 0.0;
    if (region == 3 || region == 7) {
        b = 1.0;
    }

    if (region == 4 || region == 5) {
        a = 0.0;
        if (region == 5) {
            a = 1.0;
        }

        b = u;
    }

    // The point of an outer region, or the outer and inner boundary of a side region, from one
    // call of surface_outer in a loop (see surface_normal).
    var kind = i32(sf(0, 0));
    var first = 0;
    var count = 1;
    if (region < 2) {
        a = u;
        b = v;
        first = region;
    } else if (sf(1, 0) > 0.0 && kind != 6) {
        count = 2;
    }

    var boundary: array<vec3f, 2>;
    for (var i = 0; i < count; i++) {
        boundary[i] = surface_outer(a, b, first + i);
    }

    if (region < 2) {
        return boundary[0];
    }

    var outer = boundary[0];
    var inner = outer;
    if (count == 2) {
        inner = boundary[1];
    } else if (kind == 1) {
        inner = surface_curve(b);
    } else if (kind == 2) {
        inner = surface_loft_point(0, b);
    } else if (kind == 3) {
        inner = sv(0.0, outer.y, 0.0);
    } else if (kind == 6) {
        inner = sp(8);
        inner = sv(inner.x + sf(2, 0) * b, outer.y, inner.z + sf(2, 1) * b);
    }

    return sv_mix(inner, outer, v);
}

fn surface_normal(region: i32, u: f32, v: f32) -> vec3f {
    var cap_region = region;
    if ((sf(0, 0) == 1.0 || sf(0, 0) == 2.0) && (region == 2 || region == 3)) {
        cap_region = region + 6;
    }

    if (cap_region == 8 || cap_region == 9) {
        var side = f32(cap_region - 8);
        var normal = sv(0.0, 1.0, 0.0);
        if (sf(0, 0) == 1.0) {
            normal = surface_direction(side);
        }

        if (sf(0, 0) == 2.0) {
            var triangle = i32(sf(7, 0));
            var ia = i32(sf(triangle, 0));
            var ib = i32(sf(triangle, 1));
            var ic = i32(sf(triangle, 2));
            var a = surface_loft_point(ia, side);
            var b = surface_loft_point(ib, side);
            var c = surface_loft_point(ic, side);
            normal = sv_unit(sv_cross(sv_sub(c, a), sv_sub(b, a)));
        }

        if (cap_region == 8) {
            normal = sv_mul(normal, -1.0);
        }

        return normal;
    }

    if (sf(0, 0) == 7.0 && region >= 2) {
        if (region == 2) {
            return sv(0.0, -1.0, 0.0);
        }

        return sv(0.0, 1.0, 0.0);
    }

    if (sf(0, 0) == 7.0 && region < 2) {
        var profile_normal = surface_profile_normal(u, region);
        if (sv_dot(profile_normal, profile_normal) >= 0.000000000001) {
            return profile_normal;
        }
    } else if (sf(0, 0) == 1.0 && region < 2) {
        var sample = surface_sweep_sample(region, u, v);
        if (sv_dot(sample.normal, sample.normal) >= 0.000000000001) {
            return sv_unit(sample.normal);
        }
    }

    var flip = (region == 1 && sf(0, 0) == 3.0) || region == 3 || region == 4 ||
        region == 7;
    // Central differences at v, and again at a v inside (0, 1) when they meet in a point. The
    // samples share one call of surface_position in a loop: D3D compilers inline every call, and
    // unrolled copies make the mesh pipelines compile slowly enough to trip the GPU watchdog.
    var delta = 0.0001;
    var at = v;
    var du = sv(0.0, 0.0, 0.0);
    var normal = du;
    for (var attempt = 0; attempt < 2; attempt++) {
        var p: array<vec3f, 4>;
        for (var i = 0; i < 4; i++) {
            var su = u;
            var sv_at = at;
            if (i == 0) {
                su = min(u + delta, 1.0);
            } else if (i == 1) {
                su = max(u - delta, 0.0);
            } else if (i == 2) {
                sv_at = min(at + delta, 1.0);
            } else {
                sv_at = max(at - delta, 0.0);
            }

            p[i] = surface_position(region, su, sv_at);
        }

        du = sv_sub(p[0], p[1]);
        normal = sv_cross(sv_sub(p[2], p[3]), du);
        if (flip) {
            normal = sv_mul(normal, -1.0);
        }

        if (sv_dot(normal, normal) >= 0.000000000001) {
            break;
        }

        at = min(max(v, 0.001), 0.999);
    }

    if (sf(0, 0) == 1.0 && region < 2) {
        var outward = sv_cross(surface_direction(v), du);
        if (sv_dot(normal, outward) < 0.0) {
            normal = sv_mul(normal, -1.0);
        }
    }

    return sv_unit(normal);
}
