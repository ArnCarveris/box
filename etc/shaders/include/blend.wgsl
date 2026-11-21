// The layers of a TextureBlend (box_render_texture_blend_t) as the blend group of a batch binds
// them (gpu_texture.c): the albedo, ORM and normal of each layer as a slice of an array, the
// mask as stored, the BlendUniforms and the height of each layer in the red of a slice of a
// fourth array. mesh.wgsl blends every map of the layers, the alpha tested depth prepass and
// shadow.wgsl blend the albedo alpha alone (blend_alpha), with the same height shaped weights.
//
// Include after material.wgsl, whose projections blend_alpha follows.
#pragma once

// The TextureTransform of each layer (scale in xy, offset in zw), the slices of the arrays, the
// BLEND_MASKED, BLEND_SINGLE, BLEND_HEIGHT, BLEND_TILES and BLEND_TERRAIN flags, the contrast of
// the height based transitions, 0 for the linear blend, and of a BLEND_TERRAIN blend with a mask
// atlas the texels along an edge of the masks of a tile | the cells across the atlas << 16, and
// the same of its bake atlas, 0 without one. The grass_* of a BLEND_TERRAIN blend are the far
// field of the Grass on the terrain (box_render_grass_far_t), all zero without one.
struct BlendUniforms {
    transforms: array<vec4f, 4>,
    count: u32,
    flags: u32,
    contrast: f32,
    terrain: u32,
    bake: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    grass_base: vec4f,          // Linear color at the base of the blades, variation
    grass_tip: vec4f,           // Linear color at the tip, share of the ground covered from
                                // above
    grass_range: vec4f,         // Distances the far field starts to fade in and is full, the
                                // blades end, tallest blade
    grass_wind_field: vec4f,    // GrassWind.field (grass_wind.wgsl)
    grass_wind_strength: vec4f, // GrassWind.strength
    grass_wind_time: vec4f,     // GrassWind.time
    grass_config: vec4u,        // Mask channel plus one, 0 for none; 1 with a Grass
};

// The mask is loaded; without it layer 0 shows alone.
const BLEND_MASKED: u32 = 1u;

// The mask has a single channel r, which weighs layers 0 and 1 by 1 - r and r.
const BLEND_SINGLE: u32 = 2u;

// Layer 0 has a height image, shifted left by k for layer k; a layer without one is flat at 0.5.
const BLEND_HEIGHT: u32 = 4u;

const BLEND_TILES: u32 = 256u;

// The tiles of a Terrain with masks (BOX_GPU_BLEND_TERRAIN): the mask is the mask atlas of their
// grid mesh, read at the cell of the tile of a fragment (terrain_mask of mesh_blend.wgsl); the
// fragment carries its grid uv in uv.xy and its mapping offset in flat4.w.
const BLEND_TERRAIN: u32 = 512u;

// Share of the shaped weights below which a layer of a blend with a contrast is dropped, under a
// step of an 8 bit channel, so the fragments past a border sample the maps of one layer only.
const BLEND_HEIGHT_DROP: f32 = 1.0 / 256.0;

// Normalized layer weights from the mask (rgba, or 1 - r and r for a single channel mask); layer 0
// alone where the sum is 0 or without a mask.
fn blend_weights(mask: vec4f, u: BlendUniforms) -> vec4f {
    var w = mask;
    if ((u.flags & BLEND_SINGLE) != 0u) {
        w = vec4f(1.0 - mask.r, mask.r, 0.0, 0.0);
    }
    w = select(vec4f(0.0), max(w, vec4f(0.0)), vec4u(0u, 1u, 2u, 3u) < vec4u(u.count));
    let sum = w.x + w.y + w.z + w.w;
    let alone = (u.flags & BLEND_MASKED) == 0u || !(sum > 1e-6);
    return select(w / max(sum, 1e-6), vec4f(1.0, 0.0, 0.0, 0.0), alone);
}

