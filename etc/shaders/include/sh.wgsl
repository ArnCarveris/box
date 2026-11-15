// L2 spherical harmonics irradiance, shared by the ambient of a view, the probe volume of the
// forward pass and the probe debug view.
#pragma once

// Irradiance in a direction from the L2 SH irradiance coefficients E_lm of a probe
// (Ramamoorthi and Hanrahan, Sloan's real basis), before the division by pi and the clamp.
fn sh_irradiance(e0: vec3f, e1: vec3f, e2: vec3f, e3: vec3f, e4: vec3f, e5: vec3f,
    e6: vec3f, e7: vec3f, e8: vec3f, d: vec3f) -> vec3f
{
    let band0 = e0 * 0.2820947918;
    let band1 = (e1 * d.y + e2 * d.z + e3 * d.x) * 0.4886025119;
    let band2 = (e4 * (d.x * d.y) + e5 * (d.y * d.z) + e7 * (d.x * d.z)) * 1.0925484306
        + e6 * (0.3153915653 * (3.0 * d.z * d.z - 1.0))
        + e8 * (0.5462742153 * (d.x * d.x - d.y * d.y));
    return band0 + band1 + band2;
}

// Length of the luminance of band 1 below which a ZH3 probe takes +z as its axis, as
// BOX_RENDER_ZH3_AXIS_EPSILON of render_backend.h.
const ZH3_AXIS_EPSILON: f32 = 1e-8;

// Irradiance of a light probe as ZH3 (Roughton et al., "ZH3: Quadratic Zonal Harmonics"):
// bands 0 and 1 of the L2 coefficients and one zonal band 2 coefficient per channel, along the
// axis of the luminance of band 1, which is not stored. A probe is 4 vec4: (E0, z.r),
// (E1, z.g), (E2, z.b), (E3, 0), with E1..E3 in Sloan's order (y, z, x).
struct Zh3 {
    e0: vec3f,
    e1: vec3f,
    e2: vec3f,
    e3: vec3f,
    z: vec3f,
    axis: vec3f,
};

// Axis of a ZH3 probe from its band 1 coefficients: the normalized luminance of band 1, +z
// when that is about zero.
fn zh3_axis(e1: vec3f, e2: vec3f, e3: vec3f) -> vec3f {
    let w = vec3f(0.2126, 0.7152, 0.0722);
    let a = vec3f(dot(e3, w), dot(e1, w), dot(e2, w));
    let len = length(a);
    return select(vec3f(0.0, 0.0, 1.0), a / max(len, 1e-30), len > ZH3_AXIS_EPSILON);
}

// Zonal band 2 coefficient along an axis a of the band 2 coefficients of an L2 probe: their
// projection onto y_m = sqrt(4 pi / 5) Y_2m(a), so that z y_m rebuilds them.
fn zh3_zonal(e4: vec3f, e5: vec3f, e6: vec3f, e7: vec3f, e8: vec3f, a: vec3f) -> vec3f {
    return (e4 * (a.x * a.y) + e5 * (a.y * a.z) + e7 * (a.x * a.z)) * 1.7320508076
        + e6 * (0.5 * (3.0 * a.z * a.z - 1.0))
        + e8 * (0.8660254038 * (a.x * a.x - a.y * a.y));
}

// The 4 vec4 of a ZH3 probe, with its axis.
fn zh3_load(v0: vec4f, v1: vec4f, v2: vec4f, v3: vec4f) -> Zh3 {
    return Zh3(v0.rgb, v1.rgb, v2.rgb, v3.rgb, vec3f(v0.w, v1.w, v2.w),
        zh3_axis(v1.rgb, v2.rgb, v3.rgb));
}

// Irradiance in a direction from a ZH3 probe, before the division by pi and the clamp: as
// sh_irradiance of its band 2 rebuilt as z y_m, whose sum is z times the Y_20 pattern along
// the axis.
fn zh3_irradiance(p: Zh3, d: vec3f) -> vec3f {
    let t = dot(p.axis, d);
    let band0 = p.e0 * 0.2820947918;
    let band1 = (p.e1 * d.y + p.e2 * d.z + p.e3 * d.x) * 0.4886025119;
    let band2 = p.z * (0.3153915653 * (3.0 * t * t - 1.0));
    return band0 + band1 + band2;
}
