// Spot light shadows from the shadow atlas of the scene, read by the forward pass.
//
// A shadow casting spot light has a square tile of the atlas with a reversed Z perspective along
// its axis: clip depth near * (far - z) / (z * (far - near)) at the distance z along the axis, 1
// at the near plane, the depth the shadow pass writes. A position behind the light, beyond the
// far plane or outside the frustum of the tile is lit.
//
// The lookup moves along the geometric normal by normal_offset texels of the tile at its
// distance, times 1 - N.L with l toward the light, compares the depth of its distance moved
// toward the light by depth_bias texels and filters with the tent of shadow.wgsl; every tap is
// clamped half a texel inside the tile, so none reads a neighbouring tile. Ported from
// SampleSpotLightShadow of Esoterica.
#pragma once

#include "shadow.wgsl"

// A tile of the atlas, see box_render_shadow_record_t.
struct ShadowTile {
    // World to shadow clip space of the light
    matrix: mat4x4f,
    // x, y: top left corner of the tile in atlas uv, z: its edge in uv, w: a texel of the atlas
    // in uv
    rect: vec4f,
    // x: near plane, y: far plane, z: world size of a texel per unit of distance along the axis,
    // w: edge of the tile in texels
    params: vec4f,
    // xyz: world position of the light, w: projection, 0 for the matrix of a spot light, 1 for
    // the octahedron of a point light (point_shadow.wgsl)
    light: vec4f,
};

// Tile records at most, BOX_RENDER_SHADOW_TILES.
const SHADOW_TILES: u32 = 64u;

@group(0) @binding(15) var shadow_atlas: texture_depth_2d;
@group(0) @binding(16) var<uniform> shadow_tiles: array<ShadowTile, SHADOW_TILES>;

// Fraction of a spot light with a tile that reaches a position in a medium, without a normal:
// one depth comparison of the tile, no tent filter, for the many lookups of a raymarch.
fn spot_shadow_point(light: Light, world: vec3f) -> f32 {
    let tile = shadow_tiles[light.shadow];
    let along = dot(world - light.position, light.direction);
    let near_ = tile.params.x;
    let far_ = tile.params.y;
    if (along <= 0.0 || along >= far_) {
        return 1.0;
    }

    let p = tile.matrix * vec4f(world, 1.0);
    if (p.w <= 0.0) {
        return 1.0;
    }

    let uv = p.xy / p.w * vec2f(0.5, -0.5) + vec2f(0.5);
    if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0))) {
        return 1.0;
    }

    let depth = clamp(along - view.spot_bias.x * along * tile.params.z, near_, far_);
    let reference = near_ * (far_ - depth) / (depth * (far_ - near_));
    let size = tile.rect.w;
    let tap = clamp(tile.rect.xy + uv * tile.rect.z, tile.rect.xy + vec2f(0.5 * size),
        tile.rect.xy + vec2f(tile.rect.z - 0.5 * size));
    return textureSampleCompareLevel(shadow_atlas, shadow_sampler, tap, reference);
}