// Height shaped weights: each weight w is multiplied by exp(contrast * (h + w - m)), m the largest
// h + w, then normalized. Near a border the higher layer wins, with a transition about
// 1 / contrast wide. Layers under BLEND_HEIGHT_DROP of the sum are dropped.
fn blend_height_weights(heights: texture_2d_array<f32>, smp: sampler, u: BlendUniforms,
    w: vec4f, uv: vec2f, gx: vec2f, gy: vec2f) -> vec4f {
    if (!(u.contrast > 0.0)) {
        return w;
    }
    var s = vec4f(0.0);
    var m = -1e30;
    for (var k = 0u; k < 4u; k++) {
        if (w[k] <= 0.0) {
            continue;
        }
        var h = 0.5;
        if ((u.flags & (BLEND_HEIGHT << k)) != 0u) {
            let t = u.transforms[k];
            h = textureSampleGrad(heights, smp, uv * t.xy + t.zw, k, gx * t.xy, gy * t.xy).r;
        }
        s[k] = h + w[k];
        m = max(m, s[k]);
    }
    var o = vec4f(0.0);
    for (var k = 0u; k < 4u; k++) {
        if (w[k] > 0.0) {
            o[k] = w[k] * exp(u.contrast * (s[k] - m));
        }
    }
    let sum = o.x + o.y + o.z + o.w;
    if (!(sum > 1e-12)) {
        return w;
    }
    o = select(vec4f(0.0), o, o > vec4f(sum * BLEND_HEIGHT_DROP));
    return o / (o.x + o.y + o.z + o.w);
}

// Weighted albedo alpha of the layers at uv. Explicit gradients, so it holds in any control flow.
fn blend_alpha_at(albedo: texture_2d_array<f32>, mask: texture_2d<f32>,
    heights: texture_2d_array<f32>, smp: sampler, u: BlendUniforms, uv: vec2f, gx: vec2f,
    gy: vec2f) -> f32 {
    let w = blend_height_weights(heights, smp, u,
        blend_weights(textureSampleGrad(mask, smp, uv, gx, gy), u), uv, gx, gy);
    var alpha = 0.0;
    for (var k = 0u; k < 4u; k++) {
        if (w[k] <= 0.0) {
            continue;
        }
        let t = u.transforms[k];
        alpha += w[k] * textureSampleGrad(albedo, smp, uv * t.xy + t.zw, k, gx * t.xy,
            gy * t.xy).a;
    }
    return alpha;
}

// Blended albedo alpha of an alpha tested fragment, as mapping_alpha does for one layer. mapped is
// flat, the same for every fragment of a primitive and so of a quad, so the derivatives of either
// branch hold.
@diagnostic(off, derivative_uniformity)
fn blend_alpha(albedo: texture_2d_array<f32>, mask: texture_2d<f32>,
    heights: texture_2d_array<f32>, smp: sampler, u: BlendUniforms, uv: vec2f, mapped: f32,
    object: vec4f, normal: vec3f) -> f32 {
    var alpha = 0.0;
    if ((u.flags & (BLEND_TILES | BLEND_TERRAIN)) != 0u) {
        alpha = 1.0;
    } else if (mapped > 1.0) {
        let dx = dpdx(object);
        let dy = dpdy(object);
        let w = mapping_weights(normal, mapped - 1.0);
        let s = mapping_signs(normal);
        var planes = mapping_planes(object, s);
        var gx = mapping_planes(dx, s);
        var gy = mapping_planes(dy, s);
        for (var k = 0u; k < 3u; k++) {
            if (w[k] > 0.0) {
                alpha += w[k] * blend_alpha_at(albedo, mask, heights, smp, u, planes[k] + uv,
                    gx[k], gy[k]);
            }
        }
    } else {
        var at = uv;
        if (mapped != 0.0) {
            at = mapping_uv(object, normal, uv);
        }
        alpha = blend_alpha_at(albedo, mask, heights, smp, u, at, dpdx(at), dpdy(at));
    }
    return alpha;
}
