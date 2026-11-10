// Cascaded shadow map of the first directional light of the scene, read by the forward pass.
#pragma once

@group(0) @binding(3) var shadow_map: texture_depth_2d_array;
@group(0) @binding(4) var shadow_sampler: sampler_comparison;

// Bilinear comparison taps of a tent filter: up to 4 by 4 taps at base plus the offsets
// (x[i], y[j]) in uv, weighted by wx[i] * wy[j]; the weights sum to 1. A tap in use has a
// positive weight, and the unused taps have a weight of 0 and come last.
struct ShadowTaps {
    base: vec2f,
    x: vec4f,
    y: vec4f,
    wx: vec4f,
    wy: vec4f,
};

// Taps of the tent filter of a width in texels around a uv, for a map with a texel size in uv.
// The 1D tent covers the width + 1 texels around the position, adjacent texel pairs merge into
// one bilinear tap and the 2D weights are the products of the 1D ones. Touches no texture, so
// any shadow map can be filtered with it.
fn shadow_filter_taps(uv: vec2f, texel: vec2f, size: u32) -> ShadowTaps {
    var t: ShadowTaps;
    if (size <= 1u) {
        t.base = uv;
        t.wx = vec4f(1.0, 0.0, 0.0, 0.0);
        t.wy = t.wx;
        return t;
    }

    let p = uv / texel;
    let b = floor(p + vec2f(0.5));
    let s = p + vec2f(0.5) - b;
    var o0: vec2f;
    var o1: vec2f;
    var o2 = vec2f(0.0);
    var o3 = vec2f(0.0);
    var w0: vec2f;
    var w1: vec2f;
    var w2 = vec2f(0.0);
    var w3 = vec2f(0.0);
    if (size <= 3u) {
        w0 = 3.0 - 2.0 * s;
        w1 = 1.0 + 2.0 * s;
        o0 = (2.0 - s) / w0 - 1.0;
        o1 = s / w1 + 1.0;
        w0 /= 4.0;
        w1 /= 4.0;
    } else if (size <= 5u) {
        w0 = 4.0 - 3.0 * s;
        w1 = vec2f(7.0);
        w2 = 1.0 + 3.0 * s;
        o0 = (3.0 - 2.0 * s) / w0 - 2.0;
        o1 = (3.0 + s) / w1;
        o2 = s / w2 + 2.0;
        w0 /= 12.0;
        w1 /= 12.0;
        w2 /= 12.0;
    } else {
        w0 = 6.0 - 5.0 * s;
        w1 = 28.0 - 11.0 * s;
        w2 = 17.0 + 11.0 * s;
        w3 = 1.0 + 5.0 * s;
        o0 = (5.0 - 4.0 * s) / w0 - 3.0;
        o1 = (16.0 - 4.0 * s) / w1 - 1.0;
        o2 = (5.0 + 7.0 * s) / w2 + 1.0;
        o3 = s / w3 + 3.0;
        w0 /= 52.0;
        w1 /= 52.0;
        w2 /= 52.0;
        w3 /= 52.0;
    }

    t.base = (b - vec2f(0.5)) * texel;
    t.x = vec4f(o0.x, o1.x, o2.x, o3.x) * texel.x;
    t.y = vec4f(o0.y, o1.y, o2.y, o3.y) * texel.y;
    t.wx = vec4f(w0.x, w1.x, w2.x, w3.x);
    t.wy = vec4f(w0.y, w1.y, w2.y, w3.y);
    return t;
}

// World offset of a shadow lookup along the geometric normal n: offset times 1 - N.L, with l
// the direction toward the light, so a surface facing the light is not offset.
fn shadow_normal_offset(n: vec3f, l: vec3f, offset: f32) -> vec3f {
    return n * (offset * (1.0 - saturate(dot(n, l))));
}

// Direction toward the light of the cascades, the depth axis of their light space.
fn shadow_light() -> vec3f {
    let m = view.shadow_matrix[0];
    return normalize(vec3f(m[0].z, m[1].z, m[2].z));
}

// Distance of a position from the edge of the box of a cascade, shrunk by the margin, in half
// extents of the cascade; negative outside. The box reaches far toward the light to hold the
// casters, so only its clip depth bounds it on that side.
fn shadow_box_edge(index: u32, world: vec3f) -> f32 {
    let p = view.shadow_matrix[index] * vec4f(world, 1.0);
    let far_ = 1.0 - p.z * view.shadow_depth[index];
    let edge = 1.0 - view.shadow_edge.x - max(max(abs(p.x), abs(p.y)), far_);
    return select(edge, -1.0, p.z > 1.0);
}

