// The layers of a TextureBlend (box_render_texture_blend_t) as the blend group of a batch binds
// them (gpu_texture.c): the albedo, ORM and normal of each layer as a slice of an array, the
// mask as stored and the BlendUniforms. mesh.wgsl blends every map of the layers, the alpha
// tested depth prepass and shadow.wgsl blend the albedo alpha alone (blend_alpha).
//
// Include after material.wgsl, whose projections blend_alpha follows.
#pragma once

// The TextureTransform of each layer (scale in xy, offset in zw), the slices of the arrays, and
// BLEND_MASKED and BLEND_SINGLE flags.
struct BlendUniforms {
    transforms: array<vec4f, 4>,
    count: u32,
    flags: u32,
};

// The mask is loaded; without it layer 0 shows alone.
const BLEND_MASKED: u32 = 1u;

// The mask has a single channel r, which weighs layers 0 and 1 by 1 - r and r.
const BLEND_SINGLE: u32 = 2u;

// Weights of the layers of a TextureBlend from its mask: red, green, blue and alpha, or 1 - r and
// r for a single channel mask, without the layers past the slice count, normalized by their sum;
// layer 0 alone where the sum is 0 and without a mask.
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

// Weighted albedo alpha of the layers at the texture coordinates uv of the shape, whose screen
// gradients are gx and gy: the mask at uv, and each layer with a weight once, at uv times its
// TextureTransform with the gradients scaled by it. Explicit gradients, so it holds in any
// control flow.
fn blend_alpha_at(albedo: texture_2d_array<f32>, mask: texture_2d<f32>, smp: sampler,
    u: BlendUniforms, uv: vec2f, gx: vec2f, gy: vec2f) -> f32 {
    let w = blend_weights(textureSampleGrad(mask, smp, uv, gx, gy), u);
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

// Blended albedo alpha of an alpha tested fragment of a TextureBlend, as mapping_alpha makes the
// alpha of one albedo layer: at the interpolated UV, or for a MappingObject or MappingWorld
// material (mapped not 0) projected with mapping_uv, uv its offset; with a blend (mapped above 1)
// the alpha of each projection with a weight, with the gradients of that projection, so a
// fragment whose normal is along an axis blends once. mapped is flat, the same for every
// fragment of a primitive and so of a quad, so the derivatives of either branch hold.
@diagnostic(off, derivative_uniformity)
fn blend_alpha(albedo: texture_2d_array<f32>, mask: texture_2d<f32>, smp: sampler,
    u: BlendUniforms, uv: vec2f, mapped: f32, object: vec4f, normal: vec3f) -> f32 {
    var alpha = 0.0;
    if (mapped > 1.0) {
        let dx = dpdx(object);
        let dy = dpdy(object);
        let w = mapping_weights(normal, mapped - 1.0);
        let s = mapping_signs(normal);
        var planes = mapping_planes(object, s);
        var gx = mapping_planes(dx, s);
        var gy = mapping_planes(dy, s);
        for (var k = 0u; k < 3u; k++) {
            if (w[k] > 0.0) {
                alpha += w[k] * blend_alpha_at(albedo, mask, smp, u, planes[k] + uv, gx[k],
                    gy[k]);
            }
        }
    } else {
        var at = uv;
        if (mapped != 0.0) {
            at = mapping_uv(object, normal, uv);
        }
        alpha = blend_alpha_at(albedo, mask, smp, u, at, dpdx(at), dpdy(at));
    }
    return alpha;
}
