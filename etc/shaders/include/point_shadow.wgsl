// Point light shadows from the shadow atlas of the scene, read by the forward pass.
//
// A shadow casting point light has a square tile of the atlas holding the whole sphere around
// it: the octahedron of directions unfolded into the square (Praun and Hoppe; Cigolle et al.),
// each of its 8 octants a perspective along the axis signs / sqrt(3) with reversed Z, clip depth
// near * (far - z) / (z * (far - near)) at the distance z along the axis, the depth the shadow
// pass writes. The 4 octants with z >= 0 fill the inner diamond, those with z < 0 fold out to
// the corners. A position beyond the far plane is lit.
//
// The lookup moves along the geometric normal by normal_offset texels of the tile at its
// distance, times 1 - N.L with l toward the light, rebuilds the octahedral uv from the moved
// position and filters with the tent of shadow.wgsl. A tap across an octant edge reads another
// projection, so every tap compares the depth along the axis of its own octant, moved toward the
// light by depth_bias texels. The inner octant edges are continuous; the outer edges of the
// square wrap to the opposite side, which the taps do not follow: they are clamped half a texel
// inside the tile, so none reads a neighbouring tile. Ported from SamplePointLightShadow of
// Esoterica.
#pragma once

#include "spot_shadow.wgsl"

// 1 / sqrt(3), the length of the axis of an octant
const POINT_SHADOW_AXIS: f32 = 0.57735026919;

// ShadowTile.light.w above which a tile record is the octahedron of a point light, 1 against 0
// for a spot light.
const SHADOW_OCTAHEDRAL: f32 = 0.5;

// Sign of each component, 1 for 0.
fn point_shadow_sign(v: vec2f) -> vec2f {
    return select(vec2f(-1.0), vec2f(1.0), v >= vec2f(0.0));
}

// Octahedral uv in the tile of a direction from the light, not necessarily normalized
// (ComputeOctahedralShadowCoordinates of Esoterica).
fn point_shadow_uv(to: vec3f) -> vec2f {
    let l1 = max(abs(to.x) + abs(to.y) + abs(to.z), 1e-20);
    var p = to.xy / l1;
    if (to.z < 0.0) {
        p = (1.0 - abs(p.yx)) * point_shadow_sign(p);
    }
    return p * 0.5 + vec2f(0.5);
}

// Axis signs of the octant a uv of the tile lies in (ComputeOctahedralShadowFaceSigns).
fn point_shadow_signs(uv: vec2f) -> vec3f {
    let p = uv * 2.0 - vec2f(1.0);
    return vec3f(
        select(1.0, -1.0, p.x < 0.0),
        select(1.0, -1.0, p.y < 0.0),
        select(-1.0, 1.0, abs(p.x) + abs(p.y) <= 1.0));
}

// Reference depth of a distance along an octant axis moved toward the light by a world bias,
// in the reversed Z of the tile.
fn point_shadow_depth(axis: f32, near_: f32, far_: f32, bias: f32) -> f32 {
    let depth = clamp(axis - bias, near_, far_);
    return near_ * (far_ - depth) / (depth * (far_ - near_));
}

// Fraction of a spot or point light with a tile that reaches a position with a geometric normal,
// by the projection of its tile record. A spot light compares every tap with the depth along its
// axis, a point light with the depth along the axis of the octant of the tap. Both share one
// filter loop, read as in shadow_cascade, as D3D compilers inline every call with its loops.
fn punctual_shadow(light: Light, world: vec3f, normal: vec3f) -> f32 {
    let tile = shadow_tiles[light.shadow];
    let octahedral = tile.light.w > SHADOW_OCTAHEDRAL;
    let near_ = tile.params.x;
    let far_ = tile.params.y;
    var along = 0.0;
    var texel = 0.0;
    var moved_to = vec3f(0.0);
    var uv = vec2f(0.0);
    if (octahedral) {
        let to = world - tile.light.xyz;
        let len = length(to);
        if (len < 1e-6) {
            return 1.0;
        }

        along = dot(to, point_shadow_signs(point_shadow_uv(to)) * POINT_SHADOW_AXIS);
        if (along <= 0.0 || along >= far_) {
            return 1.0;
        }

        texel = along * tile.params.z;
        let moved = world + shadow_normal_offset(normal, -to / len, view.spot_bias.y * texel);
        moved_to = moved - tile.light.xyz;
        uv = point_shadow_uv(moved_to);
    } else {
        let beam = dot(world - light.position, light.direction);
        if (beam <= 0.0) {
            return 1.0;
        }

        texel = beam * tile.params.z;
        let l = normalize(light.position - world);
        let moved = world + shadow_normal_offset(normal, l, view.spot_bias.y * texel);
        along = dot(moved - light.position, light.direction);
        if (along >= far_) {
            return 1.0;
        }

        let p = tile.matrix * vec4f(moved, 1.0);
        if (p.w <= 0.0) {
            return 1.0;
        }

        uv = p.xy / p.w * vec2f(0.5, -0.5) + vec2f(0.5);
        if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0))) {
            return 1.0;
        }
    }

    let bias = view.spot_bias.x * texel;
    let size = tile.rect.w;
    let lo = tile.rect.xy + vec2f(0.5 * size);
    let hi = tile.rect.xy + vec2f(tile.rect.z - 0.5 * size);
    let t = shadow_filter_taps(tile.rect.xy + uv * tile.rect.z, vec2f(size), view.spot_config.y);
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

            let tap = clamp(t.base + vec2f(x.x, y.x), lo, hi);
            var axis = along;
            if (octahedral) {
                let signs = point_shadow_signs((tap - tile.rect.xy) / tile.rect.z);
                axis = dot(moved_to, signs * POINT_SHADOW_AXIS);
            }

            let reference = point_shadow_depth(axis, near_, far_, bias);
            let lit = textureSampleCompareLevel(shadow_atlas, shadow_sampler, tap, reference);
            sum += wx.x * wy.x * lit;
            x = x.yzwx;
            wx = wx.yzwx;
        }

        y = y.yzwx;
        wy = wy.yzwx;
    }

    return sum;
}

// Fraction of a point light with a tile that reaches a position in a medium, without a normal:
// one depth comparison of the octant the position lies in, no tent filter, for the many lookups
// of a raymarch.
fn point_shadow_point(light: Light, world: vec3f) -> f32 {
    let tile = shadow_tiles[light.shadow];
    let near_ = tile.params.x;
    let far_ = tile.params.y;
    let to = world - tile.light.xyz;
    let uv = point_shadow_uv(to);
    let along = dot(to, point_shadow_signs(uv) * POINT_SHADOW_AXIS);
    if (along <= 0.0 || along >= far_) {
        return 1.0;
    }

    let reference = point_shadow_depth(along, near_, far_,
        view.spot_bias.x * along * tile.params.z);
    let size = tile.rect.w;
    let tap = clamp(tile.rect.xy + uv * tile.rect.z, tile.rect.xy + vec2f(0.5 * size),
        tile.rect.xy + vec2f(tile.rect.z - 0.5 * size));
    return textureSampleCompareLevel(shadow_atlas, shadow_sampler, tap, reference);
}

// Fraction of a spot or point light with a tile that reaches a position in a medium, one depth
// comparison, as punctual_shadow with a zero normal but without its filter.
fn punctual_shadow_point(light: Light, world: vec3f) -> f32 {
    if (shadow_tiles[light.shadow].light.w > SHADOW_OCTAHEDRAL) {
        return point_shadow_point(light, world);
    }
    return spot_shadow_point(light, world);
}