// Fraction of light that reaches a position with a geometric normal through one cascade. The
// taps are read in loops with a constant bound that stop at the first weight of 0, and the
// offsets and weights rotate instead of being indexed by the loop: D3D compilers turn a local
// indexed at run time into an indexable copy and are slow on loops with a run time bound.
fn shadow_cascade(index: u32, world: vec3f, normal: vec3f, l: vec3f) -> f32 {
    let offset = world + shadow_normal_offset(normal, l, view.shadow_offset[index]);
    let p = view.shadow_matrix[index] * vec4f(offset, 1.0);
    let uv = p.xy * vec2f(0.5, -0.5) + vec2f(0.5);
    let depth = p.z + view.shadow_bias[index];
    let texel = vec2f(view.shadow_params.z);
    if (view.shadow_config.y == 3u) {
        let q = shadow_filter_taps(uv, texel, 3u);
        let a = textureSampleCompareLevel(shadow_map, shadow_sampler,
            q.base + vec2f(q.x.x, q.y.x), index, depth);
        let b = textureSampleCompareLevel(shadow_map, shadow_sampler,
            q.base + vec2f(q.x.y, q.y.x), index, depth);
        let c = textureSampleCompareLevel(shadow_map, shadow_sampler,
            q.base + vec2f(q.x.x, q.y.y), index, depth);
        let d = textureSampleCompareLevel(shadow_map, shadow_sampler,
            q.base + vec2f(q.x.y, q.y.y), index, depth);
        return q.wy.x * (q.wx.x * a + q.wx.y * b) + q.wy.y * (q.wx.x * c + q.wx.y * d);
    }

    let t = shadow_filter_taps(uv, texel, view.shadow_config.y);
    var sum = 0.0;
    var y = t.y;
    var wy = t.wy;
    for (var j = 0u; j < 4u; j++) {
        if (wy.x == 0.0) {
            break;
        }

        var x = t.x;
        var wx = t.wx;
        for (var i = 0u; i < 4u; i++) {
            if (wx.x == 0.0) {
                break;
            }

            let tap = t.base + vec2f(x.x, y.x);
            let lit = textureSampleCompareLevel(shadow_map, shadow_sampler, tap, index, depth);
            sum += wx.x * wy.x * lit;
            x = x.yzwx;
            wx = wx.yzwx;
        }

        y = y.yzwx;
        wy = wy.yzwx;
    }

    return sum;
}

// Fraction of the first directional light that reaches a position with a geometric normal. Both
// cascades of a blend come from one call of shadow_cascade in a loop, as D3D compilers inline
// every call.
fn shadow_factor(world: vec3f, normal: vec3f) -> f32 {
    let count = view.shadow_config.x;
    let depth = -(view.view * vec4f(world, 1.0)).z;
    if (count == 0u || depth >= view.shadow_params.y) {
        return 1.0;
    }

    var index = count;
    var edge = 0.0;
    for (var i = 0u; i < count; i++) {
        edge = shadow_box_edge(i, world);
        if (edge >= 0.0) {
            index = i;
            break;
        }
    }

    if (index == count) {
        return 1.0;
    }

    let l = shadow_light();
    let band = view.shadow_edge.y;
    let next = index + 1u;
    var cascades = 1u;
    if (edge < band && next < count && shadow_box_edge(next, world) >= 0.0) {
        cascades = 2u;
    }

    var shadow = 1.0;
    for (var i = 0u; i < cascades; i++) {
        let cascade = shadow_cascade(index + i, world, normal, l);
        if (i == 0u) {
            shadow = cascade;
        } else {
            shadow = mix(cascade, shadow, smoothstep(0.0, band, edge));
        }
    }

    let span = max(view.shadow_params.y - view.shadow_params.x, 1e-4);
    let fade = clamp((depth - view.shadow_params.x) / span, 0.0, 1.0);
    return mix(shadow, 1.0, fade);
}

// Fraction of the first directional light that reaches a point in a medium, such as a fog
// volume, which has no surface: one bilinear comparison in the first cascade whose box holds the
// point, with the depth bias of the cascade but no normal offset, no filter, no blend into the
// next cascade and no fade. 1 without shadows, beyond them and past the last cascade.
fn shadow_point(world: vec3f) -> f32 {
    let count = view.shadow_config.x;
    let depth = -(view.view * vec4f(world, 1.0)).z;
    if (count == 0u || depth >= view.shadow_params.y) {
        return 1.0;
    }

    for (var i = 0u; i < count; i++) {
        if (shadow_box_edge(i, world) >= 0.0) {
            let p = view.shadow_matrix[i] * vec4f(world, 1.0);
            let uv = p.xy * vec2f(0.5, -0.5) + vec2f(0.5);
            let compare = p.z + view.shadow_bias[i];
            return textureSampleCompareLevel(shadow_map, shadow_sampler, uv, i, compare);
        }
    }

    return 1.0;
}
